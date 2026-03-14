#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "iora/codecs/format/sample_format.hpp"

#include <cstring>
#include <vector>

using namespace iora::codecs;

TEST_CASE("SampleFormat: bytesPerSample", "[format][sample]")
{
  CHECK(bytesPerSample(SampleFormat::S16) == 2);
  CHECK(bytesPerSample(SampleFormat::S32) == 4);
  CHECK(bytesPerSample(SampleFormat::F32) == 4);
  CHECK(bytesPerSample(SampleFormat::U8) == 1);
  CHECK(bytesPerSample(SampleFormat::Mulaw) == 1);
  CHECK(bytesPerSample(SampleFormat::Alaw) == 1);
}

TEST_CASE("SampleFormat: isFloat", "[format][sample]")
{
  CHECK(isFloat(SampleFormat::F32));
  CHECK_FALSE(isFloat(SampleFormat::S16));
  CHECK_FALSE(isFloat(SampleFormat::S32));
  CHECK_FALSE(isFloat(SampleFormat::U8));
  CHECK_FALSE(isFloat(SampleFormat::Mulaw));
  CHECK_FALSE(isFloat(SampleFormat::Alaw));
}

TEST_CASE("SampleFormat: isInteger", "[format][sample]")
{
  CHECK(isInteger(SampleFormat::S16));
  CHECK(isInteger(SampleFormat::S32));
  CHECK(isInteger(SampleFormat::U8));
  CHECK_FALSE(isInteger(SampleFormat::F32));
  CHECK_FALSE(isInteger(SampleFormat::Mulaw));
  CHECK_FALSE(isInteger(SampleFormat::Alaw));
}

TEST_CASE("SampleFormat: isSigned", "[format][sample]")
{
  CHECK(isSigned(SampleFormat::S16));
  CHECK(isSigned(SampleFormat::S32));
  CHECK(isSigned(SampleFormat::F32));
  CHECK_FALSE(isSigned(SampleFormat::U8));
  CHECK_FALSE(isSigned(SampleFormat::Mulaw));
  CHECK_FALSE(isSigned(SampleFormat::Alaw));
}

TEST_CASE("SampleFormat: sampleFormatToString", "[format][sample]")
{
  CHECK(std::string(sampleFormatToString(SampleFormat::S16)) == "S16");
  CHECK(std::string(sampleFormatToString(SampleFormat::S32)) == "S32");
  CHECK(std::string(sampleFormatToString(SampleFormat::F32)) == "F32");
  CHECK(std::string(sampleFormatToString(SampleFormat::U8)) == "U8");
  CHECK(std::string(sampleFormatToString(SampleFormat::Mulaw)) == "Mulaw");
  CHECK(std::string(sampleFormatToString(SampleFormat::Alaw)) == "Alaw");
}

TEST_CASE("SampleFormat: identity conversion", "[format][sample][convert]")
{
  std::int16_t src[] = {-32768, 0, 32767, 1000, -1000};
  std::int16_t dst[5] = {};
  convertSamples(src, SampleFormat::S16, dst, SampleFormat::S16, 5);
  CHECK(std::memcmp(src, dst, sizeof(src)) == 0);
}

TEST_CASE("SampleFormat: S16 <-> F32 round-trip", "[format][sample][convert]")
{
  std::int16_t original[] = {0, 1000, -1000, 32767, -32768, 100, -100};
  constexpr std::size_t count = sizeof(original) / sizeof(original[0]);

  float intermediate[count] = {};
  convertSamples(original, SampleFormat::S16, intermediate, SampleFormat::F32, count);

  // F32 values should be in [-1.0, 1.0]
  for (std::size_t i = 0; i < count; ++i)
  {
    CHECK(intermediate[i] >= -1.0f);
    CHECK(intermediate[i] <= 1.0f);
  }

  std::int16_t roundTripped[count] = {};
  convertSamples(intermediate, SampleFormat::F32, roundTripped, SampleFormat::S16, count);

  for (std::size_t i = 0; i < count; ++i)
  {
    CHECK(std::abs(original[i] - roundTripped[i]) <= 1);
  }
}

TEST_CASE("SampleFormat: S16 <-> S32 round-trip", "[format][sample][convert]")
{
  std::int16_t original[] = {0, 1000, -1000, 32767, -32768};
  constexpr std::size_t count = sizeof(original) / sizeof(original[0]);

  std::int32_t intermediate[count] = {};
  convertSamples(original, SampleFormat::S16, intermediate, SampleFormat::S32, count);

  std::int16_t roundTripped[count] = {};
  convertSamples(intermediate, SampleFormat::S32, roundTripped, SampleFormat::S16, count);

  for (std::size_t i = 0; i < count; ++i)
  {
    CHECK(original[i] == roundTripped[i]);
  }
}

TEST_CASE("SampleFormat: U8 <-> S16 round-trip", "[format][sample][convert]")
{
  std::uint8_t original[] = {0, 64, 128, 192, 255};
  constexpr std::size_t count = sizeof(original) / sizeof(original[0]);

  std::int16_t intermediate[count] = {};
  convertSamples(original, SampleFormat::U8, intermediate, SampleFormat::S16, count);

  std::uint8_t roundTripped[count] = {};
  convertSamples(intermediate, SampleFormat::S16, roundTripped, SampleFormat::U8, count);

  for (std::size_t i = 0; i < count; ++i)
  {
    CHECK(original[i] == roundTripped[i]);
  }
}

TEST_CASE("SampleFormat: Mulaw -> S16 -> Mulaw decoded value preserved", "[format][sample][convert]")
{
  // G.711 encoding is not bijective (multiple codes can decode to the
  // same S16 value), so we verify that the re-encoded code decodes to
  // the same linear value — not that the byte code itself round-trips.
  for (int code = 0; code < 256; ++code)
  {
    std::uint8_t mulawByte = static_cast<std::uint8_t>(code);
    std::int16_t linear = 0;
    convertSamples(&mulawByte, SampleFormat::Mulaw, &linear, SampleFormat::S16, 1);

    std::uint8_t reEncoded = 0;
    convertSamples(&linear, SampleFormat::S16, &reEncoded, SampleFormat::Mulaw, 1);

    std::int16_t reDecoded = 0;
    convertSamples(&reEncoded, SampleFormat::Mulaw, &reDecoded, SampleFormat::S16, 1);

    CHECK(std::abs(static_cast<int>(linear) - static_cast<int>(reDecoded)) <= 1);
  }
}

TEST_CASE("SampleFormat: Alaw -> S16 -> Alaw decoded value preserved", "[format][sample][convert]")
{
  for (int code = 0; code < 256; ++code)
  {
    std::uint8_t alawByte = static_cast<std::uint8_t>(code);
    std::int16_t linear = 0;
    convertSamples(&alawByte, SampleFormat::Alaw, &linear, SampleFormat::S16, 1);

    std::uint8_t reEncoded = 0;
    convertSamples(&linear, SampleFormat::S16, &reEncoded, SampleFormat::Alaw, 1);

    std::int16_t reDecoded = 0;
    convertSamples(&reEncoded, SampleFormat::Alaw, &reDecoded, SampleFormat::S16, 1);

    CHECK(std::abs(static_cast<int>(linear) - static_cast<int>(reDecoded)) <= 1);
  }
}

TEST_CASE("SampleFormat: unsupported conversion throws", "[format][sample][convert]")
{
  float src = 1.0f;
  std::uint8_t dst = 0;
  CHECK_THROWS_AS(
    convertSamples(&src, SampleFormat::F32, &dst, SampleFormat::Mulaw, 1),
    std::invalid_argument);
}
