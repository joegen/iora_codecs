#include <catch2/catch_test_macros.hpp>

#include "g711/g711_codec.hpp"
#include "g711/g711_codec_factory.hpp"
#include "iora/codecs/codec/codec_registry.hpp"

#include <cmath>
#include <cstring>
#include <vector>

using namespace iora::codecs;
using namespace std::chrono_literals;

// ============================================================================
// Helper: create a MediaBuffer filled with S16 PCM samples
// ============================================================================
static std::shared_ptr<MediaBuffer> makePcmBuffer(const std::vector<std::int16_t>& samples)
{
  std::size_t bytes = samples.size() * 2;
  auto buf = MediaBuffer::create(bytes);
  std::memcpy(buf->data(), samples.data(), bytes);
  buf->setSize(bytes);
  return buf;
}

// ============================================================================
// PCMU round-trip
// ============================================================================
TEST_CASE("G711Codec: PCMU encode round-trip", "[g711][codec]")
{
  G711Codec codec(G711CodecFactory::makePcmuInfo());

  // A range of representative S16 values
  std::vector<std::int16_t> input = {0, 100, -100, 1000, -1000, 10000, -10000, 32767, -32768};
  auto pcmIn = makePcmBuffer(input);
  auto encoded = codec.encode(*pcmIn);
  auto decoded = codec.decode(*encoded);

  REQUIRE(decoded->size() == pcmIn->size());

  const auto* original = reinterpret_cast<const std::int16_t*>(pcmIn->data());
  const auto* result = reinterpret_cast<const std::int16_t*>(decoded->data());

  for (std::size_t i = 0; i < input.size(); ++i)
  {
    // G.711 mu-law quantization: decoded values within tolerance of original
    // Tolerance scales with magnitude (logarithmic compression)
    int tolerance = std::abs(original[i]) / 16 + 8;
    INFO("sample[" << i << "]: original=" << original[i] << " decoded=" << result[i]);
    CHECK(std::abs(static_cast<int>(result[i]) - static_cast<int>(original[i])) <= tolerance);
  }
}

// ============================================================================
// PCMA round-trip
// ============================================================================
TEST_CASE("G711Codec: PCMA encode round-trip", "[g711][codec]")
{
  G711Codec codec(G711CodecFactory::makePcmaInfo());

  std::vector<std::int16_t> input = {0, 100, -100, 1000, -1000, 10000, -10000, 32767, -32768};
  auto pcmIn = makePcmBuffer(input);
  auto encoded = codec.encode(*pcmIn);
  auto decoded = codec.decode(*encoded);

  REQUIRE(decoded->size() == pcmIn->size());

  const auto* original = reinterpret_cast<const std::int16_t*>(pcmIn->data());
  const auto* result = reinterpret_cast<const std::int16_t*>(decoded->data());

  for (std::size_t i = 0; i < input.size(); ++i)
  {
    int tolerance = std::abs(original[i]) / 16 + 16;
    INFO("sample[" << i << "]: original=" << original[i] << " decoded=" << result[i]);
    CHECK(std::abs(static_cast<int>(result[i]) - static_cast<int>(original[i])) <= tolerance);
  }
}

// ============================================================================
// PCMU CodecInfo fields
// ============================================================================
TEST_CASE("G711Codec: PCMU CodecInfo fields", "[g711][codec]")
{
  auto info = G711CodecFactory::makePcmuInfo();
  CHECK(info.name == "PCMU");
  CHECK(info.type == CodecType::Audio);
  CHECK(info.mediaSubtype == "PCMU");
  CHECK(info.clockRate == 8000);
  CHECK(info.channels == 1);
  CHECK(info.defaultPayloadType == 0);
  CHECK(info.defaultBitrate == 64000);
  CHECK(info.frameSize == 20000us);
  CHECK(hasFeature(info.features, CodecFeatures::Cbr));
}

// ============================================================================
// PCMA CodecInfo fields
// ============================================================================
TEST_CASE("G711Codec: PCMA CodecInfo fields", "[g711][codec]")
{
  auto info = G711CodecFactory::makePcmaInfo();
  CHECK(info.name == "PCMA");
  CHECK(info.type == CodecType::Audio);
  CHECK(info.mediaSubtype == "PCMA");
  CHECK(info.clockRate == 8000);
  CHECK(info.channels == 1);
  CHECK(info.defaultPayloadType == 8);
  CHECK(info.defaultBitrate == 64000);
  CHECK(info.frameSize == 20000us);
  CHECK(hasFeature(info.features, CodecFeatures::Cbr));
}

// ============================================================================
// Encode output size
// ============================================================================
TEST_CASE("G711Codec: encode output size = input sample count", "[g711][codec]")
{
  G711Codec codec(G711CodecFactory::makePcmuInfo());

  constexpr std::size_t numSamples = 160; // 20ms at 8kHz
  std::vector<std::int16_t> input(numSamples, 1234);
  auto pcm = makePcmBuffer(input);

  auto encoded = codec.encode(*pcm);
  CHECK(encoded->size() == numSamples);
}

// ============================================================================
// Decode output size
// ============================================================================
TEST_CASE("G711Codec: decode output size = input bytes * 2", "[g711][codec]")
{
  G711Codec codec(G711CodecFactory::makePcmuInfo());

  constexpr std::size_t compressedBytes = 160;
  auto compressed = MediaBuffer::create(compressedBytes);
  std::memset(compressed->data(), 0xFF, compressedBytes);
  compressed->setSize(compressedBytes);

  auto decoded = codec.decode(*compressed);
  CHECK(decoded->size() == compressedBytes * 2);
}

// ============================================================================
// PLC produces zero-filled buffer
// ============================================================================
TEST_CASE("G711Codec: PLC produces zero-filled buffer", "[g711][codec]")
{
  G711Codec codec(G711CodecFactory::makePcmuInfo());

  constexpr std::size_t frameSamples = 160;
  auto plcBuf = codec.plc(frameSamples);

  REQUIRE(plcBuf->size() == frameSamples * 2);

  const auto* pcm = reinterpret_cast<const std::int16_t*>(plcBuf->data());
  for (std::size_t i = 0; i < frameSamples; ++i)
  {
    CHECK(pcm[i] == 0);
  }
}

// ============================================================================
// Factory supports()
// ============================================================================
TEST_CASE("G711CodecFactory: supports matching info", "[g711][factory]")
{
  G711CodecFactory pcmuFactory(G711CodecFactory::makePcmuInfo());
  G711CodecFactory pcmaFactory(G711CodecFactory::makePcmaInfo());

  CHECK(pcmuFactory.supports(G711CodecFactory::makePcmuInfo()));
  CHECK(pcmaFactory.supports(G711CodecFactory::makePcmaInfo()));

  CHECK_FALSE(pcmuFactory.supports(G711CodecFactory::makePcmaInfo()));
  CHECK_FALSE(pcmaFactory.supports(G711CodecFactory::makePcmuInfo()));

  // Non-G.711 codec
  CodecInfo opusInfo;
  opusInfo.name = "opus";
  opusInfo.clockRate = 48000;
  CHECK_FALSE(pcmuFactory.supports(opusInfo));
}

// ============================================================================
// Factory createEncoder/createDecoder
// ============================================================================
TEST_CASE("G711CodecFactory: createEncoder and createDecoder", "[g711][factory]")
{
  G711CodecFactory factory(G711CodecFactory::makePcmuInfo());

  auto encoder = factory.createEncoder(G711CodecFactory::makePcmuInfo());
  REQUIRE(encoder != nullptr);
  CHECK(encoder->info().name == "PCMU");

  auto decoder = factory.createDecoder(G711CodecFactory::makePcmuInfo());
  REQUIRE(decoder != nullptr);
  CHECK(decoder->info().name == "PCMU");
}

// ============================================================================
// 160 samples (20ms at 8kHz)
// ============================================================================
TEST_CASE("G711Codec: encode/decode 160 samples (20ms frame)", "[g711][codec]")
{
  G711Codec pcmu(G711CodecFactory::makePcmuInfo());
  G711Codec pcma(G711CodecFactory::makePcmaInfo());

  // Generate a simple sine-like pattern
  std::vector<std::int16_t> input(160);
  for (int i = 0; i < 160; ++i)
  {
    input[i] = static_cast<std::int16_t>(10000.0 * std::sin(2.0 * 3.14159265 * 400.0 * i / 8000.0));
  }
  auto pcm = makePcmBuffer(input);

  // PCMU
  auto encMulaw = pcmu.encode(*pcm);
  CHECK(encMulaw->size() == 160);
  auto decMulaw = pcmu.decode(*encMulaw);
  CHECK(decMulaw->size() == 320);

  // PCMA
  auto encAlaw = pcma.encode(*pcm);
  CHECK(encAlaw->size() == 160);
  auto decAlaw = pcma.decode(*encAlaw);
  CHECK(decAlaw->size() == 320);
}

// ============================================================================
// Silence encode (all zeros)
// ============================================================================
TEST_CASE("G711Codec: silence encode/decode", "[g711][codec]")
{
  G711Codec pcmu(G711CodecFactory::makePcmuInfo());
  G711Codec pcma(G711CodecFactory::makePcmaInfo());

  std::vector<std::int16_t> silence(160, 0);
  auto pcm = makePcmBuffer(silence);

  auto encMulaw = pcmu.encode(*pcm);
  auto decMulaw = pcmu.decode(*encMulaw);
  const auto* muResult = reinterpret_cast<const std::int16_t*>(decMulaw->data());
  for (std::size_t i = 0; i < 160; ++i)
  {
    CHECK(std::abs(static_cast<int>(muResult[i])) <= 8);
  }

  auto encAlaw = pcma.encode(*pcm);
  auto decAlaw = pcma.decode(*encAlaw);
  const auto* alResult = reinterpret_cast<const std::int16_t*>(decAlaw->data());
  for (std::size_t i = 0; i < 160; ++i)
  {
    CHECK(std::abs(static_cast<int>(alResult[i])) <= 16);
  }
}

// ============================================================================
// Full-scale signal
// ============================================================================
TEST_CASE("G711Codec: full-scale signal encode/decode", "[g711][codec]")
{
  G711Codec pcmu(G711CodecFactory::makePcmuInfo());

  std::vector<std::int16_t> input = {32767, -32768, 32767, -32768};
  auto pcm = makePcmBuffer(input);

  auto encoded = pcmu.encode(*pcm);
  auto decoded = pcmu.decode(*encoded);

  const auto* result = reinterpret_cast<const std::int16_t*>(decoded->data());
  // Full-scale values are clipped to mu-law's max range (32124)
  CHECK(result[0] > 30000);
  CHECK(result[1] < -30000);
  CHECK(result[2] > 30000);
  CHECK(result[3] < -30000);
}

// ============================================================================
// Metadata copied through encode/decode
// ============================================================================
TEST_CASE("G711Codec: metadata copied through encode/decode", "[g711][codec]")
{
  G711Codec codec(G711CodecFactory::makePcmuInfo());

  std::vector<std::int16_t> input(160, 500);
  auto pcm = makePcmBuffer(input);
  pcm->setTimestamp(12345);
  pcm->setSequenceNumber(42);
  pcm->setSsrc(0xDEADBEEF);
  pcm->setPayloadType(0);
  pcm->setMarker(true);

  auto encoded = codec.encode(*pcm);
  CHECK(encoded->timestamp() == 12345);
  CHECK(encoded->sequenceNumber() == 42);
  CHECK(encoded->ssrc() == 0xDEADBEEF);
  CHECK(encoded->payloadType() == 0);
  CHECK(encoded->marker() == true);

  auto decoded = codec.decode(*encoded);
  CHECK(decoded->timestamp() == 12345);
  CHECK(decoded->sequenceNumber() == 42);
  CHECK(decoded->ssrc() == 0xDEADBEEF);
  CHECK(decoded->payloadType() == 0);
  CHECK(decoded->marker() == true);
}

// ============================================================================
// CodecRegistry integration
// ============================================================================
TEST_CASE("G711CodecFactory: CodecRegistry integration", "[g711][registry]")
{
  CodecRegistry registry;

  auto pcmuFactory = std::make_shared<G711CodecFactory>(G711CodecFactory::makePcmuInfo());
  auto pcmaFactory = std::make_shared<G711CodecFactory>(G711CodecFactory::makePcmaInfo());

  registry.registerFactory(pcmuFactory);
  registry.registerFactory(pcmaFactory);

  // findByName
  auto pcmu = registry.findByName("PCMU");
  REQUIRE(pcmu.has_value());
  CHECK(pcmu->clockRate == 8000);
  CHECK(pcmu->defaultPayloadType == 0);

  auto pcma = registry.findByName("PCMA");
  REQUIRE(pcma.has_value());
  CHECK(pcma->clockRate == 8000);
  CHECK(pcma->defaultPayloadType == 8);

  // findByPayloadType
  auto byPt0 = registry.findByPayloadType(0);
  REQUIRE(byPt0.has_value());
  CHECK(byPt0->name == "PCMU");

  auto byPt8 = registry.findByPayloadType(8);
  REQUIRE(byPt8.has_value());
  CHECK(byPt8->name == "PCMA");

  // createEncoder/createDecoder
  auto encoder = registry.createEncoder(G711CodecFactory::makePcmuInfo());
  REQUIRE(encoder != nullptr);
  CHECK(encoder->info().name == "PCMU");

  auto decoder = registry.createDecoder(G711CodecFactory::makePcmaInfo());
  REQUIRE(decoder != nullptr);
  CHECK(decoder->info().name == "PCMA");

  // Enumerate
  auto codecs = registry.enumerateCodecs();
  CHECK(codecs.size() == 2);
}

// ============================================================================
// setParameter / getParameter (no-op for G.711)
// ============================================================================
TEST_CASE("G711Codec: setParameter and getParameter are no-ops", "[g711][codec]")
{
  G711Codec codec(G711CodecFactory::makePcmuInfo());

  CHECK_FALSE(codec.setParameter("bitrate", 64000));
  CHECK(codec.getParameter("bitrate") == 0);
  CHECK(codec.getParameter("anything") == 0);
}

// ============================================================================
// Empty input buffer
// ============================================================================
TEST_CASE("G711Codec: empty input encode/decode", "[g711][codec]")
{
  G711Codec codec(G711CodecFactory::makePcmuInfo());

  auto emptyPcm = MediaBuffer::create(0);
  emptyPcm->setSize(0);

  auto encoded = codec.encode(*emptyPcm);
  CHECK(encoded->size() == 0);

  auto emptyCompressed = MediaBuffer::create(0);
  emptyCompressed->setSize(0);

  auto decoded = codec.decode(*emptyCompressed);
  CHECK(decoded->size() == 0);
}

// ============================================================================
// PCMA full-scale signal
// ============================================================================
TEST_CASE("G711Codec: PCMA full-scale signal encode/decode", "[g711][codec]")
{
  G711Codec codec(G711CodecFactory::makePcmaInfo());

  std::vector<std::int16_t> input = {32767, -32768, 32767, -32768};
  auto pcm = makePcmBuffer(input);

  auto encoded = codec.encode(*pcm);
  auto decoded = codec.decode(*encoded);

  const auto* result = reinterpret_cast<const std::int16_t*>(decoded->data());
  CHECK(result[0] > 30000);
  CHECK(result[1] < -30000);
  CHECK(result[2] > 30000);
  CHECK(result[3] < -30000);
}

// ============================================================================
// Factory supports() rejects mismatched channels
// ============================================================================
TEST_CASE("G711CodecFactory: supports rejects mismatched channels", "[g711][factory]")
{
  G711CodecFactory factory(G711CodecFactory::makePcmuInfo());

  CodecInfo stereo;
  stereo.name = "PCMU";
  stereo.clockRate = 8000;
  stereo.channels = 2;
  CHECK_FALSE(factory.supports(stereo));
}

// ============================================================================
// Factory createEncoder/createDecoder reject unsupported params
// ============================================================================
TEST_CASE("G711CodecFactory: create rejects unsupported params", "[g711][factory]")
{
  G711CodecFactory factory(G711CodecFactory::makePcmuInfo());

  CodecInfo wrong;
  wrong.name = "opus";
  wrong.clockRate = 48000;
  wrong.channels = 2;

  CHECK(factory.createEncoder(wrong) == nullptr);
  CHECK(factory.createDecoder(wrong) == nullptr);
}

// ============================================================================
// Odd-sized input to encode (trailing byte ignored)
// ============================================================================
TEST_CASE("G711Codec: odd-sized input encode ignores trailing byte", "[g711][codec]")
{
  G711Codec codec(G711CodecFactory::makePcmuInfo());

  // 5 bytes = 2 full S16 samples + 1 trailing byte
  auto buf = MediaBuffer::create(5);
  auto* data = reinterpret_cast<std::int16_t*>(buf->data());
  data[0] = 1000;
  data[1] = -1000;
  buf->data()[4] = 0xAB; // trailing byte
  buf->setSize(5);

  auto encoded = codec.encode(*buf);
  // Should encode 2 samples (5/2 = 2), ignoring trailing byte
  CHECK(encoded->size() == 2);

  auto decoded = codec.decode(*encoded);
  CHECK(decoded->size() == 4);
}
