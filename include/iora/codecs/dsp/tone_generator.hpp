#pragma once

/// @file tone_generator.hpp
/// @brief DTMF dual-tone multi-frequency signal synthesis.

#include "iora/codecs/core/media_buffer.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace iora {
namespace codecs {

/// Generates DTMF tones as S16 PCM buffers per ITU-T Q.23.
///
/// Each DTMF digit is the sum of one low-group and one high-group
/// frequency. Uses double-precision phase accumulator for accuracy.
class ToneGenerator
{
public:
  explicit ToneGenerator(std::uint32_t sampleRate = 8000)
    : _sampleRate(sampleRate)
  {
  }

  /// Generate a DTMF tone for the given digit.
  /// @param digit  '0'-'9', '*', '#', 'A'-'D'
  /// @param durationMs  Tone duration in milliseconds
  /// @param amplitude  0.0-1.0 scaling factor (default 0.5 = -6dBFS)
  /// @return MediaBuffer with S16 PCM data, or nullptr for invalid digit/zero duration
  std::shared_ptr<MediaBuffer> generate(char digit, std::uint32_t durationMs,
                                        float amplitude = 0.5f) const
  {
    auto freqs = dtmfFrequencies(digit);
    if (freqs.first == 0 || durationMs == 0)
    {
      return nullptr;
    }

    std::uint32_t sampleCount = static_cast<std::uint32_t>(
      static_cast<std::uint64_t>(_sampleRate) * durationMs / 1000);
    if (sampleCount == 0)
    {
      return nullptr;
    }
    std::size_t bytes = sampleCount * sizeof(std::int16_t);
    auto buf = MediaBuffer::create(bytes);

    auto* samples = reinterpret_cast<std::int16_t*>(buf->data());
    double phaseInc1 = 2.0 * kPi * freqs.first / _sampleRate;
    double phaseInc2 = 2.0 * kPi * freqs.second / _sampleRate;
    double phase1 = 0.0;
    double phase2 = 0.0;

    for (std::uint32_t i = 0; i < sampleCount; ++i)
    {
      double value = amplitude * (std::sin(phase1) + std::sin(phase2)) / 2.0;
      std::int32_t clamped = static_cast<std::int32_t>(value * 32767.0);
      if (clamped > 32767)
      {
        clamped = 32767;
      }
      else if (clamped < -32768)
      {
        clamped = -32768;
      }
      samples[i] = static_cast<std::int16_t>(clamped);
      phase1 += phaseInc1;
      phase2 += phaseInc2;
    }

    buf->setSize(bytes);
    return buf;
  }

  /// Generate a silence buffer.
  /// @return MediaBuffer with all-zero S16 PCM data, or nullptr for zero duration
  std::shared_ptr<MediaBuffer> generateSilence(std::uint32_t durationMs) const
  {
    if (durationMs == 0)
    {
      return nullptr;
    }
    std::uint32_t sampleCount = static_cast<std::uint32_t>(
      static_cast<std::uint64_t>(_sampleRate) * durationMs / 1000);
    if (sampleCount == 0)
    {
      return nullptr;
    }
    std::size_t bytes = sampleCount * sizeof(std::int16_t);
    auto buf = MediaBuffer::create(bytes);
    std::memset(buf->data(), 0, bytes);
    buf->setSize(bytes);
    return buf;
  }

  /// Generate a sequence of DTMF tones with inter-digit gaps.
  /// @return Vector of [tone, gap, tone, gap, ...] buffers. Invalid digits are skipped.
  std::vector<std::shared_ptr<MediaBuffer>> generateSequence(
    const std::string& digits, std::uint32_t toneDurationMs,
    std::uint32_t gapDurationMs, float amplitude = 0.5f) const
  {
    std::vector<std::shared_ptr<MediaBuffer>> result;
    bool first = true;
    for (char digit : digits)
    {
      auto tone = generate(digit, toneDurationMs, amplitude);
      if (!tone)
      {
        continue;  // skip invalid digits
      }
      if (!first && gapDurationMs > 0)
      {
        auto gap = generateSilence(gapDurationMs);
        if (gap)
        {
          result.push_back(std::move(gap));
        }
      }
      result.push_back(std::move(tone));
      first = false;
    }
    return result;
  }

  std::uint32_t sampleRate() const noexcept { return _sampleRate; }

  /// Get DTMF frequency pair for a digit. Returns {0,0} for invalid digits.
  static std::pair<std::uint32_t, std::uint32_t> dtmfFrequencies(char digit)
  {
    // ITU-T Q.23 frequency assignments
    switch (digit)
    {
      case '1': return {697, 1209};
      case '2': return {697, 1336};
      case '3': return {697, 1477};
      case 'A': return {697, 1633};
      case '4': return {770, 1209};
      case '5': return {770, 1336};
      case '6': return {770, 1477};
      case 'B': return {770, 1633};
      case '7': return {852, 1209};
      case '8': return {852, 1336};
      case '9': return {852, 1477};
      case 'C': return {852, 1633};
      case '*': return {941, 1209};
      case '0': return {941, 1336};
      case '#': return {941, 1477};
      case 'D': return {941, 1633};
      default:  return {0, 0};
    }
  }

private:
  static constexpr double kPi = 3.14159265358979323846;
  std::uint32_t _sampleRate;
};

} // namespace codecs
} // namespace iora
