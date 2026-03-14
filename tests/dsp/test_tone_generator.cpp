#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "iora/codecs/dsp/tone_generator.hpp"

#include <cmath>
#include <cstring>
#include <string>
#include <vector>

using namespace iora::codecs;

namespace {

std::vector<std::int16_t> readS16(const MediaBuffer& buf)
{
  std::size_t count = buf.size() / sizeof(std::int16_t);
  std::vector<std::int16_t> out(count);
  std::memcpy(out.data(), buf.data(), count * sizeof(std::int16_t));
  return out;
}

/// Inline Goertzel magnitude helper for frequency verification.
/// Returns the magnitude^2 at the target frequency.
double goertzelMagnitude(const std::int16_t* samples, std::size_t N,
                         double targetFreq, double sampleRate)
{
  double k = targetFreq * static_cast<double>(N) / sampleRate;
  static constexpr double kPi = 3.14159265358979323846;
  double coeff = 2.0 * std::cos(2.0 * kPi * k / static_cast<double>(N));
  double q1 = 0.0;
  double q2 = 0.0;
  for (std::size_t i = 0; i < N; ++i)
  {
    double q0 = coeff * q1 - q2 + static_cast<double>(samples[i]);
    q2 = q1;
    q1 = q0;
  }
  return q1 * q1 + q2 * q2 - coeff * q1 * q2;
}

} // namespace

TEST_CASE("ToneGenerator — generate digit '5' buffer size", "[dsp][tone_gen]")
{
  ToneGenerator gen(8000);
  auto buf = gen.generate('5', 100);
  REQUIRE(buf != nullptr);
  // 8000 * 100 / 1000 = 800 samples = 1600 bytes
  CHECK(buf->size() == 800 * sizeof(std::int16_t));
}

TEST_CASE("ToneGenerator — tone is not silence", "[dsp][tone_gen]")
{
  ToneGenerator gen(8000);
  auto buf = gen.generate('5', 100);
  REQUIRE(buf != nullptr);
  auto samples = readS16(*buf);
  bool hasNonZero = false;
  for (auto s : samples)
  {
    if (s != 0)
    {
      hasNonZero = true;
      break;
    }
  }
  CHECK(hasNonZero);
}

TEST_CASE("ToneGenerator — amplitude scaling", "[dsp][tone_gen]")
{
  ToneGenerator gen(8000);
  float amplitude = 0.3f;
  auto buf = gen.generate('5', 100, amplitude);
  REQUIRE(buf != nullptr);
  auto samples = readS16(*buf);

  std::int16_t maxAbs = 0;
  for (auto s : samples)
  {
    std::int16_t absVal = (s >= 0) ? s : static_cast<std::int16_t>(-s);
    if (absVal > maxAbs)
    {
      maxAbs = absVal;
    }
  }
  // Max should be <= amplitude * 32767 (with small tolerance for rounding)
  CHECK(maxAbs <= static_cast<std::int16_t>(amplitude * 32767 + 2));
}

TEST_CASE("ToneGenerator — all 16 DTMF digits produce valid buffers", "[dsp][tone_gen]")
{
  ToneGenerator gen(8000);
  std::string digits = "0123456789*#ABCD";
  for (char d : digits)
  {
    auto buf = gen.generate(d, 100);
    REQUIRE(buf != nullptr);
    CHECK(buf->size() > 0);
  }
}

TEST_CASE("ToneGenerator — invalid digit returns nullptr", "[dsp][tone_gen]")
{
  ToneGenerator gen(8000);
  CHECK(gen.generate('X', 100) == nullptr);
  CHECK(gen.generate('E', 100) == nullptr);
  CHECK(gen.generate(' ', 100) == nullptr);
}

TEST_CASE("ToneGenerator — generate silence", "[dsp][tone_gen]")
{
  ToneGenerator gen(8000);
  auto buf = gen.generateSilence(100);
  REQUIRE(buf != nullptr);
  auto samples = readS16(*buf);
  for (auto s : samples)
  {
    CHECK(s == 0);
  }
}

TEST_CASE("ToneGenerator — generateSequence correct buffer count", "[dsp][tone_gen]")
{
  ToneGenerator gen(8000);

  SECTION("3 digits = 3 tones + 2 gaps = 5 buffers")
  {
    auto seq = gen.generateSequence("123", 100, 50);
    CHECK(seq.size() == 5);
  }

  SECTION("1 digit = 1 tone, no gaps")
  {
    auto seq = gen.generateSequence("5", 100, 50);
    CHECK(seq.size() == 1);
  }

  SECTION("empty string = no buffers")
  {
    auto seq = gen.generateSequence("", 100, 50);
    CHECK(seq.empty());
  }

  SECTION("0ms gap = tones only")
  {
    auto seq = gen.generateSequence("12", 100, 0);
    CHECK(seq.size() == 2);
  }
}

TEST_CASE("ToneGenerator — frequency verification with Goertzel", "[dsp][tone_gen]")
{
  ToneGenerator gen(8000);

  // Test digit '5' = (770, 1336)
  auto buf = gen.generate('5', 100, 0.5f);
  REQUIRE(buf != nullptr);
  auto samples = readS16(*buf);
  std::size_t N = samples.size();

  // Expected frequencies should have high magnitude
  double mag770 = goertzelMagnitude(samples.data(), N, 770.0, 8000.0);
  double mag1336 = goertzelMagnitude(samples.data(), N, 1336.0, 8000.0);

  // Unexpected frequencies should have low magnitude
  double mag697 = goertzelMagnitude(samples.data(), N, 697.0, 8000.0);
  double mag1209 = goertzelMagnitude(samples.data(), N, 1209.0, 8000.0);

  CHECK(mag770 > mag697 * 10.0);   // Expected low freq >> unexpected
  CHECK(mag1336 > mag1209 * 10.0); // Expected high freq >> unexpected
}

TEST_CASE("ToneGenerator — all digits have correct frequency pairs", "[dsp][tone_gen]")
{
  ToneGenerator gen(8000);
  std::string digits = "0123456789*#ABCD";

  for (char d : digits)
  {
    auto [lowFreq, highFreq] = ToneGenerator::dtmfFrequencies(d);
    REQUIRE(lowFreq > 0);
    REQUIRE(highFreq > 0);

    auto buf = gen.generate(d, 100, 0.5f);
    REQUIRE(buf != nullptr);
    auto samples = readS16(*buf);
    std::size_t N = samples.size();

    double magLow = goertzelMagnitude(samples.data(), N, lowFreq, 8000.0);
    double magHigh = goertzelMagnitude(samples.data(), N, highFreq, 8000.0);

    // Both expected frequencies should have significant energy
    CHECK(magLow > 1e6);
    CHECK(magHigh > 1e6);
  }
}

TEST_CASE("ToneGenerator — different sample rates", "[dsp][tone_gen]")
{
  SECTION("8000 Hz")
  {
    ToneGenerator gen(8000);
    auto buf = gen.generate('1', 100);
    REQUIRE(buf != nullptr);
    CHECK(buf->size() == 800 * sizeof(std::int16_t));
  }

  SECTION("16000 Hz")
  {
    ToneGenerator gen(16000);
    auto buf = gen.generate('1', 100);
    REQUIRE(buf != nullptr);
    CHECK(buf->size() == 1600 * sizeof(std::int16_t));
  }

  SECTION("48000 Hz")
  {
    ToneGenerator gen(48000);
    auto buf = gen.generate('1', 100);
    REQUIRE(buf != nullptr);
    CHECK(buf->size() == 4800 * sizeof(std::int16_t));
  }
}

TEST_CASE("ToneGenerator — zero duration returns nullptr", "[dsp][tone_gen]")
{
  ToneGenerator gen(8000);
  CHECK(gen.generate('5', 0) == nullptr);
  CHECK(gen.generateSilence(0) == nullptr);
}
