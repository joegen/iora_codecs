#pragma once

/// @file media_clock.hpp
/// @brief RTP-style media clock for timestamp generation and drift measurement.

#include <cassert>
#include <chrono>
#include <cstdint>

namespace iora {
namespace codecs {

/// Media clock that converts between wall-clock time and RTP timestamps.
///
/// Configured with a clock rate in Hz (e.g., 8000 for G.711, 48000
/// for Opus, 90000 for video). Anchors a steady_clock time point to
/// a base media timestamp at construction, then derives all subsequent
/// timestamps from that anchor.
///
/// Thread-safe: the anchor is set once at construction and never
/// modified, so reads are inherently safe without atomics.
class MediaClock
{
public:
  /// Construct a clock with the given rate in Hz and an optional base timestamp.
  explicit MediaClock(std::uint32_t clockRate, std::uint32_t baseTimestamp = 0) noexcept
    : _clockRate(clockRate)
    , _baseTimestamp(baseTimestamp)
    , _anchor(std::chrono::steady_clock::now())
  {
    assert(clockRate > 0 && "MediaClock: clockRate must be > 0");
  }

  /// The configured clock rate in Hz.
  std::uint32_t clockRate() const noexcept
  {
    return _clockRate;
  }

  /// Current media timestamp (wrapping 32-bit RTP timestamp semantics).
  std::uint32_t now() const noexcept
  {
    return toMediaTimestamp(std::chrono::steady_clock::now());
  }

  /// Convert a wall-clock time point to a media timestamp.
  std::uint32_t toMediaTimestamp(std::chrono::steady_clock::time_point tp) const noexcept
  {
    auto elapsed = tp - _anchor;
    auto samples = std::chrono::duration_cast<std::chrono::duration<std::int64_t, std::micro>>(elapsed).count()
                   * static_cast<std::int64_t>(_clockRate) / 1'000'000;
    return _baseTimestamp + static_cast<std::uint32_t>(samples);
  }

  /// Convert a media timestamp to an approximate wall-clock time.
  std::chrono::steady_clock::time_point toWallClock(std::uint32_t mediaTs) const noexcept
  {
    auto delta = static_cast<std::int32_t>(mediaTs - _baseTimestamp);
    auto us = static_cast<std::int64_t>(delta) * 1'000'000 / static_cast<std::int64_t>(_clockRate);
    return _anchor + std::chrono::microseconds(us);
  }

  /// Number of samples elapsed between two wall-clock time points.
  std::int64_t elapsedSamples(
    std::chrono::steady_clock::time_point start,
    std::chrono::steady_clock::time_point end) const noexcept
  {
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    return us * static_cast<std::int64_t>(_clockRate) / 1'000'000;
  }

  /// Measure clock drift relative to another MediaClock in parts-per-million.
  ///
  /// Both clocks should have been created at approximately the same time.
  /// A positive value means this clock runs faster than @p other.
  /// Requires a measurement window to have elapsed since construction
  /// for meaningful results.
  double driftPpm(const MediaClock& other) const noexcept
  {
    auto tp = std::chrono::steady_clock::now();

    auto thisSamples = toMediaTimestamp(tp) - _baseTimestamp;
    auto otherSamples = other.toMediaTimestamp(tp) - other._baseTimestamp;

    // Normalize to the same clock rate for comparison
    if (otherSamples == 0)
    {
      return 0.0;
    }

    double thisNormalized = static_cast<double>(thisSamples) / static_cast<double>(_clockRate);
    double otherNormalized = static_cast<double>(otherSamples) / static_cast<double>(other._clockRate);

    if (otherNormalized == 0.0)
    {
      return 0.0;
    }

    return (thisNormalized / otherNormalized - 1.0) * 1'000'000.0;
  }

private:
  std::uint32_t _clockRate;
  std::uint32_t _baseTimestamp;
  std::chrono::steady_clock::time_point _anchor;
};

} // namespace codecs
} // namespace iora
