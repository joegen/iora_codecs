#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "iora/codecs/dsp/vad.hpp"
#include "iora/codecs/pipeline/media_pipeline.hpp"

#include <cmath>
#include <cstring>
#include <memory>
#include <vector>

using namespace iora::codecs;
using Catch::Matchers::WithinAbs;

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

/// Generate a sine wave at the given frequency and amplitude.
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

std::vector<std::int16_t> makeSilence(std::size_t sampleCount)
{
  return std::vector<std::int16_t>(sampleCount, 0);
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

TEST_CASE("Vad — silence detection", "[dsp][vad]")
{
  Vad vad;
  auto silence = makeSilence(320);
  auto result = vad.process(silence.data(), silence.size());
  CHECK(!result.isActive);
  CHECK(result.rmsEnergy == 0.0f);
  CHECK(result.peakAmplitude == 0.0f);
}

TEST_CASE("Vad — speech detection with sine wave", "[dsp][vad]")
{
  Vad vad;
  auto speech = makeSine(16000, 440.0f, 0.5f, 320);
  auto result = vad.process(speech.data(), speech.size());
  CHECK(result.isActive);
  CHECK(result.rmsEnergy > 100.0f);
  CHECK(result.peakAmplitude > 0.0f);
}

TEST_CASE("Vad — threshold sensitivity", "[dsp][vad]")
{
  VadParams params;
  params.silenceThresholdRms = 1000.0f;
  Vad vad(params);

  SECTION("just below threshold — inactive")
  {
    // DC signal at ~900 RMS
    std::vector<std::int16_t> samples(320, 900);
    auto result = vad.process(samples.data(), samples.size());
    CHECK(!result.isActive);
  }

  SECTION("just above threshold — active")
  {
    // DC signal at ~1100 RMS
    std::vector<std::int16_t> samples(320, 1100);
    auto result = vad.process(samples.data(), samples.size());
    CHECK(result.isActive);
  }
}

TEST_CASE("Vad — hangover maintains active during brief silence", "[dsp][vad]")
{
  VadParams params;
  params.hangoverFrames = 5;
  Vad vad(params);

  auto speech = makeSine(16000, 440.0f, 0.5f, 320);
  auto silence = makeSilence(320);

  // Speech frame
  auto r1 = vad.process(speech.data(), speech.size());
  REQUIRE(r1.isActive);

  // 3 silence frames — within hangover
  for (int i = 0; i < 3; ++i)
  {
    auto r = vad.process(silence.data(), silence.size());
    CHECK(r.isActive);
  }

  // Another speech frame — resets hangover
  auto r2 = vad.process(speech.data(), speech.size());
  CHECK(r2.isActive);
}

TEST_CASE("Vad — hangover expires after sustained silence", "[dsp][vad]")
{
  VadParams params;
  params.hangoverFrames = 3;
  Vad vad(params);

  auto speech = makeSine(16000, 440.0f, 0.5f, 320);
  auto silence = makeSilence(320);

  // Speech frame
  vad.process(speech.data(), speech.size());

  // Silence frames — first 3 within hangover, 4th expires
  for (int i = 0; i < 3; ++i)
  {
    auto r = vad.process(silence.data(), silence.size());
    CHECK(r.isActive);
  }

  // Frame 4: hangover expired
  auto r = vad.process(silence.data(), silence.size());
  CHECK(!r.isActive);
}

TEST_CASE("Vad — RMS energy calculation for known signal", "[dsp][vad]")
{
  Vad vad;

  // DC signal at value 1000 — RMS should be exactly 1000.0
  std::vector<std::int16_t> dc(1000, 1000);
  auto result = vad.process(dc.data(), dc.size());
  CHECK_THAT(result.rmsEnergy, WithinAbs(1000.0f, 0.1f));
}

TEST_CASE("Vad — peak amplitude correctly reported", "[dsp][vad]")
{
  Vad vad;
  std::vector<std::int16_t> samples = {100, -500, 200, 32767, -100};
  auto result = vad.process(samples.data(), samples.size());
  CHECK(result.peakAmplitude == 32767.0f);
}

TEST_CASE("Vad — peak amplitude for negative max", "[dsp][vad]")
{
  Vad vad;
  std::vector<std::int16_t> samples = {100, -32768, 200};
  auto result = vad.process(samples.data(), samples.size());
  CHECK(result.peakAmplitude == 32768.0f);
}

TEST_CASE("Vad — reset clears hangover state", "[dsp][vad]")
{
  VadParams params;
  params.hangoverFrames = 10;
  Vad vad(params);

  auto speech = makeSine(16000, 440.0f, 0.5f, 320);
  auto silence = makeSilence(320);

  // Activate VAD
  vad.process(speech.data(), speech.size());

  // 1 silence frame — still active due to hangover
  auto r1 = vad.process(silence.data(), silence.size());
  CHECK(r1.isActive);

  // Reset — clears hangover
  vad.reset();

  // Next silence frame — now inactive (no hangover history)
  auto r2 = vad.process(silence.data(), silence.size());
  CHECK(!r2.isActive);
}

TEST_CASE("Vad — process(MediaBuffer&) convenience", "[dsp][vad]")
{
  Vad vad;
  auto speech = makeSine(16000, 440.0f, 0.5f, 320);
  auto buf = makeS16Buffer(speech);
  auto result = vad.process(*buf);
  CHECK(result.isActive);
  CHECK(result.rmsEnergy > 100.0f);
}

TEST_CASE("VadHandler DROP_SILENT — silent frames not forwarded", "[dsp][vad]")
{
  auto capture = std::make_shared<CaptureHandler>();
  auto handler = std::make_shared<VadHandler>(VadParams{}, VadMode::DROP_SILENT);
  handler->addToChain(capture);

  auto silence = makeSilence(320);
  auto buf = makeS16Buffer(silence);
  handler->incoming(buf);

  CHECK(capture->buffers.empty());
}

TEST_CASE("VadHandler DROP_SILENT — speech frames forwarded", "[dsp][vad]")
{
  auto capture = std::make_shared<CaptureHandler>();
  auto handler = std::make_shared<VadHandler>(VadParams{}, VadMode::DROP_SILENT);
  handler->addToChain(capture);

  auto speech = makeSine(16000, 440.0f, 0.5f, 320);
  auto buf = makeS16Buffer(speech);
  handler->incoming(buf);

  CHECK(capture->buffers.size() == 1);
}

TEST_CASE("VadHandler MARK_ONLY — all frames forwarded, marker on speech onset", "[dsp][vad]")
{
  auto capture = std::make_shared<CaptureHandler>();
  auto handler = std::make_shared<VadHandler>(VadParams{}, VadMode::MARK_ONLY);
  handler->addToChain(capture);

  auto silence = makeSilence(320);
  auto speech = makeSine(16000, 440.0f, 0.5f, 320);

  // Silence frame — forwarded, no marker
  auto buf1 = makeS16Buffer(silence);
  buf1->setMarker(false);
  handler->incoming(buf1);

  REQUIRE(capture->buffers.size() == 1);
  CHECK(!capture->buffers[0]->marker());

  // Speech frame — forwarded with marker (onset)
  auto buf2 = makeS16Buffer(speech);
  buf2->setMarker(false);
  handler->incoming(buf2);

  REQUIRE(capture->buffers.size() == 2);
  CHECK(capture->buffers[1]->marker());

  // Another speech frame — no marker (continuation)
  auto buf3 = makeS16Buffer(speech);
  buf3->setMarker(false);
  handler->incoming(buf3);

  REQUIRE(capture->buffers.size() == 3);
  CHECK(!capture->buffers[2]->marker());
}

TEST_CASE("VadHandler — outgoing direction works", "[dsp][vad]")
{
  VadParams params;
  params.hangoverFrames = 0;  // No hangover for deterministic test
  auto capture = std::make_shared<CaptureHandler>();
  auto handler = std::make_shared<VadHandler>(params, VadMode::DROP_SILENT);
  handler->addToChain(capture);

  auto speech = makeSine(16000, 440.0f, 0.5f, 320);
  auto silence = makeSilence(320);

  handler->outgoing(makeS16Buffer(speech));
  CHECK(capture->outBuffers.size() == 1);

  handler->outgoing(makeS16Buffer(silence));
  CHECK(capture->outBuffers.size() == 1);  // silence dropped
}

TEST_CASE("VadHandler — in MediaPipeline as named stage", "[dsp][vad]")
{
  MediaPipeline pipeline;

  VadParams params;
  params.hangoverFrames = 0;
  auto source = std::make_shared<IMediaHandler>();
  auto vadHandler = std::make_shared<VadHandler>(params, VadMode::DROP_SILENT);
  auto sink = std::make_shared<CaptureHandler>();

  REQUIRE(pipeline.addStage("source", source));
  REQUIRE(pipeline.addStage("vad", vadHandler));
  REQUIRE(pipeline.addStage("sink", sink));
  REQUIRE(pipeline.connectStages("source", "vad"));
  REQUIRE(pipeline.connectStages("vad", "sink"));

  auto result = pipeline.start();
  REQUIRE(result.success);

  auto speech = makeSine(16000, 440.0f, 0.5f, 320);
  auto silence = makeSilence(320);

  pipeline.incoming(makeS16Buffer(speech));
  CHECK(sink->buffers.size() == 1);

  pipeline.incoming(makeS16Buffer(silence));
  CHECK(sink->buffers.size() == 1);  // silence dropped
}
