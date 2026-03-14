#include "iora/codecs/pipeline/media_pipeline.hpp"
#include "iora/codecs/dsp/resampler.hpp"

#include <algorithm>
#include <chrono>
#include <queue>
#include <thread>
#include <unordered_set>

namespace iora {
namespace codecs {

using LifecycleState = iora::common::LifecycleState;
using LifecycleResult = iora::common::LifecycleResult;
using DrainStats = iora::common::DrainStats;

namespace {

/// Internal fan-out handler — clones buffers and distributes to N downstream
/// handlers. Maintains its own downstream list (does NOT use _next).
class FanOutHandler : public IMediaHandler
{
public:
  void addDownstream(std::shared_ptr<IMediaHandler> handler)
  {
    _downstreams.push_back(std::move(handler));
  }

  void incoming(std::shared_ptr<MediaBuffer> buffer) override
  {
    for (std::size_t i = 0; i < _downstreams.size(); ++i)
    {
      if (i == _downstreams.size() - 1)
      {
        // Last downstream gets the original (avoid extra clone).
        _downstreams[i]->incoming(std::move(buffer));
      }
      else
      {
        _downstreams[i]->incoming(buffer->clone());
      }
    }
  }

  void outgoing(std::shared_ptr<MediaBuffer> buffer) override
  {
    for (std::size_t i = 0; i < _downstreams.size(); ++i)
    {
      if (i == _downstreams.size() - 1)
      {
        _downstreams[i]->outgoing(std::move(buffer));
      }
      else
      {
        _downstreams[i]->outgoing(buffer->clone());
      }
    }
  }

  std::size_t downstreamCount() const noexcept
  {
    return _downstreams.size();
  }

private:
  std::vector<std::shared_ptr<IMediaHandler>> _downstreams;
};

/// Internal resampler stage — resamples S16 audio between sample rates.
class ResamplerStageHandler : public IMediaHandler
{
public:
  ResamplerStageHandler(std::uint32_t inputRate, std::uint32_t outputRate,
                        std::uint32_t channels = 1)
    : _resampler(inputRate, outputRate, channels)
    , _channels(channels)
  {
  }

  void incoming(std::shared_ptr<MediaBuffer> buffer) override
  {
    auto inSamples = static_cast<std::uint32_t>(
      buffer->size() / (sizeof(std::int16_t) * _channels));
    auto outSamples = Resampler::estimateOutputSamples(
      inSamples, _resampler.inputRate(), _resampler.outputRate());

    auto outBuf = MediaBuffer::create(
      outSamples * _channels * sizeof(std::int16_t));
    outBuf->copyMetadataFrom(*buffer);

    auto inLen = inSamples;
    auto outLen = outSamples;
    _resampler.process(
      reinterpret_cast<const std::int16_t*>(buffer->data()),
      inLen,
      reinterpret_cast<std::int16_t*>(outBuf->data()),
      outLen);

    outBuf->setSize(outLen * _channels * sizeof(std::int16_t));
    forwardIncoming(std::move(outBuf));
  }

  void outgoing(std::shared_ptr<MediaBuffer> buffer) override
  {
    forwardOutgoing(std::move(buffer));
  }

  std::uint32_t inputRate() const noexcept { return _resampler.inputRate(); }
  std::uint32_t outputRate() const noexcept { return _resampler.outputRate(); }

private:
  Resampler _resampler;
  std::uint32_t _channels;
};

/// Internal channel mixer — mono↔stereo conversion.
class ChannelMixerStageHandler : public IMediaHandler
{
public:
  ChannelMixerStageHandler(std::uint8_t inputChannels,
                           std::uint8_t outputChannels)
    : _inputChannels(inputChannels)
    , _outputChannels(outputChannels)
  {
  }

  void incoming(std::shared_ptr<MediaBuffer> buffer) override
  {
    auto inSamples = buffer->size() / (sizeof(std::int16_t) * _inputChannels);
    auto outBytes = inSamples * sizeof(std::int16_t) * _outputChannels;
    auto outBuf = MediaBuffer::create(outBytes);
    outBuf->copyMetadataFrom(*buffer);

    const auto* in = reinterpret_cast<const std::int16_t*>(buffer->data());
    auto* out = reinterpret_cast<std::int16_t*>(outBuf->data());

    if (_inputChannels == 1 && _outputChannels == 2)
    {
      // Mono → stereo: duplicate.
      for (std::size_t i = 0; i < inSamples; ++i)
      {
        out[i * 2] = in[i];
        out[i * 2 + 1] = in[i];
      }
    }
    else if (_inputChannels == 2 && _outputChannels == 1)
    {
      // Stereo → mono: average.
      for (std::size_t i = 0; i < inSamples; ++i)
      {
        out[i] = static_cast<std::int16_t>(
          (static_cast<std::int32_t>(in[i * 2]) +
           static_cast<std::int32_t>(in[i * 2 + 1])) / 2);
      }
    }

    outBuf->setSize(outBytes);
    forwardIncoming(std::move(outBuf));
  }

  void outgoing(std::shared_ptr<MediaBuffer> buffer) override
  {
    forwardOutgoing(std::move(buffer));
  }

private:
  std::uint8_t _inputChannels;
  std::uint8_t _outputChannels;
};

/// Internal format converter — converts between SampleFormat types.
class FormatConverterStageHandler : public IMediaHandler
{
public:
  FormatConverterStageHandler(SampleFormat inputFmt, SampleFormat outputFmt)
    : _inputFmt(inputFmt)
    , _outputFmt(outputFmt)
  {
  }

  void incoming(std::shared_ptr<MediaBuffer> buffer) override
  {
    auto inBps = bytesPerSample(_inputFmt);
    auto outBps = bytesPerSample(_outputFmt);
    auto sampleCount = buffer->size() / inBps;
    auto outBytes = sampleCount * outBps;

    auto outBuf = MediaBuffer::create(outBytes);
    outBuf->copyMetadataFrom(*buffer);

    convertSamples(buffer->data(), _inputFmt,
                   outBuf->data(), _outputFmt, sampleCount);

    outBuf->setSize(outBytes);
    forwardIncoming(std::move(outBuf));
  }

  void outgoing(std::shared_ptr<MediaBuffer> buffer) override
  {
    forwardOutgoing(std::move(buffer));
  }

private:
  SampleFormat _inputFmt;
  SampleFormat _outputFmt;
};

} // namespace

MediaPipeline::MediaPipeline(iora::core::ThreadPool* threadPool,
                             iora::core::TimerService* timerService)
  : _threadPool(threadPool)
  , _timerService(timerService)
{
}

// -- Graph construction --

bool MediaPipeline::addStage(const std::string& name,
                             std::shared_ptr<IMediaHandler> handler)
{
  if (!isGraphMutable() || !handler || name.empty())
  {
    return false;
  }

  if (_stages.count(name))
  {
    return false; // Duplicate name.
  }

  auto instrumented = std::make_shared<InstrumentedStage>(name, std::move(handler));

  StageEntry entry;
  entry.instrumented = std::move(instrumented);
  _stages.emplace(name, std::move(entry));
  return true;
}

bool MediaPipeline::addStage(const std::string& name,
                             std::shared_ptr<IMediaHandler> handler,
                             const StageFormat& inputFormat,
                             const StageFormat& outputFormat)
{
  if (!addStage(name, handler))
  {
    return false;
  }

  auto it = _stages.find(name);
  it->second.inputFormat = inputFormat;
  it->second.outputFormat = outputFormat;
  return true;
}

bool MediaPipeline::connectStages(const std::string& sourceName,
                                  const std::string& destName)
{
  if (!isGraphMutable())
  {
    return false;
  }

  if (sourceName == destName)
  {
    return false; // Self-loop.
  }

  auto srcIt = _stages.find(sourceName);
  auto dstIt = _stages.find(destName);
  if (srcIt == _stages.end() || dstIt == _stages.end())
  {
    return false;
  }

  // Duplicate edge detection.
  auto& outEdges = srcIt->second.outEdges;
  if (std::find(outEdges.begin(), outEdges.end(), destName) != outEdges.end())
  {
    return false;
  }

  // Cycle detection: would adding source→dest create a cycle?
  if (wouldCreateCycle(sourceName, destName))
  {
    return false;
  }

  // Format negotiation: auto-insert conversion stages if needed.
  // Returns true if it handled all wiring (stages were inserted).
  if (negotiateFormats(sourceName, destName))
  {
    return true;
  }

  // No format conversion needed — record direct edge.
  srcIt->second.outEdges.push_back(destName);
  dstIt->second.inEdges.push_back(sourceName);

  // Re-wire the source's connections.
  wireStageConnections(sourceName);
  return true;
}

bool MediaPipeline::removeStage(const std::string& name)
{
  if (!isGraphMutable())
  {
    return false;
  }

  auto it = _stages.find(name);
  if (it == _stages.end())
  {
    return false;
  }

  // Clean up inbound edges (remove this stage from sources' outEdges).
  for (const auto& srcName : it->second.inEdges)
  {
    auto srcIt = _stages.find(srcName);
    if (srcIt != _stages.end())
    {
      auto& edges = srcIt->second.outEdges;
      edges.erase(
        std::remove(edges.begin(), edges.end(), name),
        edges.end());
      // Re-wire source connections after edge removal.
      wireStageConnections(srcName);
    }
  }

  // Clean up outbound edges (remove this stage from dests' inEdges).
  for (const auto& dstName : it->second.outEdges)
  {
    auto dstIt = _stages.find(dstName);
    if (dstIt != _stages.end())
    {
      auto& edges = dstIt->second.inEdges;
      edges.erase(
        std::remove(edges.begin(), edges.end(), name),
        edges.end());
    }
  }

  _stages.erase(it);
  return true;
}

std::shared_ptr<IMediaHandler> MediaPipeline::getStage(
  const std::string& name) const
{
  auto it = _stages.find(name);
  if (it == _stages.end())
  {
    return nullptr;
  }
  return it->second.instrumented->wrappedHandler();
}

// -- Metrics --

StageMetricsSnapshot MediaPipeline::getMetrics(const std::string& name) const
{
  auto it = _stages.find(name);
  if (it == _stages.end())
  {
    return StageMetricsSnapshot{};
  }
  return it->second.instrumented->snapshot();
}

std::vector<StageMetricsSnapshot> MediaPipeline::allMetrics() const
{
  std::vector<StageMetricsSnapshot> result;
  result.reserve(_stages.size());
  for (const auto& [name, entry] : _stages)
  {
    result.push_back(entry.instrumented->snapshot());
  }
  return result;
}

std::size_t MediaPipeline::stageCount() const noexcept
{
  return _stages.size();
}

// -- Codec hot-swap --

SwapResult MediaPipeline::swapCodec(const std::string& stageName,
                                    std::unique_ptr<ICodec> newDecoder,
                                    std::unique_ptr<ICodec> newEncoder)
{
  auto current = _state.load(std::memory_order_acquire);
  if (current != LifecycleState::Running)
  {
    return {false, "Pipeline must be in Running state to swap codecs",
            std::chrono::microseconds{0}};
  }

  auto it = _stages.find(stageName);
  if (it == _stages.end())
  {
    return {false, "Stage not found: " + stageName,
            std::chrono::microseconds{0}};
  }

  auto handler = it->second.instrumented->wrappedHandler();
  auto* transcoder = dynamic_cast<TranscodingHandler*>(handler.get());
  if (!transcoder)
  {
    return {false, "Stage is not a TranscodingHandler: " + stageName,
            std::chrono::microseconds{0}};
  }

  // Wait for in-flight frames to complete without changing state.
  // The pipeline stays Running — we just need a quiescent moment.
  auto drainStart = std::chrono::steady_clock::now();
  constexpr int kMaxWaitMs = 30000;
  int waitMs = 0;

  while (waitMs < kMaxWaitMs)
  {
    if (_inFlightCount.load(std::memory_order_acquire) == 0)
    {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    ++waitMs;
  }

  auto drainEnd = std::chrono::steady_clock::now();
  auto drainDuration = std::chrono::duration_cast<std::chrono::microseconds>(
    drainEnd - drainStart);

  if (_inFlightCount.load(std::memory_order_acquire) != 0)
  {
    return {false, "Timed out waiting for in-flight frames", drainDuration};
  }

  // Swap codecs on the TranscodingHandler.
  transcoder->swapCodecs(std::move(newDecoder), std::move(newEncoder));

  return {true, "Codec swapped successfully", drainDuration};
}

// -- Topology validation --

bool MediaPipeline::validateAcyclic() const
{
  // Kahn's algorithm for topological sort / cycle detection.
  std::unordered_map<std::string, std::uint32_t> inDegree;
  for (const auto& [name, entry] : _stages)
  {
    if (inDegree.find(name) == inDegree.end())
    {
      inDegree[name] = 0;
    }
    for (const auto& dest : entry.outEdges)
    {
      ++inDegree[dest];
    }
  }

  std::queue<std::string> q;
  for (const auto& [name, degree] : inDegree)
  {
    if (degree == 0)
    {
      q.push(name);
    }
  }

  std::uint32_t visited = 0;
  while (!q.empty())
  {
    auto current = q.front();
    q.pop();
    ++visited;

    auto it = _stages.find(current);
    if (it != _stages.end())
    {
      for (const auto& dest : it->second.outEdges)
      {
        if (--inDegree[dest] == 0)
        {
          q.push(dest);
        }
      }
    }
  }

  return visited == _stages.size();
}

// -- Processing --

void MediaPipeline::incoming(std::shared_ptr<MediaBuffer> buffer)
{
  if (_state.load(std::memory_order_acquire) != LifecycleState::Running)
  {
    return;
  }

  if (_entryStage.empty())
  {
    return;
  }

  auto it = _stages.find(_entryStage);
  if (it == _stages.end())
  {
    return;
  }

  _inFlightCount.fetch_add(1, std::memory_order_relaxed);
  try
  {
    it->second.instrumented->incoming(std::move(buffer));
  }
  catch (...)
  {
    _inFlightCount.fetch_sub(1, std::memory_order_relaxed);
    throw;
  }
  _inFlightCount.fetch_sub(1, std::memory_order_relaxed);
}

void MediaPipeline::outgoing(std::shared_ptr<MediaBuffer> buffer)
{
  if (_state.load(std::memory_order_acquire) != LifecycleState::Running)
  {
    return;
  }

  if (_exitStage.empty())
  {
    return;
  }

  auto it = _stages.find(_exitStage);
  if (it == _stages.end())
  {
    return;
  }

  _inFlightCount.fetch_add(1, std::memory_order_relaxed);
  try
  {
    it->second.instrumented->outgoing(std::move(buffer));
  }
  catch (...)
  {
    _inFlightCount.fetch_sub(1, std::memory_order_relaxed);
    throw;
  }
  _inFlightCount.fetch_sub(1, std::memory_order_relaxed);
}

// -- ILifecycleManaged --

LifecycleResult MediaPipeline::start()
{
  auto current = _state.load(std::memory_order_acquire);

  if (current == LifecycleState::Running)
  {
    return LifecycleResult(true, LifecycleState::Running, "Already running");
  }

  if (current != LifecycleState::Created && current != LifecycleState::Reset)
  {
    return LifecycleResult(false, current,
      "Can only start from Created or Reset state");
  }

  // Validate graph is acyclic.
  if (!_stages.empty() && !validateAcyclic())
  {
    return LifecycleResult(false, current,
      "Pipeline graph contains a cycle");
  }

  // Identify entry and exit stages.
  _entryStage = findEntryStage();
  _exitStage = findExitStage();

  _state.store(LifecycleState::Running, std::memory_order_release);
  return LifecycleResult(true, LifecycleState::Running, "Pipeline started");
}

LifecycleResult MediaPipeline::drain(std::uint32_t timeoutMs)
{
  auto current = _state.load(std::memory_order_acquire);

  if (current != LifecycleState::Running)
  {
    return LifecycleResult(false, current,
      "Can only drain from Running state");
  }

  _state.store(LifecycleState::Draining, std::memory_order_release);

  std::uint32_t inFlightAtStart = _inFlightCount.load(std::memory_order_relaxed);
  int waitMs = 0;
  int maxWaitMs = (timeoutMs == 0)
    ? 3600000
    : static_cast<int>(timeoutMs);

  while (waitMs < maxWaitMs)
  {
    if (_inFlightCount.load(std::memory_order_acquire) == 0)
    {
      DrainStats stats(inFlightAtStart, 0, 0, inFlightAtStart);
      return LifecycleResult(true, LifecycleState::Draining,
        "Drain completed", stats);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    ++waitMs;
  }

  auto remaining = _inFlightCount.load(std::memory_order_acquire);
  DrainStats stats(inFlightAtStart, remaining, 0,
    inFlightAtStart - remaining);
  return LifecycleResult(false, LifecycleState::Draining,
    "Drain timed out", stats);
}

LifecycleResult MediaPipeline::stop()
{
  auto current = _state.load(std::memory_order_acquire);

  if (current == LifecycleState::Stopped)
  {
    return LifecycleResult(true, LifecycleState::Stopped, "Already stopped");
  }

  if (current != LifecycleState::Running &&
      current != LifecycleState::Draining)
  {
    return LifecycleResult(false, current,
      "Can only stop from Running or Draining state");
  }

  if (current == LifecycleState::Running)
  {
    auto drainResult = drain();
    if (!drainResult.success)
    {
      return LifecycleResult(false, LifecycleState::Draining,
        "Drain failed during stop: " + drainResult.message);
    }
  }

  _state.store(LifecycleState::Stopped, std::memory_order_release);
  return LifecycleResult(true, LifecycleState::Stopped, "Pipeline stopped");
}

LifecycleResult MediaPipeline::reset()
{
  auto current = _state.load(std::memory_order_acquire);

  if (current != LifecycleState::Stopped)
  {
    return LifecycleResult(false, current,
      "Can only reset from Stopped state");
  }

  _stages.clear();
  _entryStage.clear();
  _exitStage.clear();
  _inFlightCount.store(0, std::memory_order_relaxed);

  _state.store(LifecycleState::Reset, std::memory_order_release);
  return LifecycleResult(true, LifecycleState::Reset, "Pipeline reset");
}

LifecycleState MediaPipeline::getState() const
{
  return _state.load(std::memory_order_acquire);
}

std::uint32_t MediaPipeline::getInFlightCount() const
{
  return _inFlightCount.load(std::memory_order_acquire);
}

// -- Private helpers --

bool MediaPipeline::isGraphMutable() const
{
  auto s = _state.load(std::memory_order_acquire);
  return s == LifecycleState::Created ||
         s == LifecycleState::Stopped ||
         s == LifecycleState::Reset;
}

bool MediaPipeline::wouldCreateCycle(const std::string& source,
                                     const std::string& dest) const
{
  // BFS from dest to check if source is reachable. If so, adding
  // source→dest would create a cycle.
  std::unordered_set<std::string> visited;
  std::queue<std::string> q;
  q.push(dest);
  visited.insert(dest);

  while (!q.empty())
  {
    auto current = q.front();
    q.pop();

    if (current == source)
    {
      return true; // Cycle would be created.
    }

    auto it = _stages.find(current);
    if (it != _stages.end())
    {
      for (const auto& next : it->second.outEdges)
      {
        if (visited.find(next) == visited.end())
        {
          visited.insert(next);
          q.push(next);
        }
      }
    }
  }
  return false;
}

void MediaPipeline::wireStageConnections(const std::string& sourceName)
{
  auto srcIt = _stages.find(sourceName);
  if (srcIt == _stages.end())
  {
    return;
  }

  auto& entry = srcIt->second;

  if (entry.outEdges.empty())
  {
    // No downstream — disconnect.
    entry.instrumented->addToChain(nullptr);
  }
  else if (entry.outEdges.size() == 1)
  {
    // Single downstream — direct chain.
    auto dstIt = _stages.find(entry.outEdges[0]);
    if (dstIt != _stages.end())
    {
      entry.instrumented->addToChain(dstIt->second.instrumented);
    }
  }
  else
  {
    // Multiple downstreams — fan-out handler.
    auto fanout = std::make_shared<FanOutHandler>();
    for (const auto& destName : entry.outEdges)
    {
      auto dstIt = _stages.find(destName);
      if (dstIt != _stages.end())
      {
        fanout->addDownstream(dstIt->second.instrumented);
      }
    }
    entry.instrumented->addToChain(fanout);
  }
}

bool MediaPipeline::negotiateFormats(const std::string& sourceName,
                                     const std::string& destName)
{
  auto srcIt = _stages.find(sourceName);
  auto dstIt = _stages.find(destName);
  if (srcIt == _stages.end() || dstIt == _stages.end())
  {
    return false;
  }

  const auto& srcOut = srcIt->second.outputFormat;
  const auto& dstIn = dstIt->second.inputFormat;

  // Determine what conversions are needed.
  bool needFmtConvert = srcOut.sampleFormat.has_value() &&
                        dstIn.sampleFormat.has_value() &&
                        *srcOut.sampleFormat != *dstIn.sampleFormat;
  bool needResample = srcOut.sampleRate.has_value() &&
                      dstIn.sampleRate.has_value() &&
                      *srcOut.sampleRate != *dstIn.sampleRate;
  bool needChannelMix = srcOut.channels.has_value() &&
                        dstIn.channels.has_value() &&
                        *srcOut.channels != *dstIn.channels;

  if (!needFmtConvert && !needResample && !needChannelMix)
  {
    return false;
  }

  // Build chain: source → [fmt] → [resampler] → [chmix] → dest.
  // Track the current tail of the inserted chain.
  std::string currentSource = sourceName;

  auto insertStage = [&](const std::string& name,
                         std::shared_ptr<IMediaHandler> handler,
                         const StageFormat& inFmt,
                         const StageFormat& outFmt)
  {
    auto inst = std::make_shared<InstrumentedStage>(name, handler);
    StageEntry entry;
    entry.instrumented = inst;
    entry.inputFormat = inFmt;
    entry.outputFormat = outFmt;
    entry.autoInserted = true;

    auto curIt = _stages.find(currentSource);
    curIt->second.outEdges.push_back(name);
    entry.inEdges.push_back(currentSource);
    _stages.emplace(name, std::move(entry));
    wireStageConnections(currentSource);

    currentSource = name;
  };

  // 1. Sample format conversion (before resampler — resampler expects S16).
  if (needFmtConvert)
  {
    StageFormat inFmt;
    inFmt.sampleFormat = srcOut.sampleFormat;
    StageFormat outFmt;
    outFmt.sampleFormat = dstIn.sampleFormat;

    insertStage(
      sourceName + "_to_" + destName + "_fmt",
      std::make_shared<FormatConverterStageHandler>(
        *srcOut.sampleFormat, *dstIn.sampleFormat),
      inFmt, outFmt);
  }

  // 2. Sample rate conversion.
  if (needResample)
  {
    std::uint32_t channels = 1;
    if (srcOut.channels.has_value())
    {
      channels = *srcOut.channels;
    }
    else if (dstIn.channels.has_value())
    {
      channels = *dstIn.channels;
    }

    StageFormat inFmt;
    inFmt.sampleRate = srcOut.sampleRate;
    StageFormat outFmt;
    outFmt.sampleRate = dstIn.sampleRate;

    insertStage(
      sourceName + "_to_" + destName + "_resampler",
      std::make_shared<ResamplerStageHandler>(
        *srcOut.sampleRate, *dstIn.sampleRate, channels),
      inFmt, outFmt);
  }

  // 3. Channel conversion (after resampling).
  if (needChannelMix)
  {
    StageFormat inFmt;
    inFmt.channels = srcOut.channels;
    StageFormat outFmt;
    outFmt.channels = dstIn.channels;

    insertStage(
      sourceName + "_to_" + destName + "_chmix",
      std::make_shared<ChannelMixerStageHandler>(
        *srcOut.channels, *dstIn.channels),
      inFmt, outFmt);
  }

  // Final edge: last auto-inserted stage → dest.
  auto curIt = _stages.find(currentSource);
  curIt->second.outEdges.push_back(destName);
  dstIt = _stages.find(destName);
  dstIt->second.inEdges.push_back(currentSource);
  wireStageConnections(currentSource);

  return true;
}

std::string MediaPipeline::findEntryStage() const
{
  for (const auto& [name, entry] : _stages)
  {
    if (entry.inEdges.empty())
    {
      return name;
    }
  }
  if (!_stages.empty())
  {
    return _stages.begin()->first;
  }
  return {};
}

std::string MediaPipeline::findExitStage() const
{
  for (const auto& [name, entry] : _stages)
  {
    if (entry.outEdges.empty())
    {
      return name;
    }
  }
  if (!_stages.empty())
  {
    return _stages.begin()->first;
  }
  return {};
}

} // namespace codecs
} // namespace iora
