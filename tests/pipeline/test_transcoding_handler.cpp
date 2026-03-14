#include <catch2/catch_test_macros.hpp>

#include "iora/codecs/pipeline/i_media_handler.hpp"
#include "iora/codecs/pipeline/transcoding_handler.hpp"
#include "iora/codecs/core/media_buffer.hpp"
#include "iora/codecs/codec/codec_info.hpp"

#include "g711/g711_codec.hpp"
#include "g711/g711_codec_factory.hpp"
#include "opus/opus_codec.hpp"
#include "opus/opus_codec_factory.hpp"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

using namespace iora::codecs;

// ============================================================================
// Helpers
// ============================================================================

/// Mock handler that captures buffers forwarded to it.
class MockHandler : public IMediaHandler
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

/// Generate a mono S16 sine wave.
static std::vector<std::int16_t> generateSineWave(
  int sampleRate, int frameSamples, double frequency, double amplitude)
{
  std::vector<std::int16_t> samples(static_cast<std::size_t>(frameSamples));
  for (int i = 0; i < frameSamples; ++i)
  {
    double t = static_cast<double>(i) / sampleRate;
    samples[static_cast<std::size_t>(i)] =
      static_cast<std::int16_t>(amplitude * std::sin(2.0 * M_PI * frequency * t));
  }
  return samples;
}

/// Wrap S16 PCM samples into a MediaBuffer.
static std::shared_ptr<MediaBuffer> wrapPcm(
  const std::int16_t* data, std::size_t sampleCount)
{
  auto bytes = sampleCount * sizeof(std::int16_t);
  auto buf = MediaBuffer::create(bytes);
  std::memcpy(buf->data(), data, bytes);
  buf->setSize(bytes);
  return buf;
}

/// Create a mono Opus CodecInfo (channels=1 for pipeline compatibility with G.711).
static CodecInfo makeMonoOpusInfo()
{
  auto info = OpusCodecFactory::makeOpusInfo();
  info.channels = 1;
  return info;
}

/// Encode PCM with a codec, return the compressed buffer.
static std::shared_ptr<MediaBuffer> encodeWith(
  ICodec& encoder, const std::int16_t* pcm, std::size_t sampleCount)
{
  auto input = wrapPcm(pcm, sampleCount);
  return encoder.encode(*input);
}

// ============================================================================
// Test 1: IMediaHandler chain wiring
// ============================================================================

/// Pass-through handler that uses default IMediaHandler forwarding.
class PassthroughHandler : public IMediaHandler
{
};

TEST_CASE("IMediaHandler: addToChain forwards correctly", "[pipeline]")
{
  auto handler = std::make_shared<PassthroughHandler>();
  auto sink = std::make_shared<MockHandler>();

  handler->addToChain(sink);

  auto buf = MediaBuffer::create(10);
  buf->setSize(10);

  handler->incoming(buf);
  REQUIRE(sink->incomingCount == 1);
  REQUIRE(sink->lastIncoming == buf);

  handler->outgoing(buf);
  REQUIRE(sink->outgoingCount == 1);
  REQUIRE(sink->lastOutgoing == buf);
}

TEST_CASE("IMediaHandler: chainWith fluent API", "[pipeline]")
{
  auto a = std::make_shared<PassthroughHandler>();
  auto b = std::make_shared<PassthroughHandler>();
  auto c = std::make_shared<MockHandler>();

  a->chainWith(b).chainWith(c);

  auto buf = MediaBuffer::create(10);
  buf->setSize(10);

  a->incoming(buf);
  REQUIRE(c->incomingCount == 1);
  REQUIRE(c->lastIncoming == buf);
}

// ============================================================================
// Test 2: Opus→G.711 transcode (48kHz→8kHz)
// ============================================================================

TEST_CASE("TranscodingHandler: Opus to G.711 PCMU transcode", "[pipeline]")
{
  auto opusInfo = makeMonoOpusInfo();
  auto pcmuInfo = G711CodecFactory::makePcmuInfo();

  OpusCodecFactory opusFactory(opusInfo);
  G711CodecFactory g711Factory(pcmuInfo);

  auto decoder = opusFactory.createDecoder(opusInfo);
  auto encoder = g711Factory.createEncoder(pcmuInfo);
  REQUIRE(decoder);
  REQUIRE(encoder);

  auto handler = std::make_shared<TranscodingHandler>(
    std::move(decoder), std::move(encoder));
  auto sink = std::make_shared<MockHandler>();
  handler->addToChain(sink);

  REQUIRE(handler->hasResampler()); // 48kHz→8kHz

  // Generate 20ms of mono 48kHz audio, encode with Opus.
  auto pcm = generateSineWave(48000, 960, 440.0, 10000.0);
  auto opusEncoder = opusFactory.createEncoder(opusInfo);
  auto opusFrame = encodeWith(*opusEncoder, pcm.data(), pcm.size());
  REQUIRE(opusFrame);

  // Feed Opus frame into the transcoding handler.
  handler->incoming(opusFrame);

  // Verify output is a valid G.711 frame.
  REQUIRE(sink->incomingCount == 1);
  auto& output = sink->lastIncoming;
  REQUIRE(output);
  // 20ms at 8kHz = 160 samples, G.711 = 1 byte/sample = 160 bytes.
  CHECK(output->size() == 160);
  CHECK(output->payloadType() == 0); // PCMU PT
}

// ============================================================================
// Test 3: G.711→Opus transcode (8kHz→48kHz)
// ============================================================================

TEST_CASE("TranscodingHandler: G.711 PCMU to Opus transcode", "[pipeline]")
{
  auto pcmuInfo = G711CodecFactory::makePcmuInfo();
  auto opusInfo = makeMonoOpusInfo();

  G711CodecFactory g711Factory(pcmuInfo);
  OpusCodecFactory opusFactory(opusInfo);

  auto decoder = g711Factory.createDecoder(pcmuInfo);
  auto encoder = opusFactory.createEncoder(opusInfo);
  REQUIRE(decoder);
  REQUIRE(encoder);

  auto handler = std::make_shared<TranscodingHandler>(
    std::move(decoder), std::move(encoder));
  auto sink = std::make_shared<MockHandler>();
  handler->addToChain(sink);

  REQUIRE(handler->hasResampler()); // 8kHz→48kHz

  // Generate 20ms of mono 8kHz audio, encode with G.711.
  auto pcm = generateSineWave(8000, 160, 440.0, 10000.0);
  auto g711Encoder = g711Factory.createEncoder(pcmuInfo);
  auto g711Frame = encodeWith(*g711Encoder, pcm.data(), pcm.size());
  REQUIRE(g711Frame);
  REQUIRE(g711Frame->size() == 160); // 160 bytes G.711

  // Feed G.711 frame into the transcoding handler.
  handler->incoming(g711Frame);

  // Verify output is a compressed Opus frame.
  REQUIRE(sink->incomingCount == 1);
  auto& output = sink->lastIncoming;
  REQUIRE(output);
  CHECK(output->size() > 0);
  CHECK(output->size() < 400); // Opus at 64kbps, 20ms < 400 bytes
  CHECK(output->payloadType() == 111); // Opus PT
}

// ============================================================================
// Test 4: Same-rate transcode (PCMU→PCMA, no resampler)
// ============================================================================

TEST_CASE("TranscodingHandler: PCMU to PCMA same-rate transcode", "[pipeline]")
{
  auto pcmuInfo = G711CodecFactory::makePcmuInfo();
  auto pcmaInfo = G711CodecFactory::makePcmaInfo();

  G711CodecFactory pcmuFactory(pcmuInfo);
  G711CodecFactory pcmaFactory(pcmaInfo);

  auto decoder = pcmuFactory.createDecoder(pcmuInfo);
  auto encoder = pcmaFactory.createEncoder(pcmaInfo);

  auto handler = std::make_shared<TranscodingHandler>(
    std::move(decoder), std::move(encoder));
  auto sink = std::make_shared<MockHandler>();
  handler->addToChain(sink);

  CHECK_FALSE(handler->hasResampler()); // same rate

  // Generate and encode a PCMU frame.
  auto pcm = generateSineWave(8000, 160, 440.0, 10000.0);
  auto pcmuEncoder = pcmuFactory.createEncoder(pcmuInfo);
  auto pcmuFrame = encodeWith(*pcmuEncoder, pcm.data(), pcm.size());
  REQUIRE(pcmuFrame);

  handler->incoming(pcmuFrame);

  REQUIRE(sink->incomingCount == 1);
  auto& output = sink->lastIncoming;
  REQUIRE(output);
  CHECK(output->size() == 160); // PCMA: 1 byte/sample
  CHECK(output->payloadType() == 8); // PCMA PT
}

// ============================================================================
// Test 5: Metadata preservation
// ============================================================================

TEST_CASE("TranscodingHandler: metadata preservation", "[pipeline]")
{
  auto pcmuInfo = G711CodecFactory::makePcmuInfo();
  auto pcmaInfo = G711CodecFactory::makePcmaInfo();

  G711CodecFactory pcmuFactory(pcmuInfo);
  G711CodecFactory pcmaFactory(pcmaInfo);

  auto decoder = pcmuFactory.createDecoder(pcmuInfo);
  auto encoder = pcmaFactory.createEncoder(pcmaInfo);

  auto handler = std::make_shared<TranscodingHandler>(
    std::move(decoder), std::move(encoder));
  auto sink = std::make_shared<MockHandler>();
  handler->addToChain(sink);

  // Create input with specific metadata.
  auto pcm = generateSineWave(8000, 160, 440.0, 10000.0);
  auto pcmuEncoder = pcmuFactory.createEncoder(pcmuInfo);
  auto frame = encodeWith(*pcmuEncoder, pcm.data(), pcm.size());
  frame->setTimestamp(12345);
  frame->setSsrc(0xDEADBEEF);
  frame->setSequenceNumber(42);
  frame->setMarker(true);

  handler->incoming(frame);

  REQUIRE(sink->lastIncoming);
  auto& out = sink->lastIncoming;
  CHECK(out->timestamp() == 12345);
  CHECK(out->ssrc() == 0xDEADBEEF);
  CHECK(out->sequenceNumber() == 42);
  CHECK(out->marker() == true);
  // PayloadType should be updated to encoder's PT.
  CHECK(out->payloadType() == 8); // PCMA
}

// ============================================================================
// Test 6: PLC on decode failure
// ============================================================================

TEST_CASE("TranscodingHandler: PLC fallback on corrupt input", "[pipeline]")
{
  auto opusInfo = makeMonoOpusInfo();
  auto pcmuInfo = G711CodecFactory::makePcmuInfo();

  OpusCodecFactory opusFactory(opusInfo);
  G711CodecFactory g711Factory(pcmuInfo);

  auto decoder = opusFactory.createDecoder(opusInfo);
  auto encoder = g711Factory.createEncoder(pcmuInfo);

  auto handler = std::make_shared<TranscodingHandler>(
    std::move(decoder), std::move(encoder));
  auto sink = std::make_shared<MockHandler>();
  handler->addToChain(sink);

  // First, feed a valid frame so decoder has state for PLC.
  auto pcm = generateSineWave(48000, 960, 440.0, 10000.0);
  auto opusEncoder = opusFactory.createEncoder(opusInfo);
  auto validFrame = encodeWith(*opusEncoder, pcm.data(), pcm.size());
  handler->incoming(validFrame);
  REQUIRE(sink->incomingCount == 1);

  // Now feed a corrupt/garbage frame — decode should fail, PLC should kick in.
  auto corrupt = MediaBuffer::create(50);
  // Fill with random non-Opus data.
  std::memset(corrupt->data(), 0xFF, 50);
  corrupt->setSize(50);

  handler->incoming(corrupt);

  // PLC should produce a valid output (Opus PLC generates concealment audio).
  CHECK(sink->incomingCount == 2);
  if (sink->lastIncoming)
  {
    CHECK(sink->lastIncoming->size() == 160); // G.711 20ms frame
  }
}

// ============================================================================
// Test 7: Round-trip quality (Opus→G.711→Opus, PSNR check)
// ============================================================================

TEST_CASE("TranscodingHandler: round-trip Opus-G711-Opus PSNR", "[pipeline]")
{
  auto opusInfo = makeMonoOpusInfo();
  auto pcmuInfo = G711CodecFactory::makePcmuInfo();

  OpusCodecFactory opusFactory(opusInfo);
  G711CodecFactory g711Factory(pcmuInfo);

  // Forward handler: Opus→G.711
  auto fwdHandler = std::make_shared<TranscodingHandler>(
    opusFactory.createDecoder(opusInfo),
    g711Factory.createEncoder(pcmuInfo));
  auto fwdSink = std::make_shared<MockHandler>();
  fwdHandler->addToChain(fwdSink);

  // Reverse handler: G.711→Opus
  auto revHandler = std::make_shared<TranscodingHandler>(
    g711Factory.createDecoder(pcmuInfo),
    opusFactory.createEncoder(opusInfo));
  auto revSink = std::make_shared<MockHandler>();
  revHandler->addToChain(revSink);

  // Final Opus decoder for quality comparison.
  auto finalDecoder = opusFactory.createDecoder(opusInfo);

  // Generate reference: multiple frames of 440Hz sine at 48kHz.
  constexpr int kFrames = 10;
  constexpr int kFrameSamples = 960; // 20ms at 48kHz
  auto reference = generateSineWave(48000, kFrameSamples * kFrames, 440.0, 10000.0);

  auto opusEncoder = opusFactory.createEncoder(opusInfo);

  // Collect round-trip PCM output.
  std::vector<std::int16_t> roundTrip;

  for (int f = 0; f < kFrames; ++f)
  {
    // Encode reference PCM with Opus.
    auto framePcm = wrapPcm(
      reference.data() + f * kFrameSamples,
      static_cast<std::size_t>(kFrameSamples));
    auto opusFrame = opusEncoder->encode(*framePcm);
    REQUIRE(opusFrame);

    // Forward: Opus→G.711
    fwdHandler->incoming(opusFrame);
    if (!fwdSink->lastIncoming)
    {
      continue;
    }

    // Reverse: G.711→Opus
    revHandler->incoming(fwdSink->lastIncoming);
    if (!revSink->lastIncoming)
    {
      continue;
    }

    // Decode final Opus to PCM for comparison.
    auto finalPcm = finalDecoder->decode(*revSink->lastIncoming);
    if (!finalPcm)
    {
      continue;
    }

    auto sampleCount = finalPcm->size() / sizeof(std::int16_t);
    const auto* samples = reinterpret_cast<const std::int16_t*>(finalPcm->data());
    roundTrip.insert(roundTrip.end(), samples, samples + sampleCount);
  }

  REQUIRE(roundTrip.size() > 0);

  // Compute PSNR with alignment search to account for pipeline latency.
  double bestPsnr = -1.0;
  auto refSize = static_cast<int>(reference.size());
  auto rtSize = static_cast<int>(roundTrip.size());
  int compareLen = std::min(refSize, rtSize) - 200;
  REQUIRE(compareLen > 0);

  for (int offset = -200; offset <= 200; ++offset)
  {
    double mse = 0.0;
    int count = 0;
    for (int i = 0; i < compareLen; ++i)
    {
      int ri = i + 200; // skip startup transient in round-trip
      int refI = ri + offset;
      if (refI < 0 || refI >= refSize || ri >= rtSize)
      {
        continue;
      }
      double diff = static_cast<double>(reference[static_cast<std::size_t>(refI)]) -
                    static_cast<double>(roundTrip[static_cast<std::size_t>(ri)]);
      mse += diff * diff;
      count++;
    }
    if (count > 0)
    {
      mse /= count;
      double psnr = (mse > 0.0) ? 10.0 * std::log10(32767.0 * 32767.0 / mse) : 100.0;
      if (psnr > bestPsnr)
      {
        bestPsnr = psnr;
      }
    }
  }

  // Voice telephony: lossy double-transcode with resampling.
  // >10dB PSNR indicates intelligible audio.
  INFO("Best round-trip PSNR: " << bestPsnr << " dB");
  CHECK(bestPsnr > 10.0);
}

// ============================================================================
// Test 8: Multiple frames in sequence
// ============================================================================

TEST_CASE("TranscodingHandler: multiple sequential frames", "[pipeline]")
{
  auto opusInfo = makeMonoOpusInfo();
  auto pcmuInfo = G711CodecFactory::makePcmuInfo();

  OpusCodecFactory opusFactory(opusInfo);
  G711CodecFactory g711Factory(pcmuInfo);

  auto handler = std::make_shared<TranscodingHandler>(
    opusFactory.createDecoder(opusInfo),
    g711Factory.createEncoder(pcmuInfo));
  auto sink = std::make_shared<MockHandler>();
  handler->addToChain(sink);

  auto opusEncoder = opusFactory.createEncoder(opusInfo);
  auto pcm = generateSineWave(48000, 960 * 5, 440.0, 10000.0);

  for (int f = 0; f < 5; ++f)
  {
    auto framePcm = wrapPcm(
      pcm.data() + f * 960, 960);
    auto opusFrame = opusEncoder->encode(*framePcm);
    REQUIRE(opusFrame);
    handler->incoming(opusFrame);
  }

  CHECK(sink->incomingCount == 5);

  // Each output should be a 160-byte G.711 frame.
  REQUIRE(sink->lastIncoming);
  CHECK(sink->lastIncoming->size() == 160);
}

// ============================================================================
// Test 9: swapCodecs
// ============================================================================

TEST_CASE("TranscodingHandler: swapCodecs changes codec pair", "[pipeline]")
{
  auto opusInfo = makeMonoOpusInfo();
  auto pcmuInfo = G711CodecFactory::makePcmuInfo();
  auto pcmaInfo = G711CodecFactory::makePcmaInfo();

  OpusCodecFactory opusFactory(opusInfo);
  G711CodecFactory pcmuFactory(pcmuInfo);
  G711CodecFactory pcmaFactory(pcmaInfo);

  // Start with Opus→PCMU.
  auto handler = std::make_shared<TranscodingHandler>(
    opusFactory.createDecoder(opusInfo),
    pcmuFactory.createEncoder(pcmuInfo));
  auto sink = std::make_shared<MockHandler>();
  handler->addToChain(sink);

  CHECK(handler->encoderInfo().name == "PCMU");
  CHECK(handler->hasResampler());

  // Swap to Opus→PCMA.
  handler->swapCodecs(
    opusFactory.createDecoder(opusInfo),
    pcmaFactory.createEncoder(pcmaInfo));

  CHECK(handler->encoderInfo().name == "PCMA");
  CHECK(handler->hasResampler()); // still 48→8kHz

  // Verify it works after swap.
  auto opusEncoder = opusFactory.createEncoder(opusInfo);
  auto pcm = generateSineWave(48000, 960, 440.0, 10000.0);
  auto opusFrame = encodeWith(*opusEncoder, pcm.data(), pcm.size());
  handler->incoming(opusFrame);

  REQUIRE(sink->lastIncoming);
  CHECK(sink->lastIncoming->payloadType() == 8); // PCMA

  // Swap to same-rate: PCMU→PCMA (removes resampler).
  handler->swapCodecs(
    pcmuFactory.createDecoder(pcmuInfo),
    pcmaFactory.createEncoder(pcmaInfo));

  CHECK_FALSE(handler->hasResampler());
}

// ============================================================================
// Test 10: Constructor with same-rate codecs — no Resampler
// ============================================================================

TEST_CASE("TranscodingHandler: same-rate constructor has no resampler", "[pipeline]")
{
  auto pcmuInfo = G711CodecFactory::makePcmuInfo();
  auto pcmaInfo = G711CodecFactory::makePcmaInfo();

  G711CodecFactory pcmuFactory(pcmuInfo);
  G711CodecFactory pcmaFactory(pcmaInfo);

  TranscodingHandler handler(
    pcmuFactory.createDecoder(pcmuInfo),
    pcmaFactory.createEncoder(pcmaInfo));

  CHECK_FALSE(handler.hasResampler());
}

// ============================================================================
// Test 11: Constructor with different-rate codecs — Resampler created
// ============================================================================

TEST_CASE("TranscodingHandler: different-rate constructor has resampler", "[pipeline]")
{
  auto opusInfo = makeMonoOpusInfo();
  auto pcmuInfo = G711CodecFactory::makePcmuInfo();

  OpusCodecFactory opusFactory(opusInfo);
  G711CodecFactory g711Factory(pcmuInfo);

  TranscodingHandler handler(
    opusFactory.createDecoder(opusInfo),
    g711Factory.createEncoder(pcmuInfo));

  CHECK(handler.hasResampler());
  CHECK(handler.decoderInfo().clockRate == 48000);
  CHECK(handler.encoderInfo().clockRate == 8000);
}

// ============================================================================
// Test 12: Empty/zero-size input buffer
// ============================================================================

TEST_CASE("TranscodingHandler: empty input buffer is dropped", "[pipeline]")
{
  auto pcmuInfo = G711CodecFactory::makePcmuInfo();
  auto pcmaInfo = G711CodecFactory::makePcmaInfo();

  G711CodecFactory pcmuFactory(pcmuInfo);
  G711CodecFactory pcmaFactory(pcmaInfo);

  auto handler = std::make_shared<TranscodingHandler>(
    pcmuFactory.createDecoder(pcmuInfo),
    pcmaFactory.createEncoder(pcmaInfo));
  auto sink = std::make_shared<MockHandler>();
  handler->addToChain(sink);

  // Zero-size buffer.
  auto empty = MediaBuffer::create(100);
  empty->setSize(0);
  handler->incoming(empty);
  CHECK(sink->incomingCount == 0);

  // Null buffer.
  handler->incoming(nullptr);
  CHECK(sink->incomingCount == 0);
}

// ============================================================================
// Test 13: Constructor validation
// ============================================================================

TEST_CASE("TranscodingHandler: null decoder/encoder throws", "[pipeline]")
{
  auto pcmuInfo = G711CodecFactory::makePcmuInfo();
  G711CodecFactory g711Factory(pcmuInfo);

  REQUIRE_THROWS_AS(
    TranscodingHandler(nullptr, g711Factory.createEncoder(pcmuInfo)),
    std::invalid_argument);

  REQUIRE_THROWS_AS(
    TranscodingHandler(g711Factory.createDecoder(pcmuInfo), nullptr),
    std::invalid_argument);
}

// ============================================================================
// Test 14: outgoing() passthrough (unidirectional design)
// ============================================================================

TEST_CASE("TranscodingHandler: outgoing() forwards without processing", "[pipeline]")
{
  auto opusInfo = makeMonoOpusInfo();
  auto pcmuInfo = G711CodecFactory::makePcmuInfo();

  OpusCodecFactory opusFactory(opusInfo);
  G711CodecFactory g711Factory(pcmuInfo);

  auto handler = std::make_shared<TranscodingHandler>(
    opusFactory.createDecoder(opusInfo),
    g711Factory.createEncoder(pcmuInfo));
  auto sink = std::make_shared<MockHandler>();
  handler->addToChain(sink);

  // Create a buffer and call outgoing().
  auto buf = MediaBuffer::create(100);
  buf->setSize(42);
  buf->setPayloadType(99);
  buf->setTimestamp(7777);

  handler->outgoing(buf);

  // Buffer should be forwarded unchanged (no decode/resample/encode).
  REQUIRE(sink->outgoingCount == 1);
  REQUIRE(sink->lastOutgoing == buf); // same pointer — not processed
  CHECK(sink->lastOutgoing->size() == 42);
  CHECK(sink->lastOutgoing->payloadType() == 99);
  CHECK(sink->lastOutgoing->timestamp() == 7777);
}

// ============================================================================
// Test 15: Throughput benchmark
// ============================================================================

TEST_CASE("TranscodingHandler: throughput benchmark Opus to G.711", "[pipeline][!benchmark]")
{
  auto opusInfo = makeMonoOpusInfo();
  auto pcmuInfo = G711CodecFactory::makePcmuInfo();

  OpusCodecFactory opusFactory(opusInfo);
  G711CodecFactory g711Factory(pcmuInfo);

  auto handler = std::make_shared<TranscodingHandler>(
    opusFactory.createDecoder(opusInfo),
    g711Factory.createEncoder(pcmuInfo));
  auto sink = std::make_shared<MockHandler>();
  handler->addToChain(sink);

  // Pre-encode 1000 Opus frames.
  auto opusEncoder = opusFactory.createEncoder(opusInfo);
  auto pcm = generateSineWave(48000, 960, 440.0, 10000.0);
  auto pcmBuf = wrapPcm(pcm.data(), pcm.size());

  std::vector<std::shared_ptr<MediaBuffer>> opusFrames;
  opusFrames.reserve(1000);
  for (int i = 0; i < 1000; ++i)
  {
    auto frame = opusEncoder->encode(*pcmBuf);
    REQUIRE(frame);
    opusFrames.push_back(std::move(frame));
  }

  // Time 1000 transcode operations.
  auto start = std::chrono::steady_clock::now();
  for (int i = 0; i < 1000; ++i)
  {
    handler->incoming(opusFrames[static_cast<std::size_t>(i)]);
  }
  auto end = std::chrono::steady_clock::now();

  auto totalUs = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
  double perFrameUs = static_cast<double>(totalUs) / 1000.0;
  double perFrameMs = perFrameUs / 1000.0;
  double fps = 1000.0 / (static_cast<double>(totalUs) / 1e6);

  INFO("Total: " << totalUs << " us for 1000 frames");
  INFO("Per-frame: " << perFrameMs << " ms");
  INFO("Throughput: " << fps << " frames/sec");

  // Architecture requirement: per-frame time < 50% of frame duration.
  // Frame duration = 20ms, so per-frame transcode must be < 10ms.
  CHECK(perFrameMs < 10.0);

  // All frames should have been processed.
  CHECK(sink->incomingCount == 1000);
}
