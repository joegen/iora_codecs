#include <catch2/catch_test_macros.hpp>

#include "iora/codecs/dsp/resampler.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

using namespace iora::codecs;

namespace {

constexpr double kPi = 3.14159265358979323846;

/// Generate a mono sine wave at the given frequency and sample rate.
std::vector<std::int16_t> generateSineS16(std::uint32_t samples,
                                          std::uint32_t sampleRate,
                                          double freqHz = 440.0)
{
  std::vector<std::int16_t> buf(samples);
  for (std::uint32_t i = 0; i < samples; ++i)
  {
    double t = static_cast<double>(i) / sampleRate;
    buf[i] = static_cast<std::int16_t>(16000.0 * std::sin(2.0 * kPi * freqHz * t));
  }
  return buf;
}

/// Generate a mono sine wave as F32.
std::vector<float> generateSineF32(std::uint32_t samples,
                                   std::uint32_t sampleRate,
                                   double freqHz = 440.0)
{
  std::vector<float> buf(samples);
  for (std::uint32_t i = 0; i < samples; ++i)
  {
    double t = static_cast<double>(i) / sampleRate;
    buf[i] = static_cast<float>(0.5 * std::sin(2.0 * kPi * freqHz * t));
  }
  return buf;
}

/// Compute PSNR between two S16 buffers (in dB).
double computePsnrS16(const std::int16_t* a, const std::int16_t* b,
                      std::size_t count)
{
  double mse = 0.0;
  for (std::size_t i = 0; i < count; ++i)
  {
    double diff = static_cast<double>(a[i]) - static_cast<double>(b[i]);
    mse += diff * diff;
  }
  mse /= count;
  if (mse < 1e-10)
  {
    return 100.0;
  }
  double maxVal = 32767.0;
  return 10.0 * std::log10((maxVal * maxVal) / mse);
}

} // anonymous namespace

TEST_CASE("Resampler: 48kHz to 8kHz downsampling", "[dsp][resampler]")
{
  Resampler r(48000, 8000);

  auto input = generateSineS16(480, 48000); // 10ms at 48kHz
  std::vector<std::int16_t> output(Resampler::estimateOutputSamples(480, 48000, 8000) + 10);

  std::uint32_t inLen = 480;
  std::uint32_t outLen = static_cast<std::uint32_t>(output.size());

  REQUIRE(r.process(input.data(), inLen, output.data(), outLen));

  // 480 samples at 48kHz -> ~80 samples at 8kHz (allow +-2)
  CHECK(outLen >= 78);
  CHECK(outLen <= 82);
  CHECK(inLen == 480);
}

TEST_CASE("Resampler: 8kHz to 48kHz upsampling", "[dsp][resampler]")
{
  Resampler r(8000, 48000);

  auto input = generateSineS16(80, 8000); // 10ms at 8kHz
  std::vector<std::int16_t> output(Resampler::estimateOutputSamples(80, 8000, 48000) + 10);

  std::uint32_t inLen = 80;
  std::uint32_t outLen = static_cast<std::uint32_t>(output.size());

  REQUIRE(r.process(input.data(), inLen, output.data(), outLen));

  // 80 samples at 8kHz -> ~480 samples at 48kHz (allow +-2)
  CHECK(outLen >= 478);
  CHECK(outLen <= 482);
  CHECK(inLen == 80);
}

TEST_CASE("Resampler: same-rate passthrough", "[dsp][resampler]")
{
  Resampler r(48000, 48000);

  // Feed multiple frames and verify output count matches input count
  constexpr std::uint32_t kFrameSize = 480;
  constexpr std::uint32_t kFrames = 10;
  auto input = generateSineS16(kFrames * kFrameSize, 48000);
  std::uint32_t totalOut = 0;

  for (std::uint32_t f = 0; f < kFrames; ++f)
  {
    std::int16_t outBuf[500];
    std::uint32_t inLen = kFrameSize;
    std::uint32_t outLen = 500;

    REQUIRE(r.process(input.data() + f * kFrameSize, inLen, outBuf, outLen));
    CHECK(inLen == kFrameSize);
    totalOut += outLen;
  }

  // Total output should be very close to total input for same rate
  CHECK(totalOut >= kFrames * kFrameSize - 10);
  CHECK(totalOut <= kFrames * kFrameSize + 10);
}

TEST_CASE("Resampler: 48kHz->8kHz->48kHz round-trip PSNR", "[dsp][resampler]")
{
  Resampler down(48000, 8000, 1, 5);
  Resampler up(8000, 48000, 1, 5);

  // Use many frames to let resamplers settle. The round-trip introduces
  // latency from both the downsampler and upsampler filters, so we need
  // enough data and must find the correct alignment offset.
  constexpr std::uint32_t kFrames = 40;
  constexpr std::uint32_t kFrameSize = 480; // 10ms at 48kHz

  auto fullInput = generateSineS16(kFrames * kFrameSize, 48000, 440.0);
  std::vector<std::int16_t> fullOutput;
  fullOutput.reserve(fullInput.size() + 2000);

  for (std::uint32_t f = 0; f < kFrames; ++f)
  {
    const std::int16_t* frameIn = fullInput.data() + f * kFrameSize;

    // Downsample
    std::int16_t midBuf[200];
    std::uint32_t inLen = kFrameSize;
    std::uint32_t midLen = 200;
    REQUIRE(down.process(frameIn, inLen, midBuf, midLen));

    // Upsample
    std::int16_t outBuf[1200];
    std::uint32_t midIn = midLen;
    std::uint32_t outLen = 1200;
    REQUIRE(up.process(midBuf, midIn, outBuf, outLen));

    fullOutput.insert(fullOutput.end(), outBuf, outBuf + outLen);
  }

  // Find the best alignment offset by trying offsets and picking the
  // one with the highest PSNR (accounts for combined filter latency).
  std::uint32_t compareLen = 2000;
  std::uint32_t searchStart = kFrameSize * 4;
  double bestPsnr = 0.0;

  for (int offset = -200; offset <= 200; ++offset)
  {
    std::uint32_t inStart = searchStart;
    std::int32_t outStart = static_cast<std::int32_t>(searchStart) + offset;
    if (outStart < 0 || static_cast<std::uint32_t>(outStart) + compareLen > fullOutput.size())
    {
      continue;
    }
    if (inStart + compareLen > fullInput.size())
    {
      continue;
    }
    double p = computePsnrS16(fullInput.data() + inStart,
                              fullOutput.data() + outStart,
                              compareLen);
    if (p > bestPsnr)
    {
      bestPsnr = p;
    }
  }

  // 48kHz->8kHz loses frequencies above 4kHz (Nyquist at 8kHz).
  // 440Hz sine is well below that, so round-trip should preserve it well.
  CHECK(bestPsnr > 20.0);
}

TEST_CASE("Resampler: 16kHz to 8kHz (G.722 to G.711 bridge)", "[dsp][resampler]")
{
  Resampler r(16000, 8000);

  auto input = generateSineS16(160, 16000); // 10ms at 16kHz
  std::vector<std::int16_t> output(100);

  std::uint32_t inLen = 160;
  std::uint32_t outLen = 100;

  REQUIRE(r.process(input.data(), inLen, output.data(), outLen));

  // 160 samples at 16kHz -> ~80 samples at 8kHz
  CHECK(outLen >= 78);
  CHECK(outLen <= 82);
}

TEST_CASE("Resampler: all quality levels 0-10 produce valid output", "[dsp][resampler]")
{
  auto input = generateSineS16(480, 48000);

  for (int q = 0; q <= 10; ++q)
  {
    Resampler r(48000, 8000, 1, q);
    std::vector<std::int16_t> output(100);

    std::uint32_t inLen = 480;
    std::uint32_t outLen = 100;

    REQUIRE(r.process(input.data(), inLen, output.data(), outLen));
    CHECK(outLen > 0);

    // Verify output is not all zeros
    bool hasNonZero = false;
    for (std::uint32_t i = 0; i < outLen; ++i)
    {
      if (output[i] != 0)
      {
        hasNonZero = true;
        break;
      }
    }
    CHECK(hasNonZero);
  }
}

TEST_CASE("Resampler: reset() clears state", "[dsp][resampler]")
{
  Resampler r(48000, 8000);

  auto input = generateSineS16(480, 48000);
  std::vector<std::int16_t> firstPass(100);
  std::vector<std::int16_t> continuedPass(100);
  std::vector<std::int16_t> resetPass(100);

  // First pass (fresh resampler)
  std::uint32_t inLen = 480;
  std::uint32_t outLen = 100;
  REQUIRE(r.process(input.data(), inLen, firstPass.data(), outLen));
  std::uint32_t firstLen = outLen;

  // Second pass without reset (filter has accumulated state)
  inLen = 480;
  outLen = 100;
  REQUIRE(r.process(input.data(), inLen, continuedPass.data(), outLen));
  std::uint32_t continuedLen = outLen;

  // Reset and third pass (should match first pass — fresh filter state)
  r.reset();
  inLen = 480;
  outLen = 100;
  REQUIRE(r.process(input.data(), inLen, resetPass.data(), outLen));
  std::uint32_t resetLen = outLen;

  // After reset, output should match first pass exactly
  CHECK(resetLen == firstLen);
  for (std::uint32_t i = 0; i < std::min(resetLen, firstLen); ++i)
  {
    CHECK(resetPass[i] == firstPass[i]);
  }

  // Continued pass should differ from first pass (filter state was different)
  bool hasDifference = false;
  for (std::uint32_t i = 0; i < std::min(continuedLen, firstLen); ++i)
  {
    if (continuedPass[i] != firstPass[i])
    {
      hasDifference = true;
      break;
    }
  }
  CHECK(hasDifference);
}

TEST_CASE("Resampler: setRate() changes rate on the fly", "[dsp][resampler]")
{
  Resampler r(48000, 8000);

  auto input = generateSineS16(480, 48000);
  std::vector<std::int16_t> output(500);

  // First: 48kHz -> 8kHz
  std::uint32_t inLen = 480;
  std::uint32_t outLen = 500;
  REQUIRE(r.process(input.data(), inLen, output.data(), outLen));
  CHECK(outLen >= 78);
  CHECK(outLen <= 82);

  // Change to 48kHz -> 16kHz and reset to flush buffered samples
  REQUIRE(r.setRate(48000, 16000));
  CHECK(r.inputRate() == 48000);
  CHECK(r.outputRate() == 16000);
  r.reset();

  // Feed a few frames so the filter settles, then check the last frame
  for (int i = 0; i < 5; ++i)
  {
    inLen = 480;
    outLen = 500;
    REQUIRE(r.process(input.data(), inLen, output.data(), outLen));
  }
  // At 48kHz -> 16kHz, 480 input -> ~160 output (allow +-4)
  CHECK(outLen >= 156);
  CHECK(outLen <= 164);
}

TEST_CASE("Resampler: setQuality() changes quality on the fly", "[dsp][resampler]")
{
  Resampler r(48000, 8000, 1, 3);
  CHECK(r.getQuality() == 3);

  r.setQuality(7);
  CHECK(r.getQuality() == 7);

  // Should still produce valid output
  auto input = generateSineS16(480, 48000);
  std::vector<std::int16_t> output(100);
  std::uint32_t inLen = 480;
  std::uint32_t outLen = 100;
  REQUIRE(r.process(input.data(), inLen, output.data(), outLen));
  CHECK(outLen > 0);
}

TEST_CASE("Resampler: stereo resampling", "[dsp][resampler]")
{
  Resampler r(48000, 8000, 2);
  CHECK(r.channels() == 2);

  // Generate stereo interleaved: 480 per-channel = 960 total int16 values
  std::vector<std::int16_t> input(480 * 2);
  for (std::uint32_t i = 0; i < 480; ++i)
  {
    double t = static_cast<double>(i) / 48000.0;
    std::int16_t left = static_cast<std::int16_t>(
      16000.0 * std::sin(2.0 * kPi * 440.0 * t));
    std::int16_t right = static_cast<std::int16_t>(
      16000.0 * std::sin(2.0 * kPi * 880.0 * t));
    input[i * 2] = left;
    input[i * 2 + 1] = right;
  }

  // Output buffer: ~80 per-channel samples * 2 channels
  std::vector<std::int16_t> output(100 * 2);

  std::uint32_t inLen = 480;  // per-channel
  std::uint32_t outLen = 100; // per-channel capacity

  REQUIRE(r.process(input.data(), inLen, output.data(), outLen));

  CHECK(outLen >= 78);
  CHECK(outLen <= 82);
}

TEST_CASE("Resampler: processFloat() works for F32 samples", "[dsp][resampler]")
{
  Resampler r(48000, 8000);

  auto input = generateSineF32(480, 48000);
  std::vector<float> output(100);

  std::uint32_t inLen = 480;
  std::uint32_t outLen = 100;

  REQUIRE(r.processFloat(input.data(), inLen, output.data(), outLen));

  CHECK(outLen >= 78);
  CHECK(outLen <= 82);

  // Verify output is not all zeros
  bool hasNonZero = false;
  for (std::uint32_t i = 0; i < outLen; ++i)
  {
    if (std::fabs(output[i]) > 1e-6f)
    {
      hasNonZero = true;
      break;
    }
  }
  CHECK(hasNonZero);
}

TEST_CASE("Resampler: estimateOutputSamples() returns correct estimates", "[dsp][resampler]")
{
  // 48kHz -> 8kHz: 480 * 8000 / 48000 = 80
  CHECK(Resampler::estimateOutputSamples(480, 48000, 8000) == 80);

  // 8kHz -> 48kHz: 80 * 48000 / 8000 = 480
  CHECK(Resampler::estimateOutputSamples(80, 8000, 48000) == 480);

  // Same rate: should be identity
  CHECK(Resampler::estimateOutputSamples(480, 48000, 48000) == 480);

  // Non-exact ratio: 160 * 8000 / 16000 = 80
  CHECK(Resampler::estimateOutputSamples(160, 16000, 8000) == 80);

  // Rounds up: 1 sample at 48kHz -> 8kHz = ceil(1 * 8000 / 48000) = 1
  CHECK(Resampler::estimateOutputSamples(1, 48000, 8000) == 1);

  // Zero input -> zero output
  CHECK(Resampler::estimateOutputSamples(0, 48000, 8000) == 0);
}

TEST_CASE("Resampler: constructor throws on invalid args", "[dsp][resampler]")
{
  CHECK_THROWS_AS(Resampler(0, 8000), std::runtime_error);
  CHECK_THROWS_AS(Resampler(48000, 0), std::runtime_error);
  CHECK_THROWS_AS(Resampler(48000, 8000, 0), std::runtime_error);
}

TEST_CASE("Resampler: inputLatency/outputLatency return non-negative", "[dsp][resampler]")
{
  Resampler r(48000, 8000);
  CHECK(r.inputLatency() >= 0);
  CHECK(r.outputLatency() >= 0);
}

TEST_CASE("Resampler: move constructor transfers ownership", "[dsp][resampler]")
{
  Resampler r1(48000, 8000);
  CHECK(r1.inputRate() == 48000);
  CHECK(r1.outputRate() == 8000);

  Resampler r2(std::move(r1));
  CHECK(r2.inputRate() == 48000);
  CHECK(r2.outputRate() == 8000);

  // Moved-from object should have zeroed state
  CHECK(r1.inputRate() == 0);
  CHECK(r1.outputRate() == 0);

  // Moved-to object should still work
  auto input = generateSineS16(480, 48000);
  std::vector<std::int16_t> output(100);
  std::uint32_t inLen = 480;
  std::uint32_t outLen = 100;
  REQUIRE(r2.process(input.data(), inLen, output.data(), outLen));
  CHECK(outLen > 0);
}

TEST_CASE("Resampler: zero-length input produces zero-length output", "[dsp][resampler]")
{
  Resampler r(48000, 8000);

  std::int16_t dummy = 0;
  std::uint32_t inLen = 0;
  std::uint32_t outLen = 100;

  std::vector<std::int16_t> output(100);
  REQUIRE(r.process(&dummy, inLen, output.data(), outLen));
  CHECK(outLen == 0);
}

TEST_CASE("Resampler: accessor methods", "[dsp][resampler]")
{
  Resampler r(48000, 8000, 2, 5);

  CHECK(r.inputRate() == 48000);
  CHECK(r.outputRate() == 8000);
  CHECK(r.channels() == 2);
  CHECK(r.getQuality() == 5);
}
