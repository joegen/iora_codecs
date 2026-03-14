#include <catch2/catch_test_macros.hpp>

#include "iora/codecs/dsp/goertzel_detector.hpp"
#include "iora/codecs/dsp/tone_generator.hpp"
#include "iora/codecs/pipeline/media_pipeline.hpp"

#include <cmath>
#include <cstring>
#include <memory>
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

std::shared_ptr<MediaBuffer> makeS16Buffer(
  const std::vector<std::int16_t>& samples)
{
  std::size_t bytes = samples.size() * sizeof(std::int16_t);
  auto buf = MediaBuffer::create(bytes);
  std::memcpy(buf->data(), samples.data(), bytes);
  buf->setSize(bytes);
  return buf;
}

/// Generate a pure single-frequency sine wave.
std::shared_ptr<MediaBuffer> makeSine(std::uint32_t sampleRate,
                                      double freq, float amplitude,
                                      std::uint32_t durationMs)
{
  std::uint32_t sampleCount = sampleRate * durationMs / 1000;
  std::size_t bytes = sampleCount * sizeof(std::int16_t);
  auto buf = MediaBuffer::create(bytes);
  auto* samples = reinterpret_cast<std::int16_t*>(buf->data());

  for (std::uint32_t i = 0; i < sampleCount; ++i)
  {
    static constexpr double kPi = 3.14159265358979323846;
    double phase = 2.0 * kPi * freq * i / sampleRate;
    samples[i] = static_cast<std::int16_t>(amplitude * 32767.0 * std::sin(phase));
  }
  buf->setSize(bytes);
  return buf;
}

/// Generate random noise.
std::shared_ptr<MediaBuffer> makeNoise(std::uint32_t sampleRate,
                                       std::uint32_t durationMs)
{
  std::uint32_t sampleCount = sampleRate * durationMs / 1000;
  std::size_t bytes = sampleCount * sizeof(std::int16_t);
  auto buf = MediaBuffer::create(bytes);
  auto* samples = reinterpret_cast<std::int16_t*>(buf->data());

  // Simple LCG random
  std::uint32_t seed = 42;
  for (std::uint32_t i = 0; i < sampleCount; ++i)
  {
    seed = seed * 1103515245 + 12345;
    samples[i] = static_cast<std::int16_t>((seed >> 16) & 0x7FFF) - 16384;
  }
  buf->setSize(bytes);
  return buf;
}

class CaptureHandler : public IMediaHandler
{
public:
  void incoming(std::shared_ptr<MediaBuffer> buffer) override
  {
    buffers.push_back(std::move(buffer));
  }
  void outgoing(std::shared_ptr<MediaBuffer> buffer) override
  {
    outBuffers.push_back(std::move(buffer));
  }
  std::vector<std::shared_ptr<MediaBuffer>> buffers;
  std::vector<std::shared_ptr<MediaBuffer>> outBuffers;
};

} // namespace

TEST_CASE("GoertzelDetector — detect digit '5' from ToneGenerator", "[dsp][goertzel]")
{
  ToneGenerator gen(8000);
  GoertzelParams params;
  params.minDurationMs = 0;  // Detect immediately
  GoertzelDetector det(8000, params);

  auto tone = gen.generate('5', 100, 0.5f);
  REQUIRE(tone != nullptr);

  auto result = det.detect(*tone);
  CHECK(result.detected);
  CHECK(result.digit == '5');
}

TEST_CASE("GoertzelDetector — detect all 16 DTMF digits", "[dsp][goertzel]")
{
  ToneGenerator gen(8000);
  GoertzelParams params;
  params.minDurationMs = 0;
  std::string digits = "0123456789*#ABCD";

  for (char d : digits)
  {
    GoertzelDetector det(8000, params);
    auto tone = gen.generate(d, 100, 0.5f);
    REQUIRE(tone != nullptr);

    auto result = det.detect(*tone);
    CHECK(result.detected);
    CHECK(result.digit == d);
  }
}

TEST_CASE("GoertzelDetector — silence produces no detection", "[dsp][goertzel]")
{
  GoertzelParams params;
  params.minDurationMs = 0;
  GoertzelDetector det(8000, params);

  std::vector<std::int16_t> silence(800, 0);
  auto result = det.detect(silence.data(), silence.size());
  CHECK(!result.detected);
  CHECK(result.digit == '\0');
}

TEST_CASE("GoertzelDetector — single-frequency tone rejected (twist)", "[dsp][goertzel]")
{
  GoertzelParams params;
  params.minDurationMs = 0;
  GoertzelDetector det(8000, params);

  // Pure 697Hz without matching high-group frequency.
  auto sine = makeSine(8000, 697.0, 0.5f, 100);
  auto result = det.detect(*sine);
  CHECK(!result.detected);
}

TEST_CASE("GoertzelDetector — low-amplitude tone below threshold", "[dsp][goertzel]")
{
  GoertzelParams params;
  params.minDurationMs = 0;
  params.energyThreshold = 1e12f;  // Very high threshold (magnitudes are squared)
  GoertzelDetector det(8000, params);

  ToneGenerator gen(8000);
  auto tone = gen.generate('5', 100, 0.01f);  // Very faint
  REQUIRE(tone != nullptr);

  auto result = det.detect(*tone);
  CHECK(!result.detected);
}

TEST_CASE("GoertzelDetector — minimum duration enforcement", "[dsp][goertzel]")
{
  GoertzelParams params;
  params.minDurationMs = 80;  // Require 80ms
  GoertzelDetector det(8000, params);

  ToneGenerator gen(8000);

  // 50ms tone — single frame under minDuration
  auto shortTone = gen.generate('5', 50, 0.5f);
  REQUIRE(shortTone != nullptr);
  auto r1 = det.detect(*shortTone);
  CHECK(!r1.detected);  // Only 50ms < 80ms required
}

TEST_CASE("GoertzelDetector — duration tracking across frames", "[dsp][goertzel]")
{
  GoertzelParams params;
  params.minDurationMs = 60;
  GoertzelDetector det(8000, params);

  ToneGenerator gen(8000);

  // Generate a 100ms tone and split into 5 x 20ms frames.
  auto fullTone = gen.generate('5', 100, 0.5f);
  REQUIRE(fullTone != nullptr);
  auto allSamples = readS16(*fullTone);
  std::uint32_t frameSamples = 8000 * 20 / 1000; // 160 samples per 20ms

  bool everDetected = false;
  int detectedAtFrame = -1;
  for (int frame = 0; frame < 5; ++frame)
  {
    auto r = det.detect(allSamples.data() + frame * frameSamples, frameSamples);
    if (r.detected && !everDetected)
    {
      everDetected = true;
      detectedAtFrame = frame;
    }
  }

  CHECK(everDetected);
  // Frame 0: 20ms, Frame 1: 40ms, Frame 2: 60ms >= minDurationMs
  CHECK(detectedAtFrame == 2);
}

TEST_CASE("GoertzelDetector — noise rejection", "[dsp][goertzel]")
{
  GoertzelParams params;
  params.minDurationMs = 0;
  GoertzelDetector det(8000, params);

  auto noise = makeNoise(8000, 100);
  auto result = det.detect(*noise);
  CHECK(!result.detected);
}

TEST_CASE("GoertzelDetector — asymmetric twist limits", "[dsp][goertzel]")
{
  GoertzelParams params;
  params.minDurationMs = 0;
  params.normalTwistDb = 4.0f;
  params.reverseTwistDb = 8.0f;
  GoertzelDetector det(8000, params);

  // Balanced DTMF should be detected
  ToneGenerator gen(8000);
  auto balanced = gen.generate('5', 100, 0.5f);
  auto r1 = det.detect(*balanced);
  CHECK(r1.detected);
}

TEST_CASE("GoertzelDetector — different sample rates", "[dsp][goertzel]")
{
  GoertzelParams params;
  params.minDurationMs = 0;

  SECTION("8kHz")
  {
    ToneGenerator gen(8000);
    GoertzelDetector det(8000, params);
    auto tone = gen.generate('3', 100, 0.5f);
    auto r = det.detect(*tone);
    CHECK(r.detected);
    CHECK(r.digit == '3');
  }

  SECTION("16kHz")
  {
    ToneGenerator gen(16000);
    GoertzelDetector det(16000, params);
    auto tone = gen.generate('7', 100, 0.5f);
    auto r = det.detect(*tone);
    CHECK(r.detected);
    CHECK(r.digit == '7');
  }

  SECTION("48kHz")
  {
    ToneGenerator gen(48000);
    GoertzelDetector det(48000, params);
    auto tone = gen.generate('#', 100, 0.5f);
    auto r = det.detect(*tone);
    CHECK(r.detected);
    CHECK(r.digit == '#');
  }
}

TEST_CASE("GoertzelHandler — callback invoked on detection", "[dsp][goertzel]")
{
  GoertzelParams params;
  params.minDurationMs = 0;

  char detectedDigit = '\0';
  int callbackCount = 0;
  auto cb = [&](char digit, std::uint32_t) {
    detectedDigit = digit;
    ++callbackCount;
  };

  auto handler = std::make_shared<GoertzelHandler>(8000, params, cb);
  auto capture = std::make_shared<CaptureHandler>();
  handler->addToChain(capture);

  ToneGenerator gen(8000);
  auto tone = gen.generate('9', 100, 0.5f);
  handler->incoming(tone);

  CHECK(callbackCount == 1);
  CHECK(detectedDigit == '9');

  // Audio forwarded unchanged
  REQUIRE(capture->buffers.size() == 1);
  CHECK(capture->buffers[0]->size() == tone->size());
}

TEST_CASE("GoertzelHandler — outgoing direction detection", "[dsp][goertzel]")
{
  GoertzelParams params;
  params.minDurationMs = 0;

  char detectedDigit = '\0';
  auto cb = [&](char digit, std::uint32_t) { detectedDigit = digit; };

  auto handler = std::make_shared<GoertzelHandler>(8000, params, cb);
  auto capture = std::make_shared<CaptureHandler>();
  handler->addToChain(capture);

  ToneGenerator gen(8000);
  auto tone = gen.generate('A', 100, 0.5f);
  handler->outgoing(tone);

  CHECK(detectedDigit == 'A');
  REQUIRE(capture->outBuffers.size() == 1);
}

TEST_CASE("GoertzelHandler — in MediaPipeline as named stage", "[dsp][goertzel]")
{
  GoertzelParams params;
  params.minDurationMs = 0;

  char detectedDigit = '\0';
  auto cb = [&](char digit, std::uint32_t) { detectedDigit = digit; };

  MediaPipeline pipeline;
  auto source = std::make_shared<IMediaHandler>();
  auto detector = std::make_shared<GoertzelHandler>(8000, params, cb);
  auto sink = std::make_shared<CaptureHandler>();

  REQUIRE(pipeline.addStage("source", source));
  REQUIRE(pipeline.addStage("detector", detector));
  REQUIRE(pipeline.addStage("sink", sink));
  REQUIRE(pipeline.connectStages("source", "detector"));
  REQUIRE(pipeline.connectStages("detector", "sink"));

  auto result = pipeline.start();
  REQUIRE(result.success);

  ToneGenerator gen(8000);
  auto tone = gen.generate('D', 100, 0.5f);
  pipeline.incoming(tone);

  CHECK(detectedDigit == 'D');
  REQUIRE(sink->buffers.size() == 1);
}
