#pragma once

/// @file gain.hpp
/// @brief Volume adjustment DSP utility and pipeline handler.

#include "iora/codecs/core/media_buffer.hpp"
#include "iora/codecs/pipeline/i_media_handler.hpp"

#include <cmath>
#include <cstdint>
#include <memory>
#include <stdexcept>

namespace iora {
namespace codecs {

/// Applies linear volume scaling to S16 PCM audio buffers.
///
/// Uses int32_t intermediate arithmetic to prevent overflow. Output
/// samples are clamped to [INT16_MIN, INT16_MAX].
class Gain
{
public:
  explicit Gain(float gainFactor = 1.0f)
    : _gain(gainFactor)
    , _previousGain(gainFactor)
    , _muted(false)
  {
    if (gainFactor < 0.0f || !std::isfinite(gainFactor))
    {
      throw std::invalid_argument("Gain factor must be a finite non-negative value");
    }
  }

  /// Apply gain to raw S16 PCM samples in-place.
  void apply(std::int16_t* samples, std::size_t sampleCount)
  {
    if (samples == nullptr)
    {
      return;
    }
    float g = _muted ? 0.0f : _gain;
    for (std::size_t i = 0; i < sampleCount; ++i)
    {
      float product = g * static_cast<float>(samples[i]);
      if (product > 32767.0f)
      {
        samples[i] = 32767;
      }
      else if (product < -32768.0f)
      {
        samples[i] = -32768;
      }
      else
      {
        samples[i] = static_cast<std::int16_t>(static_cast<std::int32_t>(product));
      }
    }
  }

  /// Apply gain to a MediaBuffer's S16 PCM data in-place.
  /// Rejects odd-byte-count buffers (no-op).
  void apply(MediaBuffer& buffer)
  {
    if (buffer.size() % sizeof(std::int16_t) != 0)
    {
      return;
    }
    auto* samples = reinterpret_cast<std::int16_t*>(buffer.data());
    std::size_t sampleCount = buffer.size() / sizeof(std::int16_t);
    apply(samples, sampleCount);
  }

  void setGain(float factor)
  {
    if (factor < 0.0f || !std::isfinite(factor))
    {
      throw std::invalid_argument("Gain factor must be a finite non-negative value");
    }
    _gain = factor;
    _previousGain = factor;
  }

  float gain() const noexcept { return _gain; }

  float gainDb() const noexcept
  {
    if (_gain <= 0.0f)
    {
      return -std::numeric_limits<float>::infinity();
    }
    return 20.0f * std::log10(_gain);
  }

  void setGainDb(float db)
  {
    setGain(std::pow(10.0f, db / 20.0f));
  }

  void mute()
  {
    if (!_muted)
    {
      _previousGain = _gain;
      _muted = true;
    }
  }

  void unmute()
  {
    if (_muted)
    {
      _muted = false;
      _gain = _previousGain;
    }
  }

  bool isMuted() const noexcept { return _muted; }

private:
  float _gain;
  float _previousGain;
  bool _muted;
};

/// IMediaHandler that applies Gain to buffers in both directions.
class GainHandler : public IMediaHandler
{
public:
  explicit GainHandler(float gainFactor = 1.0f)
    : _gain(gainFactor)
  {
  }

  void incoming(std::shared_ptr<MediaBuffer> buffer) override
  {
    if (buffer)
    {
      _gain.apply(*buffer);
    }
    forwardIncoming(std::move(buffer));
  }

  void outgoing(std::shared_ptr<MediaBuffer> buffer) override
  {
    if (buffer)
    {
      _gain.apply(*buffer);
    }
    forwardOutgoing(std::move(buffer));
  }

  Gain& gain() noexcept { return _gain; }
  const Gain& gain() const noexcept { return _gain; }

private:
  Gain _gain;
};

} // namespace codecs
} // namespace iora
