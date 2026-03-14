#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "iora/codecs/dsp/gain.hpp"
#include "iora/codecs/pipeline/media_pipeline.hpp"

#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
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

class CaptureHandler : public IMediaHandler
{
public:
  void incoming(std::shared_ptr<MediaBuffer> buffer) override
  {
    lastIncoming = std::move(buffer);
    incomingCount++;
  }
  void outgoing(std::shared_ptr<MediaBuffer> buffer) override
  {
    lastOutgoing = std::move(buffer);
    outgoingCount++;
  }
  std::shared_ptr<MediaBuffer> lastIncoming;
  std::shared_ptr<MediaBuffer> lastOutgoing;
  int incomingCount = 0;
  int outgoingCount = 0;
};

} // namespace

TEST_CASE("Gain — unity gain leaves input unchanged", "[dsp][gain]")
{
  Gain gain(1.0f);
  std::vector<std::int16_t> samples = {0, 100, -100, 32767, -32768, 1000};
  std::vector<std::int16_t> original = samples;
  gain.apply(samples.data(), samples.size());
  REQUIRE(samples == original);
}

TEST_CASE("Gain — double gain doubles samples with saturation", "[dsp][gain]")
{
  Gain gain(2.0f);
  std::vector<std::int16_t> samples = {100, -100, 0, 20000, -20000};
  gain.apply(samples.data(), samples.size());
  CHECK(samples[0] == 200);
  CHECK(samples[1] == -200);
  CHECK(samples[2] == 0);
  CHECK(samples[3] == 32767);   // saturated
  CHECK(samples[4] == -32768);  // saturated
}

TEST_CASE("Gain — half gain halves samples", "[dsp][gain]")
{
  Gain gain(0.5f);
  std::vector<std::int16_t> samples = {1000, -1000, 100, -100};
  gain.apply(samples.data(), samples.size());
  CHECK(samples[0] == 500);
  CHECK(samples[1] == -500);
  CHECK(samples[2] == 50);
  CHECK(samples[3] == -50);
}

TEST_CASE("Gain — zero gain produces silence", "[dsp][gain]")
{
  Gain gain(0.0f);
  std::vector<std::int16_t> samples = {1000, -1000, 32767, -32768};
  gain.apply(samples.data(), samples.size());
  for (auto s : samples)
  {
    CHECK(s == 0);
  }
}

TEST_CASE("Gain — saturation clamping at boundaries", "[dsp][gain]")
{
  Gain gain(2.0f);

  SECTION("positive overflow clamps to INT16_MAX")
  {
    std::int16_t sample = 32767;
    gain.apply(&sample, 1);
    CHECK(sample == 32767);
  }

  SECTION("negative overflow clamps to INT16_MIN")
  {
    std::int16_t sample = -32768;
    gain.apply(&sample, 1);
    CHECK(sample == -32768);
  }

  SECTION("near-boundary positive")
  {
    std::int16_t sample = 20000;
    gain.apply(&sample, 1);
    CHECK(sample == 32767);  // 40000 clamped
  }

  SECTION("near-boundary negative")
  {
    std::int16_t sample = -20000;
    gain.apply(&sample, 1);
    CHECK(sample == -32768);  // -40000 clamped
  }
}

TEST_CASE("Gain — negative gain factor rejected", "[dsp][gain]")
{
  CHECK_THROWS_AS(Gain(-1.0f), std::invalid_argument);
  CHECK_THROWS_AS(Gain(-0.001f), std::invalid_argument);

  Gain gain(1.0f);
  CHECK_THROWS_AS(gain.setGain(-1.0f), std::invalid_argument);
}

TEST_CASE("Gain — NaN and infinity rejected", "[dsp][gain]")
{
  CHECK_THROWS_AS(Gain(std::numeric_limits<float>::quiet_NaN()), std::invalid_argument);
  CHECK_THROWS_AS(Gain(std::numeric_limits<float>::infinity()), std::invalid_argument);

  Gain gain(1.0f);
  CHECK_THROWS_AS(gain.setGain(std::numeric_limits<float>::quiet_NaN()), std::invalid_argument);
  CHECK_THROWS_AS(gain.setGain(std::numeric_limits<float>::infinity()), std::invalid_argument);
}

TEST_CASE("Gain — apply(nullptr, N) is safe no-op", "[dsp][gain]")
{
  Gain gain(2.0f);
  gain.apply(nullptr, 100);  // should not crash
}

TEST_CASE("Gain — setGain while muted updates unmute value", "[dsp][gain]")
{
  Gain gain(2.0f);
  gain.mute();
  gain.setGain(3.0f);
  gain.unmute();
  CHECK(gain.gain() == 3.0f);
}

TEST_CASE("Gain — dB conversion round-trip", "[dsp][gain]")
{
  using Catch::Matchers::WithinAbs;

  SECTION("+6dB ≈ 2.0x")
  {
    Gain gain;
    gain.setGainDb(6.0f);
    CHECK_THAT(gain.gain(), WithinAbs(1.9953f, 0.01f));
    CHECK_THAT(gain.gainDb(), WithinAbs(6.0f, 0.05f));
  }

  SECTION("-6dB ≈ 0.5x")
  {
    Gain gain;
    gain.setGainDb(-6.0f);
    CHECK_THAT(gain.gain(), WithinAbs(0.5012f, 0.01f));
    CHECK_THAT(gain.gainDb(), WithinAbs(-6.0f, 0.05f));
  }

  SECTION("0dB = 1.0x")
  {
    Gain gain;
    gain.setGainDb(0.0f);
    CHECK_THAT(gain.gain(), WithinAbs(1.0f, 0.001f));
    CHECK_THAT(gain.gainDb(), WithinAbs(0.0f, 0.001f));
  }

  SECTION("zero gain returns -infinity dB")
  {
    Gain gain(0.0f);
    CHECK(gain.gainDb() == -std::numeric_limits<float>::infinity());
  }
}

TEST_CASE("Gain — mute and unmute", "[dsp][gain]")
{
  Gain gain(2.0f);
  CHECK(!gain.isMuted());

  gain.mute();
  CHECK(gain.isMuted());

  std::vector<std::int16_t> samples = {1000, -1000};
  gain.apply(samples.data(), samples.size());
  CHECK(samples[0] == 0);
  CHECK(samples[1] == 0);

  gain.unmute();
  CHECK(!gain.isMuted());
  CHECK(gain.gain() == 2.0f);

  samples = {1000, -1000};
  gain.apply(samples.data(), samples.size());
  CHECK(samples[0] == 2000);
  CHECK(samples[1] == -2000);
}

TEST_CASE("Gain — apply(MediaBuffer&) convenience", "[dsp][gain]")
{
  Gain gain(2.0f);
  auto buf = makeS16Buffer({100, -100, 500});
  gain.apply(*buf);
  auto result = readS16(*buf);
  CHECK(result[0] == 200);
  CHECK(result[1] == -200);
  CHECK(result[2] == 1000);
}

TEST_CASE("Gain — apply(MediaBuffer&) rejects odd-byte buffer", "[dsp][gain]")
{
  Gain gain(2.0f);
  auto buf = MediaBuffer::create(5);
  std::uint8_t data[5] = {0x64, 0x00, 0x9C, 0xFF, 0x42};
  std::memcpy(buf->data(), data, 5);
  buf->setSize(5);

  std::uint8_t before[5];
  std::memcpy(before, buf->data(), 5);

  gain.apply(*buf);

  CHECK(std::memcmp(buf->data(), before, 5) == 0);
}

TEST_CASE("GainHandler — incoming applies gain and forwards", "[dsp][gain]")
{
  auto capture = std::make_shared<CaptureHandler>();
  auto handler = std::make_shared<GainHandler>(2.0f);
  handler->addToChain(capture);

  auto buf = makeS16Buffer({100, -100, 500});
  handler->incoming(buf);

  REQUIRE(capture->incomingCount == 1);
  auto result = readS16(*capture->lastIncoming);
  CHECK(result[0] == 200);
  CHECK(result[1] == -200);
  CHECK(result[2] == 1000);
}

TEST_CASE("GainHandler — outgoing applies gain and forwards", "[dsp][gain]")
{
  auto capture = std::make_shared<CaptureHandler>();
  auto handler = std::make_shared<GainHandler>(0.5f);
  handler->addToChain(capture);

  auto buf = makeS16Buffer({1000, -1000});
  handler->outgoing(buf);

  REQUIRE(capture->outgoingCount == 1);
  auto result = readS16(*capture->lastOutgoing);
  CHECK(result[0] == 500);
  CHECK(result[1] == -500);
}

TEST_CASE("GainHandler — in MediaPipeline as named stage", "[dsp][gain]")
{
  MediaPipeline pipeline;

  // Source is a passthrough (IMediaHandler default forwards to next)
  auto source = std::make_shared<IMediaHandler>();
  auto gainHandler = std::make_shared<GainHandler>(3.0f);
  auto sink = std::make_shared<CaptureHandler>();

  REQUIRE(pipeline.addStage("source", source));
  REQUIRE(pipeline.addStage("gain", gainHandler));
  REQUIRE(pipeline.addStage("sink", sink));
  REQUIRE(pipeline.connectStages("source", "gain"));
  REQUIRE(pipeline.connectStages("gain", "sink"));

  auto result = pipeline.start();
  REQUIRE(result.success);

  auto buf = makeS16Buffer({1000, -1000, 5000});
  pipeline.incoming(buf);

  REQUIRE(sink->incomingCount == 1);
  auto samples = readS16(*sink->lastIncoming);
  CHECK(samples[0] == 3000);
  CHECK(samples[1] == -3000);
  CHECK(samples[2] == 15000);
}
