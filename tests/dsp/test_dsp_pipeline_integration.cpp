#include <catch2/catch_test_macros.hpp>

#include "iora/codecs/dsp/gain.hpp"
#include "iora/codecs/dsp/goertzel_detector.hpp"
#include "iora/codecs/dsp/tone_generator.hpp"
#include "iora/codecs/dsp/vad.hpp"
#include "iora/codecs/pipeline/media_pipeline.hpp"

#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace iora::codecs;

namespace {

std::shared_ptr<MediaBuffer> makeS16Buffer(
  const std::vector<std::int16_t>& samples)
{
  std::size_t bytes = samples.size() * sizeof(std::int16_t);
  auto buf = MediaBuffer::create(bytes);
  std::memcpy(buf->data(), samples.data(), bytes);
  buf->setSize(bytes);
  return buf;
}

std::vector<std::int16_t> readS16(const MediaBuffer& buf)
{
  std::size_t count = buf.size() / sizeof(std::int16_t);
  std::vector<std::int16_t> out(count);
  std::memcpy(out.data(), buf.data(), count * sizeof(std::int16_t));
  return out;
}

std::vector<std::int16_t> makeSine(
  std::uint32_t sampleRate, float freqHz, float amplitude, std::size_t sampleCount)
{
  std::vector<std::int16_t> out(sampleCount);
  for (std::size_t i = 0; i < sampleCount; ++i)
  {
    static constexpr double kPi = 3.14159265358979323846;
    double phase = 2.0 * kPi * freqHz * static_cast<double>(i) / sampleRate;
    out[i] = static_cast<std::int16_t>(amplitude * 32767.0 * std::sin(phase));
  }
  return out;
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

TEST_CASE("Integration: VAD → Gain pipeline", "[dsp][integration]")
{
  MediaPipeline pipeline;

  VadParams vadParams;
  vadParams.hangoverFrames = 0;
  auto source = std::make_shared<IMediaHandler>();
  auto vad = std::make_shared<VadHandler>(vadParams, VadMode::DROP_SILENT);
  auto gain = std::make_shared<GainHandler>(2.0f);
  auto sink = std::make_shared<CaptureHandler>();

  REQUIRE(pipeline.addStage("source", source));
  REQUIRE(pipeline.addStage("vad", vad));
  REQUIRE(pipeline.addStage("gain", gain));
  REQUIRE(pipeline.addStage("sink", sink));
  REQUIRE(pipeline.connectStages("source", "vad"));
  REQUIRE(pipeline.connectStages("vad", "gain"));
  REQUIRE(pipeline.connectStages("gain", "sink"));

  auto result = pipeline.start();
  REQUIRE(result.success);

  // Speech frame — VAD passes, gain doubles
  auto speech = makeSine(16000, 440.0f, 0.1f, 320);
  pipeline.incoming(makeS16Buffer(speech));
  REQUIRE(sink->buffers.size() == 1);

  auto out = readS16(*sink->buffers[0]);
  // Gain should have doubled the samples
  for (std::size_t i = 0; i < out.size(); ++i)
  {
    std::int32_t expected = std::min(std::max(
      static_cast<std::int32_t>(speech[i]) * 2, -32768), 32767);
    CHECK(out[i] == static_cast<std::int16_t>(expected));
  }

  // Silence frame — VAD drops it
  std::vector<std::int16_t> silence(320, 0);
  pipeline.incoming(makeS16Buffer(silence));
  CHECK(sink->buffers.size() == 1);  // still 1
}

TEST_CASE("Integration: Gain in outgoing direction", "[dsp][integration]")
{
  MediaPipeline pipeline;

  auto source = std::make_shared<IMediaHandler>();
  auto gain = std::make_shared<GainHandler>(0.5f);
  auto sink = std::make_shared<CaptureHandler>();

  REQUIRE(pipeline.addStage("source", source));
  REQUIRE(pipeline.addStage("gain", gain));
  REQUIRE(pipeline.addStage("sink", sink));
  REQUIRE(pipeline.connectStages("source", "gain"));
  REQUIRE(pipeline.connectStages("gain", "sink"));

  auto result = pipeline.start();
  REQUIRE(result.success);

  auto buf = makeS16Buffer({1000, -1000, 2000});
  pipeline.outgoing(buf);

  // outgoing goes to exit stage (sink has no outEdges), backwards through chain
  // MediaPipeline::outgoing() pushes to exit stage (the one with no outEdges)
  // In this topology: source→gain→sink, exit is "sink"
  // outgoing() on sink's InstrumentedStage calls sink's outgoing()
  REQUIRE(sink->outBuffers.size() == 1);
}

TEST_CASE("Integration: ToneGenerator → GoertzelDetector round-trip", "[dsp][integration]")
{
  ToneGenerator gen(8000);
  GoertzelParams params;
  params.minDurationMs = 0;

  std::string digits = "123*0#ABCD";
  std::vector<char> detected;

  for (char d : digits)
  {
    GoertzelDetector det(8000, params);
    auto tone = gen.generate(d, 100, 0.5f);
    REQUIRE(tone != nullptr);
    auto result = det.detect(*tone);
    if (result.detected)
    {
      detected.push_back(result.digit);
    }
  }

  REQUIRE(detected.size() == digits.size());
  for (std::size_t i = 0; i < digits.size(); ++i)
  {
    CHECK(detected[i] == digits[i]);
  }
}

TEST_CASE("Integration: ToneGenerator → GoertzelHandler in pipeline", "[dsp][integration]")
{
  GoertzelParams params;
  params.minDurationMs = 0;

  std::vector<char> detectedDigits;
  auto cb = [&](char digit, std::uint32_t) {
    detectedDigits.push_back(digit);
  };

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
  auto seq = gen.generateSequence("159", 100, 50, 0.5f);
  for (auto& buf : seq)
  {
    pipeline.incoming(buf);
  }

  // Should have detected 3 digits (silence gaps produce no detection)
  REQUIRE(detectedDigits.size() == 3);
  CHECK(detectedDigits[0] == '1');
  CHECK(detectedDigits[1] == '5');
  CHECK(detectedDigits[2] == '9');

  // All buffers forwarded to sink (tones + gaps)
  CHECK(sink->buffers.size() == seq.size());
}

TEST_CASE("Integration: VAD + Gain + GoertzelDetector multi-stage pipeline", "[dsp][integration]")
{
  VadParams vadParams;
  vadParams.hangoverFrames = 0;
  GoertzelParams goertzelParams;
  goertzelParams.minDurationMs = 0;

  char lastDetected = '\0';
  auto cb = [&](char digit, std::uint32_t) { lastDetected = digit; };

  MediaPipeline pipeline;
  auto source = std::make_shared<IMediaHandler>();
  auto vad = std::make_shared<VadHandler>(vadParams, VadMode::DROP_SILENT);
  auto gain = std::make_shared<GainHandler>(1.0f);
  auto detector = std::make_shared<GoertzelHandler>(8000, goertzelParams, cb);
  auto sink = std::make_shared<CaptureHandler>();

  REQUIRE(pipeline.addStage("source", source));
  REQUIRE(pipeline.addStage("vad", vad));
  REQUIRE(pipeline.addStage("gain", gain));
  REQUIRE(pipeline.addStage("detector", detector));
  REQUIRE(pipeline.addStage("sink", sink));
  REQUIRE(pipeline.connectStages("source", "vad"));
  REQUIRE(pipeline.connectStages("vad", "gain"));
  REQUIRE(pipeline.connectStages("gain", "detector"));
  REQUIRE(pipeline.connectStages("detector", "sink"));

  auto result = pipeline.start();
  REQUIRE(result.success);

  // DTMF tone — VAD should pass it (it has energy), detector should detect
  ToneGenerator gen(8000);
  auto tone = gen.generate('7', 100, 0.5f);
  pipeline.incoming(tone);

  CHECK(lastDetected == '7');
  CHECK(sink->buffers.size() == 1);

  // Silence — VAD drops, detector never sees it
  lastDetected = '\0';
  std::vector<std::int16_t> silence(800, 0);
  pipeline.incoming(makeS16Buffer(silence));
  CHECK(lastDetected == '\0');
  CHECK(sink->buffers.size() == 1);  // still 1
}

TEST_CASE("Integration: All DSP handlers have metrics in pipeline", "[dsp][integration]")
{
  VadParams vadParams;
  vadParams.hangoverFrames = 0;
  GoertzelParams goertzelParams;
  goertzelParams.minDurationMs = 0;

  MediaPipeline pipeline;
  auto source = std::make_shared<IMediaHandler>();
  auto gain = std::make_shared<GainHandler>(1.5f);
  auto vad = std::make_shared<VadHandler>(vadParams, VadMode::MARK_ONLY);
  auto detector = std::make_shared<GoertzelHandler>(8000, goertzelParams);
  auto sink = std::make_shared<CaptureHandler>();

  REQUIRE(pipeline.addStage("source", source));
  REQUIRE(pipeline.addStage("gain", gain));
  REQUIRE(pipeline.addStage("vad", vad));
  REQUIRE(pipeline.addStage("detector", detector));
  REQUIRE(pipeline.addStage("sink", sink));
  REQUIRE(pipeline.connectStages("source", "gain"));
  REQUIRE(pipeline.connectStages("gain", "vad"));
  REQUIRE(pipeline.connectStages("vad", "detector"));
  REQUIRE(pipeline.connectStages("detector", "sink"));

  auto result = pipeline.start();
  REQUIRE(result.success);

  auto speech = makeSine(16000, 440.0f, 0.3f, 320);
  for (int i = 0; i < 5; ++i)
  {
    pipeline.incoming(makeS16Buffer(speech));
  }

  // All stages should show 5 incoming frames in MARK_ONLY mode
  CHECK(pipeline.getMetrics("source").framesIn == 5);
  CHECK(pipeline.getMetrics("gain").framesIn == 5);
  CHECK(pipeline.getMetrics("vad").framesIn == 5);
  CHECK(pipeline.getMetrics("detector").framesIn == 5);
  CHECK(pipeline.stageCount() == 5);
}
