#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "iora/codecs/core/media_clock.hpp"

#include <chrono>
#include <thread>

using namespace iora::codecs;

TEST_CASE("MediaClock: construction and clockRate", "[core][clock]")
{
  MediaClock clock(8000);
  CHECK(clock.clockRate() == 8000);
}

TEST_CASE("MediaClock: now() returns valid timestamp", "[core][clock]")
{
  MediaClock clock(48000);
  auto ts = clock.now();
  // Just check it doesn't crash and returns a value
  (void)ts;
}

TEST_CASE("MediaClock: now() is non-decreasing", "[core][clock]")
{
  MediaClock clock(8000);
  auto ts1 = clock.now();
  auto ts2 = clock.now();
  // ts2 >= ts1 (uint32_t wrapping means we check difference is small)
  auto diff = static_cast<std::int32_t>(ts2 - ts1);
  CHECK(diff >= 0);
}

TEST_CASE("MediaClock: elapsedSamples for known duration", "[core][clock]")
{
  MediaClock clock(8000);
  auto start = std::chrono::steady_clock::now();
  auto end = start + std::chrono::milliseconds(20);

  auto samples = clock.elapsedSamples(start, end);
  CHECK(samples == 160); // 20ms * 8000Hz = 160 samples
}

TEST_CASE("MediaClock: elapsedSamples at 48kHz", "[core][clock]")
{
  MediaClock clock(48000);
  auto start = std::chrono::steady_clock::now();
  auto end = start + std::chrono::milliseconds(20);

  auto samples = clock.elapsedSamples(start, end);
  CHECK(samples == 960); // 20ms * 48000Hz = 960 samples
}

TEST_CASE("MediaClock: toMediaTimestamp/toWallClock round-trip", "[core][clock]")
{
  MediaClock clock(8000);
  auto wallTime = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);

  auto mediaTs = clock.toMediaTimestamp(wallTime);
  auto recoveredWall = clock.toWallClock(mediaTs);

  // Should be within one sample period (125us at 8kHz)
  auto error = std::chrono::duration_cast<std::chrono::microseconds>(
    recoveredWall - wallTime);
  CHECK(std::abs(error.count()) <= 125);
}

TEST_CASE("MediaClock: toMediaTimestamp/toWallClock round-trip at 48kHz", "[core][clock]")
{
  MediaClock clock(48000);
  auto wallTime = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);

  auto mediaTs = clock.toMediaTimestamp(wallTime);
  auto recoveredWall = clock.toWallClock(mediaTs);

  // Within one sample period (~20.8us at 48kHz)
  auto error = std::chrono::duration_cast<std::chrono::microseconds>(
    recoveredWall - wallTime);
  CHECK(std::abs(error.count()) <= 21);
}

TEST_CASE("MediaClock: base timestamp offset", "[core][clock]")
{
  MediaClock clock(8000, 1000);
  auto ts = clock.now();
  // Should be >= base timestamp
  CHECK(ts >= 1000);
}

TEST_CASE("MediaClock: 48kHz produces 6x samples vs 8kHz", "[core][clock]")
{
  MediaClock clock8k(8000);
  MediaClock clock48k(48000);

  auto start = std::chrono::steady_clock::now();
  auto end = start + std::chrono::seconds(1);

  auto samples8k = clock8k.elapsedSamples(start, end);
  auto samples48k = clock48k.elapsedSamples(start, end);

  CHECK(samples8k == 8000);
  CHECK(samples48k == 48000);
  CHECK(samples48k == samples8k * 6);
}

TEST_CASE("MediaClock: driftPpm between same-rate clocks", "[core][clock]")
{
  MediaClock a(8000);
  MediaClock b(8000);

  // Two clocks created at nearly the same time with the same rate
  // should show near-zero drift
  auto drift = a.driftPpm(b);
  CHECK_THAT(drift, Catch::Matchers::WithinAbs(0.0, 1000.0));
}

TEST_CASE("MediaClock: video clock at 90kHz", "[core][clock]")
{
  MediaClock clock(90000);
  auto start = std::chrono::steady_clock::now();
  auto end = start + std::chrono::milliseconds(33); // ~1 frame at 30fps

  auto samples = clock.elapsedSamples(start, end);
  CHECK(samples == 2970); // 33ms * 90000Hz = 2970
}
