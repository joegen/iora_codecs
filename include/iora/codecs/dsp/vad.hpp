#pragma once

/// @file vad.hpp
/// @brief Energy-based voice activity detection and pipeline handler.

#include "iora/codecs/core/media_buffer.hpp"
#include "iora/codecs/pipeline/i_media_handler.hpp"

#include <cmath>
#include <cstdint>
#include <memory>

namespace iora {
namespace codecs {

/// Configuration for energy-based VAD.
struct VadParams
{
  float silenceThresholdRms = 100.0f;
  std::uint32_t hangoverFrames = 10;
  std::uint32_t sampleRate = 16000;
};

/// Result of processing one audio frame through VAD.
struct VadResult
{
  bool isActive = false;
  float rmsEnergy = 0.0f;
  float peakAmplitude = 0.0f;
};

/// Energy-based voice activity detection for S16 PCM audio.
///
/// Classifies each frame as speech or silence based on RMS energy.
/// Uses hangover logic to prevent choppy transitions during brief
/// intra-word pauses.
class Vad
{
public:
  explicit Vad(VadParams params = {})
    : _params(params)
    , _consecutiveSilenceFrames(0)
    , _wasActive(false)
  {
  }

  /// Analyze one frame of S16 PCM samples.
  VadResult process(const std::int16_t* samples, std::size_t sampleCount)
  {
    VadResult result;

    if (sampleCount == 0 || samples == nullptr)
    {
      updateState(false);
      result.isActive = _wasActive;
      return result;
    }

    // Compute RMS energy using double accumulator.
    double sumSquares = 0.0;
    std::int32_t peak = 0;
    for (std::size_t i = 0; i < sampleCount; ++i)
    {
      double s = static_cast<double>(samples[i]);
      sumSquares += s * s;
      std::int32_t absVal = samples[i] >= 0
        ? static_cast<std::int32_t>(samples[i])
        : -static_cast<std::int32_t>(samples[i]);
      if (absVal > peak)
      {
        peak = absVal;
      }
    }

    result.rmsEnergy = static_cast<float>(
      std::sqrt(sumSquares / static_cast<double>(sampleCount)));
    result.peakAmplitude = static_cast<float>(peak);

    bool frameAboveThreshold = result.rmsEnergy >= _params.silenceThresholdRms;
    updateState(frameAboveThreshold);
    result.isActive = _wasActive;

    return result;
  }

  /// Convenience overload for MediaBuffer.
  /// Derives sample count from buffer.size() / sizeof(int16_t).
  /// Returns inactive result for odd-byte-count buffers.
  VadResult process(const MediaBuffer& buffer)
  {
    if (buffer.size() % sizeof(std::int16_t) != 0)
    {
      return {};
    }
    auto* samples = reinterpret_cast<const std::int16_t*>(buffer.data());
    std::size_t sampleCount = buffer.size() / sizeof(std::int16_t);
    return process(samples, sampleCount);
  }

  void reset()
  {
    _consecutiveSilenceFrames = 0;
    _wasActive = false;
  }

  const VadParams& params() const noexcept { return _params; }

private:
  void updateState(bool frameAboveThreshold)
  {
    if (frameAboveThreshold)
    {
      _consecutiveSilenceFrames = 0;
      _wasActive = true;
    }
    else
    {
      if (_consecutiveSilenceFrames < UINT32_MAX)
      {
        ++_consecutiveSilenceFrames;
      }
      if (_consecutiveSilenceFrames > _params.hangoverFrames)
      {
        _wasActive = false;
      }
      // Else: within hangover — stay active.
    }
  }

  VadParams _params;
  std::uint32_t _consecutiveSilenceFrames;
  bool _wasActive;
};

/// VAD operating mode for the pipeline handler.
enum class VadMode
{
  DROP_SILENT,  ///< Discard silent frames entirely.
  MARK_ONLY    ///< Forward all frames; set marker on speech onset.
};

/// IMediaHandler that runs VAD on audio in both directions.
///
/// In DROP_SILENT mode, silent frames are discarded.
/// In MARK_ONLY mode, all frames are forwarded but the RTP marker bit
/// is set on the first buffer after a silence→speech transition
/// (RFC 3550 §5.1 marker semantics for audio).
class VadHandler : public IMediaHandler
{
public:
  explicit VadHandler(VadParams params = {}, VadMode mode = VadMode::DROP_SILENT)
    : _vadIncoming(params)
    , _vadOutgoing(params)
    , _mode(mode)
    , _prevActiveIncoming(false)
    , _prevActiveOutgoing(false)
  {
  }

  void incoming(std::shared_ptr<MediaBuffer> buffer) override
  {
    if (!buffer)
    {
      forwardIncoming(std::move(buffer));
      return;
    }

    auto result = _vadIncoming.process(*buffer);

    if (_mode == VadMode::DROP_SILENT)
    {
      if (result.isActive)
      {
        if (!_prevActiveIncoming)
        {
          buffer->setMarker(true);
        }
        _prevActiveIncoming = true;
        forwardIncoming(std::move(buffer));
      }
      else
      {
        _prevActiveIncoming = false;
        // Drop the buffer.
      }
    }
    else // MARK_ONLY
    {
      if (result.isActive && !_prevActiveIncoming)
      {
        buffer->setMarker(true);
      }
      _prevActiveIncoming = result.isActive;
      forwardIncoming(std::move(buffer));
    }
  }

  void outgoing(std::shared_ptr<MediaBuffer> buffer) override
  {
    if (!buffer)
    {
      forwardOutgoing(std::move(buffer));
      return;
    }

    auto result = _vadOutgoing.process(*buffer);

    if (_mode == VadMode::DROP_SILENT)
    {
      if (result.isActive)
      {
        if (!_prevActiveOutgoing)
        {
          buffer->setMarker(true);
        }
        _prevActiveOutgoing = true;
        forwardOutgoing(std::move(buffer));
      }
      else
      {
        _prevActiveOutgoing = false;
      }
    }
    else
    {
      if (result.isActive && !_prevActiveOutgoing)
      {
        buffer->setMarker(true);
      }
      _prevActiveOutgoing = result.isActive;
      forwardOutgoing(std::move(buffer));
    }
  }

  Vad& vadIncoming() noexcept { return _vadIncoming; }
  Vad& vadOutgoing() noexcept { return _vadOutgoing; }
  VadMode mode() const noexcept { return _mode; }

private:
  Vad _vadIncoming;
  Vad _vadOutgoing;
  VadMode _mode;
  bool _prevActiveIncoming;
  bool _prevActiveOutgoing;
};

} // namespace codecs
} // namespace iora
