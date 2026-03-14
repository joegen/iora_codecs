#include <catch2/catch_test_macros.hpp>

#include "iora/codecs/dsp/audio_mixer.hpp"
#include "iora/codecs/pipeline/audio_mixer_handler.hpp"
#include "iora/codecs/pipeline/i_media_handler.hpp"

#include <cstring>
#include <memory>
#include <vector>

using namespace iora::codecs;

namespace {

/// Mock output handler that captures received buffers.
class CaptureHandler : public IMediaHandler
{
public:
  void incoming(std::shared_ptr<MediaBuffer> buffer) override
  {
    received.push_back(std::move(buffer));
  }

  std::vector<std::shared_ptr<MediaBuffer>> received;
};

/// Mock codec for PLC testing.
class MockPlcCodec : public ICodec
{
public:
  explicit MockPlcCodec(std::int16_t plcValue = 500)
    : _plcValue(plcValue)
  {
  }

  const CodecInfo& info() const override
  {
    static CodecInfo info{"mock", CodecType::Audio, "mock", 16000, 1, 0, 64000, {}, CodecFeatures::None};
    return info;
  }

  std::shared_ptr<MediaBuffer> encode(const MediaBuffer&) override
  {
    return nullptr;
  }

  std::shared_ptr<MediaBuffer> decode(const MediaBuffer&) override
  {
    return nullptr;
  }

  std::shared_ptr<MediaBuffer> plc(std::size_t frameSamples) override
  {
    ++plcCallCount;
    auto buf = MediaBuffer::create(frameSamples * sizeof(std::int16_t));
    auto* ptr = reinterpret_cast<std::int16_t*>(buf->data());
    for (std::size_t i = 0; i < frameSamples; ++i)
    {
      ptr[i] = _plcValue;
    }
    buf->setSize(frameSamples * sizeof(std::int16_t));
    return buf;
  }

  bool setParameter(const std::string&, std::uint32_t) override { return false; }
  std::uint32_t getParameter(const std::string&) const override { return 0; }

  int plcCallCount = 0;

private:
  std::int16_t _plcValue;
};

/// Mock codec where PLC returns nullptr.
class NullPlcCodec : public ICodec
{
public:
  const CodecInfo& info() const override
  {
    static CodecInfo info{"null", CodecType::Audio, "null", 16000, 1, 0, 64000, {}, CodecFeatures::None};
    return info;
  }
  std::shared_ptr<MediaBuffer> encode(const MediaBuffer&) override { return nullptr; }
  std::shared_ptr<MediaBuffer> decode(const MediaBuffer&) override { return nullptr; }
  std::shared_ptr<MediaBuffer> plc(std::size_t) override { return nullptr; }
  bool setParameter(const std::string&, std::uint32_t) override { return false; }
  std::uint32_t getParameter(const std::string&) const override { return 0; }
};

/// Create a MediaBuffer with constant S16 value and SSRC set.
std::shared_ptr<MediaBuffer> makeBuffer(std::uint32_t ssrc,
                                        std::int16_t value,
                                        std::size_t sampleCount = 320)
{
  auto buf = MediaBuffer::create(sampleCount * sizeof(std::int16_t));
  auto* ptr = reinterpret_cast<std::int16_t*>(buf->data());
  for (std::size_t i = 0; i < sampleCount; ++i)
  {
    ptr[i] = value;
  }
  buf->setSize(sampleCount * sizeof(std::int16_t));
  buf->setSsrc(ssrc);
  return buf;
}

/// Read first S16 sample from a MediaBuffer.
std::int16_t firstSample(const std::shared_ptr<MediaBuffer>& buf)
{
  std::int16_t val = 0;
  std::memcpy(&val, buf->data(), sizeof(val));
  return val;
}

} // namespace

static constexpr std::size_t kFrameSamples = 320; // 20ms at 16kHz

TEST_CASE("AudioMixerHandler: 2-participant conference", "[pipeline][mixer]")
{
  MixParams params;
  params.targetSampleRate = 16000;
  params.algorithm = MixAlgorithm::SaturatingAdd;

  AudioMixerHandler handler(params);

  auto capA = std::make_shared<CaptureHandler>();
  auto capB = std::make_shared<CaptureHandler>();

  handler.addParticipant(100, capA);
  handler.addParticipant(200, capB);

  handler.incoming(makeBuffer(100, 1000));
  handler.incoming(makeBuffer(200, 2000));
  handler.mix();

  // A's output should contain B's audio (2000).
  REQUIRE(capA->received.size() == 1);
  CHECK(firstSample(capA->received[0]) == 2000);

  // B's output should contain A's audio (1000).
  REQUIRE(capB->received.size() == 1);
  CHECK(firstSample(capB->received[0]) == 1000);
}

TEST_CASE("AudioMixerHandler: 3-participant N-1 routing", "[pipeline][mixer]")
{
  MixParams params;
  params.targetSampleRate = 16000;
  params.algorithm = MixAlgorithm::SampleAverage;

  AudioMixerHandler handler(params);

  auto capA = std::make_shared<CaptureHandler>();
  auto capB = std::make_shared<CaptureHandler>();
  auto capC = std::make_shared<CaptureHandler>();

  handler.addParticipant(1, capA);
  handler.addParticipant(2, capB);
  handler.addParticipant(3, capC);

  handler.incoming(makeBuffer(1, 1000));
  handler.incoming(makeBuffer(2, 2000));
  handler.incoming(makeBuffer(3, 3000));
  handler.mix();

  // A hears avg(B, C) = (2000 + 3000) / 2 = 2500
  REQUIRE(capA->received.size() == 1);
  CHECK(firstSample(capA->received[0]) == 2500);

  // B hears avg(A, C) = (1000 + 3000) / 2 = 2000
  REQUIRE(capB->received.size() == 1);
  CHECK(firstSample(capB->received[0]) == 2000);

  // C hears avg(A, B) = (1000 + 2000) / 2 = 1500
  REQUIRE(capC->received.size() == 1);
  CHECK(firstSample(capC->received[0]) == 1500);
}

TEST_CASE("AudioMixerHandler: add participant mid-conference", "[pipeline][mixer]")
{
  MixParams params;
  params.targetSampleRate = 16000;
  params.algorithm = MixAlgorithm::SaturatingAdd;

  AudioMixerHandler handler(params);

  auto capA = std::make_shared<CaptureHandler>();
  auto capB = std::make_shared<CaptureHandler>();

  handler.addParticipant(1, capA);
  handler.addParticipant(2, capB);

  handler.incoming(makeBuffer(1, 1000));
  handler.incoming(makeBuffer(2, 2000));
  handler.mix();

  REQUIRE(capA->received.size() == 1);
  CHECK(firstSample(capA->received[0]) == 2000);

  // Add participant 3 mid-conference.
  auto capC = std::make_shared<CaptureHandler>();
  handler.addParticipant(3, capC);

  handler.incoming(makeBuffer(1, 1000));
  handler.incoming(makeBuffer(2, 2000));
  handler.incoming(makeBuffer(3, 3000));
  handler.mix();

  // A now hears B + C = 5000
  REQUIRE(capA->received.size() == 2);
  CHECK(firstSample(capA->received[1]) == 5000);

  // C hears A + B = 3000
  REQUIRE(capC->received.size() == 1);
  CHECK(firstSample(capC->received[0]) == 3000);
}

TEST_CASE("AudioMixerHandler: remove participant mid-conference", "[pipeline][mixer]")
{
  MixParams params;
  params.targetSampleRate = 16000;
  params.algorithm = MixAlgorithm::SaturatingAdd;

  AudioMixerHandler handler(params);

  auto capA = std::make_shared<CaptureHandler>();
  auto capB = std::make_shared<CaptureHandler>();
  auto capC = std::make_shared<CaptureHandler>();

  handler.addParticipant(1, capA);
  handler.addParticipant(2, capB);
  handler.addParticipant(3, capC);

  handler.incoming(makeBuffer(1, 1000));
  handler.incoming(makeBuffer(2, 2000));
  handler.incoming(makeBuffer(3, 3000));
  handler.mix();

  CHECK(handler.participantCount() == 3);

  // Remove participant 2.
  handler.removeParticipant(2);
  CHECK(handler.participantCount() == 2);

  handler.incoming(makeBuffer(1, 1000));
  handler.incoming(makeBuffer(3, 3000));
  handler.mix();

  // A now hears only C = 3000
  REQUIRE(capA->received.size() == 2);
  CHECK(firstSample(capA->received[1]) == 3000);
}

TEST_CASE("AudioMixerHandler: missing frame skipped (no PLC)", "[pipeline][mixer]")
{
  MixParams params;
  params.targetSampleRate = 16000;
  params.algorithm = MixAlgorithm::SaturatingAdd;

  AudioMixerHandler handler(params);

  auto capA = std::make_shared<CaptureHandler>();
  auto capB = std::make_shared<CaptureHandler>();

  handler.addParticipant(1, capA);
  handler.addParticipant(2, capB);

  // Only participant 1 sends audio.
  handler.incoming(makeBuffer(1, 1000));
  // Participant 2 is missing.
  handler.mix();

  // B's output should contain A's audio.
  REQUIRE(capB->received.size() == 1);
  CHECK(firstSample(capB->received[0]) == 1000);

  // A has no audio from B → nullptr (nothing forwarded).
  CHECK(capA->received.empty());
}

TEST_CASE("AudioMixerHandler: PLC on missing frame", "[pipeline][mixer]")
{
  MixParams params;
  params.targetSampleRate = 16000;
  params.algorithm = MixAlgorithm::SaturatingAdd;

  AudioMixerHandler handler(params);

  auto capA = std::make_shared<CaptureHandler>();
  auto capB = std::make_shared<CaptureHandler>();

  MockPlcCodec plcCodecB(500); // PLC generates constant 500

  handler.addParticipant(1, capA, nullptr);
  handler.addParticipant(2, capB, &plcCodecB);

  // Only participant 1 sends.
  handler.incoming(makeBuffer(1, 1000));
  handler.mix();

  // PLC should have been called for participant 2.
  CHECK(plcCodecB.plcCallCount == 1);

  // A should hear PLC from B (500).
  REQUIRE(capA->received.size() == 1);
  CHECK(firstSample(capA->received[0]) == 500);

  // B should hear A (1000).
  REQUIRE(capB->received.size() == 1);
  CHECK(firstSample(capB->received[0]) == 1000);
}

TEST_CASE("AudioMixerHandler: PLC fallback to silence", "[pipeline][mixer]")
{
  MixParams params;
  params.targetSampleRate = 16000;
  params.algorithm = MixAlgorithm::SaturatingAdd;

  AudioMixerHandler handler(params);

  auto capA = std::make_shared<CaptureHandler>();
  auto capB = std::make_shared<CaptureHandler>();

  NullPlcCodec nullPlc;

  handler.addParticipant(1, capA, nullptr);
  handler.addParticipant(2, capB, &nullPlc);

  handler.incoming(makeBuffer(1, 1000));
  handler.mix();

  // PLC returned nullptr → B is skipped.
  // A has no other sources → nothing forwarded.
  CHECK(capA->received.empty());

  // B still hears A.
  REQUIRE(capB->received.size() == 1);
  CHECK(firstSample(capB->received[0]) == 1000);
}

TEST_CASE("AudioMixerHandler: SSRC identification", "[pipeline][mixer]")
{
  MixParams params;
  params.targetSampleRate = 16000;
  AudioMixerHandler handler(params);

  auto capA = std::make_shared<CaptureHandler>();
  handler.addParticipant(42, capA);

  // Buffer with unknown SSRC is silently dropped.
  handler.incoming(makeBuffer(999, 1000));
  CHECK(handler.bufferCount(42) == 0);

  // Buffer with correct SSRC is buffered.
  handler.incoming(makeBuffer(42, 1000));
  CHECK(handler.bufferCount(42) == 1);
}

TEST_CASE("AudioMixerHandler: output SSRC is set per participant", "[pipeline][mixer]")
{
  MixParams params;
  params.targetSampleRate = 16000;
  params.algorithm = MixAlgorithm::SaturatingAdd;

  AudioMixerHandler handler(params);

  auto capA = std::make_shared<CaptureHandler>();
  auto capB = std::make_shared<CaptureHandler>();

  handler.addParticipant(100, capA);
  handler.addParticipant(200, capB);

  handler.incoming(makeBuffer(100, 1000));
  handler.incoming(makeBuffer(200, 2000));
  handler.mix();

  // Output buffer SSRC matches the participant it's destined for.
  REQUIRE(capA->received.size() == 1);
  CHECK(capA->received[0]->ssrc() == 100);

  REQUIRE(capB->received.size() == 1);
  CHECK(capB->received[0]->ssrc() == 200);
}

TEST_CASE("AudioMixerHandler: empty conference", "[pipeline][mixer]")
{
  MixParams params;
  params.targetSampleRate = 16000;
  AudioMixerHandler handler(params);

  CHECK(handler.participantCount() == 0);

  // Mix with no participants → no crash.
  handler.mix();
}

TEST_CASE("AudioMixerHandler: single participant", "[pipeline][mixer]")
{
  MixParams params;
  params.targetSampleRate = 16000;
  AudioMixerHandler handler(params);

  auto capA = std::make_shared<CaptureHandler>();
  handler.addParticipant(1, capA);

  handler.incoming(makeBuffer(1, 1000));
  handler.mix();

  // Single participant → no mix output (nobody else to hear).
  CHECK(capA->received.empty());
}

TEST_CASE("AudioMixerHandler: bounded frame queue drops oldest", "[pipeline][mixer]")
{
  MixParams params;
  params.targetSampleRate = 16000;
  params.algorithm = MixAlgorithm::SaturatingAdd;

  AudioMixerHandler handler(params);

  auto capA = std::make_shared<CaptureHandler>();
  auto capB = std::make_shared<CaptureHandler>();

  handler.addParticipant(1, capA);
  handler.addParticipant(2, capB);

  // Push 4 frames for participant 1 (max is 3).
  handler.incoming(makeBuffer(1, 1000)); // oldest
  handler.incoming(makeBuffer(1, 2000));
  handler.incoming(makeBuffer(1, 3000));
  handler.incoming(makeBuffer(1, 4000)); // newest — oldest (1000) dropped

  CHECK(handler.bufferCount(1) == 3);

  // Push one frame for participant 2.
  handler.incoming(makeBuffer(2, 5000));

  // Mix should use oldest remaining frame for 1 (2000).
  handler.mix();

  REQUIRE(capB->received.size() == 1);
  CHECK(firstSample(capB->received[0]) == 2000);
}

TEST_CASE("AudioMixerHandler: multiple sequential mix rounds", "[pipeline][mixer]")
{
  MixParams params;
  params.targetSampleRate = 16000;
  params.algorithm = MixAlgorithm::SaturatingAdd;

  AudioMixerHandler handler(params);

  auto capA = std::make_shared<CaptureHandler>();
  auto capB = std::make_shared<CaptureHandler>();

  handler.addParticipant(1, capA);
  handler.addParticipant(2, capB);

  for (int round = 0; round < 5; ++round)
  {
    auto val = static_cast<std::int16_t>(1000 + round * 100);
    handler.incoming(makeBuffer(1, val));
    handler.incoming(makeBuffer(2, val));
    handler.mix();
  }

  CHECK(capA->received.size() == 5);
  CHECK(capB->received.size() == 5);
}
