#include "iora/codecs/pipeline/stage_metrics.hpp"

#include <stdexcept>
#include <utility>

namespace iora {
namespace codecs {

// -- StageMetrics --

StageMetrics::StageMetrics(std::string name)
  : _stageName(std::move(name))
{
}

void StageMetrics::recordIncoming(std::chrono::microseconds latency) noexcept
{
  _framesIn.fetch_add(1, std::memory_order_relaxed);
  updateLatency(latency);
}

void StageMetrics::recordOutgoing(std::chrono::microseconds latency) noexcept
{
  _framesOut.fetch_add(1, std::memory_order_relaxed);
  updateLatency(latency);
}

void StageMetrics::recordDrop() noexcept
{
  _framesDropped.fetch_add(1, std::memory_order_relaxed);
}

void StageMetrics::recordError() noexcept
{
  _errorCount.fetch_add(1, std::memory_order_relaxed);
}

StageMetricsSnapshot StageMetrics::snapshot() const
{
  StageMetricsSnapshot s;
  s.stageName = _stageName;
  s.framesIn = _framesIn.load(std::memory_order_relaxed);
  s.framesOut = _framesOut.load(std::memory_order_relaxed);
  s.framesDropped = _framesDropped.load(std::memory_order_relaxed);
  s.errorCount = _errorCount.load(std::memory_order_relaxed);
  s.totalLatencyUs = std::chrono::microseconds(
    _totalLatencyUs.load(std::memory_order_relaxed));
  s.maxLatencyUs = std::chrono::microseconds(
    _maxLatencyUs.load(std::memory_order_relaxed));
  s.minLatencyUs = std::chrono::microseconds(
    _minLatencyUs.load(std::memory_order_relaxed));
  return s;
}

const std::string& StageMetrics::stageName() const noexcept
{
  return _stageName;
}

void StageMetrics::updateLatency(std::chrono::microseconds latency) noexcept
{
  auto us = latency.count();
  _totalLatencyUs.fetch_add(us, std::memory_order_relaxed);

  // CAS loop for max.
  auto current = _maxLatencyUs.load(std::memory_order_relaxed);
  while (us > current)
  {
    if (_maxLatencyUs.compare_exchange_weak(
          current, us,
          std::memory_order_relaxed, std::memory_order_relaxed))
    {
      break;
    }
  }

  // CAS loop for min.
  current = _minLatencyUs.load(std::memory_order_relaxed);
  while (us < current)
  {
    if (_minLatencyUs.compare_exchange_weak(
          current, us,
          std::memory_order_relaxed, std::memory_order_relaxed))
    {
      break;
    }
  }
}

// -- InstrumentedStage --

InstrumentedStage::InstrumentedStage(std::string name,
                                     std::shared_ptr<IMediaHandler> wrapped)
  : _wrapped(std::move(wrapped))
  , _metrics(std::move(name))
{
  if (!_wrapped)
  {
    throw std::invalid_argument("InstrumentedStage: wrapped handler must not be null");
  }
}

void InstrumentedStage::incoming(std::shared_ptr<MediaBuffer> buffer)
{
  auto start = std::chrono::steady_clock::now();
  try
  {
    _wrapped->incoming(std::move(buffer));
  }
  catch (...)
  {
    _metrics.recordError();
    throw;
  }
  auto end = std::chrono::steady_clock::now();
  auto latency = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
  _metrics.recordIncoming(latency);

  if (!_hasDownstream)
  {
    _metrics.recordDrop();
  }
}

void InstrumentedStage::outgoing(std::shared_ptr<MediaBuffer> buffer)
{
  auto start = std::chrono::steady_clock::now();
  try
  {
    _wrapped->outgoing(std::move(buffer));
  }
  catch (...)
  {
    _metrics.recordError();
    throw;
  }
  auto end = std::chrono::steady_clock::now();
  auto latency = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
  _metrics.recordOutgoing(latency);
}

void InstrumentedStage::addToChain(std::shared_ptr<IMediaHandler> next)
{
  _hasDownstream = (next != nullptr);
  _wrapped->addToChain(std::move(next));
}

IMediaHandler& InstrumentedStage::chainWith(std::shared_ptr<IMediaHandler> handler)
{
  _hasDownstream = (handler != nullptr);
  return _wrapped->chainWith(std::move(handler));
}

StageMetricsSnapshot InstrumentedStage::snapshot() const
{
  return _metrics.snapshot();
}

const std::string& InstrumentedStage::stageName() const noexcept
{
  return _metrics.stageName();
}

std::shared_ptr<IMediaHandler> InstrumentedStage::wrappedHandler()
{
  return _wrapped;
}

const std::shared_ptr<IMediaHandler>& InstrumentedStage::wrappedHandler() const
{
  return _wrapped;
}

bool InstrumentedStage::hasDownstream() const noexcept
{
  return _hasDownstream;
}

} // namespace codecs
} // namespace iora
