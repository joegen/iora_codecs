#include <catch2/catch_test_macros.hpp>

#include "g729/g729_codec.hpp"
#include "g729/g729_codec_factory.hpp"
#include "iora/codecs/codec/codec_registry.hpp"
#include "iora/codecs/core/media_buffer.hpp"

#include <chrono>
#include <cmath>
#include <cstring>
#include <memory>

using namespace iora::codecs;

static constexpr int kFrameSamples = 160; // 20ms at 8kHz
static constexpr int kFrameBytes = 20;    // two 10-byte sub-frames

static std::shared_ptr<MediaBuffer> makePcmFrame(int16_t fillValue = 1000)
{
  auto buf = MediaBuffer::create(kFrameSamples * 2);
  auto* pcm = reinterpret_cast<int16_t*>(buf->data());
  for (int i = 0; i < kFrameSamples; ++i)
  {
    pcm[i] = fillValue;
  }
  buf->setSize(kFrameSamples * 2);
  buf->setTimestamp(12345);
  buf->setSequenceNumber(42);
  return buf;
}

static std::shared_ptr<MediaBuffer> makeSilenceFrame()
{
  auto buf = MediaBuffer::create(kFrameSamples * 2);
  std::memset(buf->data(), 0, kFrameSamples * 2);
  buf->setSize(kFrameSamples * 2);
  return buf;
}

TEST_CASE("G729Codec: encode produces 20-byte output", "[g729]")
{
  auto info = G729CodecFactory::makeG729Info();
  G729Codec encoder(info, G729Mode::Encoder);

  auto pcm = makePcmFrame();
  auto encoded = encoder.encode(*pcm);
  REQUIRE(encoded != nullptr);
  CHECK(encoded->size() == kFrameBytes);
}

TEST_CASE("G729Codec: decode produces 160 S16 samples", "[g729]")
{
  auto info = G729CodecFactory::makeG729Info();
  G729Codec encoder(info, G729Mode::Encoder);
  G729Codec decoder(info, G729Mode::Decoder);

  auto pcm = makePcmFrame();
  auto encoded = encoder.encode(*pcm);
  REQUIRE(encoded != nullptr);

  auto decoded = decoder.decode(*encoded);
  REQUIRE(decoded != nullptr);
  CHECK(decoded->size() == kFrameSamples * 2);
}

TEST_CASE("G729Codec: encode/decode round-trip", "[g729]")
{
  auto info = G729CodecFactory::makeG729Info();
  G729Codec encoder(info, G729Mode::Encoder);
  G729Codec decoder(info, G729Mode::Decoder);

  auto pcm = makePcmFrame(5000);
  auto encoded = encoder.encode(*pcm);
  REQUIRE(encoded != nullptr);

  auto decoded = decoder.decode(*encoded);
  REQUIRE(decoded != nullptr);
  CHECK(decoded->size() == kFrameSamples * 2);

  // Verify output has energy (not all zeros)
  auto* samples = reinterpret_cast<const int16_t*>(decoded->data());
  double rms = 0;
  for (int i = 0; i < kFrameSamples; ++i)
  {
    rms += static_cast<double>(samples[i]) * samples[i];
  }
  rms = std::sqrt(rms / kFrameSamples);
  CHECK(rms > 10.0);
}

TEST_CASE("G729Codec: CodecInfo fields", "[g729]")
{
  auto info = G729CodecFactory::makeG729Info();
  CHECK(info.name == "G729");
  CHECK(info.type == CodecType::Audio);
  CHECK(info.mediaSubtype == "G729");
  CHECK(info.clockRate == 8000);
  CHECK(info.channels == 1);
  CHECK(info.defaultPayloadType == 18);
  CHECK(info.defaultBitrate == 8000);
  CHECK(info.frameSize == std::chrono::microseconds{20000});
  CHECK((info.features & CodecFeatures::Plc) != CodecFeatures::None);
  CHECK((info.features & CodecFeatures::Cbr) != CodecFeatures::None);
}

TEST_CASE("G729Codec: PLC produces valid concealment frame", "[g729]")
{
  auto info = G729CodecFactory::makeG729Info();
  G729Codec decoder(info, G729Mode::Decoder);

  // Feed a real frame first to prime the decoder state
  G729Codec encoder(info, G729Mode::Encoder);
  auto pcm = makePcmFrame(8000);
  auto encoded = encoder.encode(*pcm);
  decoder.decode(*encoded);

  // Now request PLC
  auto plcBuf = decoder.plc(kFrameSamples);
  REQUIRE(plcBuf != nullptr);
  CHECK(plcBuf->size() == kFrameSamples * 2);
}

TEST_CASE("G729Codec: factory supports() matching", "[g729]")
{
  G729CodecFactory factory(G729CodecFactory::makeG729Info());

  CodecInfo match;
  match.name = "G729";
  match.clockRate = 8000;
  match.channels = 1;
  CHECK(factory.supports(match));

  CodecInfo wrongName = match;
  wrongName.name = "opus";
  CHECK_FALSE(factory.supports(wrongName));

  CodecInfo wrongRate = match;
  wrongRate.clockRate = 48000;
  CHECK_FALSE(factory.supports(wrongRate));

  CodecInfo wrongChannels = match;
  wrongChannels.channels = 2;
  CHECK_FALSE(factory.supports(wrongChannels));
}

TEST_CASE("G729Codec: factory createEncoder/createDecoder", "[g729]")
{
  G729CodecFactory factory(G729CodecFactory::makeG729Info());
  auto info = G729CodecFactory::makeG729Info();

  auto encoder = factory.createEncoder(info);
  REQUIRE(encoder != nullptr);
  CHECK(encoder->info().name == "G729");

  auto decoder = factory.createDecoder(info);
  CHECK(decoder != nullptr);
}

TEST_CASE("G729Codec: factory rejects unsupported params", "[g729]")
{
  G729CodecFactory factory(G729CodecFactory::makeG729Info());

  CodecInfo bad;
  bad.name = "PCMU";
  bad.clockRate = 8000;
  bad.channels = 1;

  CHECK(factory.createEncoder(bad) == nullptr);
  CHECK(factory.createDecoder(bad) == nullptr);
}

TEST_CASE("G729Codec: multiple sequential frames", "[g729]")
{
  auto info = G729CodecFactory::makeG729Info();
  G729Codec encoder(info, G729Mode::Encoder);
  G729Codec decoder(info, G729Mode::Decoder);

  for (int i = 0; i < 50; ++i)
  {
    auto pcm = makePcmFrame(static_cast<int16_t>(1000 + i * 100));
    auto encoded = encoder.encode(*pcm);
    REQUIRE(encoded != nullptr);
    REQUIRE(encoded->size() == kFrameBytes);

    auto decoded = decoder.decode(*encoded);
    REQUIRE(decoded != nullptr);
    REQUIRE(decoded->size() == kFrameSamples * 2);
  }
}

TEST_CASE("G729Codec: silence encode/decode", "[g729]")
{
  auto info = G729CodecFactory::makeG729Info();
  G729Codec encoder(info, G729Mode::Encoder);
  G729Codec decoder(info, G729Mode::Decoder);

  auto silence = makeSilenceFrame();
  auto encoded = encoder.encode(*silence);
  REQUIRE(encoded != nullptr);
  REQUIRE(encoded->size() == kFrameBytes);

  auto decoded = decoder.decode(*encoded);
  REQUIRE(decoded != nullptr);
  REQUIRE(decoded->size() == kFrameSamples * 2);
}

TEST_CASE("G729Codec: metadata copied through encode/decode", "[g729]")
{
  auto info = G729CodecFactory::makeG729Info();
  G729Codec encoder(info, G729Mode::Encoder);
  G729Codec decoder(info, G729Mode::Decoder);

  auto pcm = makePcmFrame();
  pcm->setTimestamp(99999);
  pcm->setSequenceNumber(77);

  auto encoded = encoder.encode(*pcm);
  REQUIRE(encoded != nullptr);
  CHECK(encoded->timestamp() == 99999);
  CHECK(encoded->sequenceNumber() == 77);

  auto decoded = decoder.decode(*encoded);
  REQUIRE(decoded != nullptr);
  CHECK(decoded->timestamp() == 99999);
  CHECK(decoded->sequenceNumber() == 77);
}

TEST_CASE("G729Codec: setParameter returns false", "[g729]")
{
  auto info = G729CodecFactory::makeG729Info();
  G729Codec encoder(info, G729Mode::Encoder);
  CHECK_FALSE(encoder.setParameter("bitrate", 8000));
  CHECK_FALSE(encoder.setParameter("anything", 1));
}

TEST_CASE("G729Codec: encoder plc() returns nullptr", "[g729]")
{
  auto info = G729CodecFactory::makeG729Info();
  G729Codec encoder(info, G729Mode::Encoder);
  REQUIRE(encoder.plc(kFrameSamples) == nullptr);
}

TEST_CASE("G729Codec: wrong-mode operations return nullptr", "[g729]")
{
  auto info = G729CodecFactory::makeG729Info();
  G729Codec encoder(info, G729Mode::Encoder);
  G729Codec decoder(info, G729Mode::Decoder);

  auto pcm = makePcmFrame();
  auto encoded = encoder.encode(*pcm);
  REQUIRE(encoded != nullptr);

  CHECK(decoder.encode(*pcm) == nullptr);
  CHECK(encoder.decode(*encoded) == nullptr);
}

TEST_CASE("G729Codec: undersized input returns nullptr", "[g729]")
{
  auto info = G729CodecFactory::makeG729Info();
  G729Codec encoder(info, G729Mode::Encoder);
  G729Codec decoder(info, G729Mode::Decoder);

  // Empty buffer
  auto empty = MediaBuffer::create(0);
  empty->setSize(0);
  CHECK(encoder.encode(*empty) == nullptr);
  CHECK(decoder.decode(*empty) == nullptr);

  // Too small for a 20ms frame
  auto small = MediaBuffer::create(10);
  small->setSize(10);
  CHECK(encoder.encode(*small) == nullptr);
  CHECK(decoder.decode(*small) == nullptr);
}

TEST_CASE("G729Codec: CodecRegistry integration", "[g729]")
{
  CodecRegistry registry;
  auto factory = std::make_shared<G729CodecFactory>(G729CodecFactory::makeG729Info());
  registry.registerFactory(factory);

  auto found = registry.findByName("G729");
  REQUIRE(found.has_value());
  REQUIRE(found->name == "G729");
  REQUIRE(found->defaultPayloadType == 18);

  auto encoder = registry.createEncoder(*found);
  REQUIRE(encoder != nullptr);

  auto decoder = registry.createDecoder(*found);
  REQUIRE(decoder != nullptr);

  registry.unregisterFactory("G729");
  auto notFound = registry.findByName("G729");
  REQUIRE_FALSE(notFound.has_value());
}
