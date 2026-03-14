#include <catch2/catch_test_macros.hpp>

#include "g722/g722_codec.hpp"
#include "g722/g722_codec_factory.hpp"
#include "iora/codecs/codec/codec_registry.hpp"

#include <cmath>
#include <cstring>
#include <vector>

using namespace iora::codecs;
using namespace std::chrono_literals;

static constexpr double kPi = 3.14159265358979323846;

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
// Helper: generate a sine wave at 16kHz sample rate
// ============================================================================
static std::vector<std::int16_t> generateSineWave(int frameSamples, double frequency, double amplitude)
{
  std::vector<std::int16_t> samples(static_cast<std::size_t>(frameSamples));
  for (int i = 0; i < frameSamples; ++i)
  {
    samples[i] = static_cast<std::int16_t>(
      amplitude * std::sin(2.0 * kPi * frequency * i / 16000.0));
  }
  return samples;
}

// ============================================================================
// Encode/decode round-trip at 64kbps
// ============================================================================
TEST_CASE("G722Codec: encode/decode round-trip at 64kbps", "[g722][codec]")
{
  auto info = G722CodecFactory::makeG722Info();

  G722Codec encoder(info, G722Mode::Encoder, 64000);
  G722Codec decoder(info, G722Mode::Decoder, 64000);

  // 20ms at 16kHz = 320 samples
  auto samples = generateSineWave(320, 1000.0, 10000.0);
  auto pcm = makePcmBuffer(samples);

  auto encoded = encoder.encode(*pcm);
  REQUIRE(encoded != nullptr);
  CHECK(encoded->size() > 0);
  CHECK(encoded->size() <= 160); // 64kbps * 20ms / 8 = 160 bytes

  auto decoded = decoder.decode(*encoded);
  REQUIRE(decoded != nullptr);
  CHECK(decoded->size() == 320 * 2); // 320 samples * 2 bytes

  // Verify decoded output has energy
  const auto* result = reinterpret_cast<const std::int16_t*>(decoded->data());
  double rms = 0.0;
  for (int i = 0; i < 320; ++i)
  {
    rms += static_cast<double>(result[i]) * static_cast<double>(result[i]);
  }
  rms = std::sqrt(rms / 320.0);
  INFO("Decoded RMS = " << rms);
  CHECK(rms > 100.0);
}

// ============================================================================
// Encode/decode round-trip at 56kbps
// ============================================================================
TEST_CASE("G722Codec: encode/decode round-trip at 56kbps", "[g722][codec]")
{
  auto info = G722CodecFactory::makeG722Info();

  G722Codec encoder(info, G722Mode::Encoder, 56000);
  G722Codec decoder(info, G722Mode::Decoder, 56000);

  auto samples = generateSineWave(320, 1000.0, 10000.0);
  auto pcm = makePcmBuffer(samples);

  auto encoded = encoder.encode(*pcm);
  REQUIRE(encoded != nullptr);
  CHECK(encoded->size() > 0);

  auto decoded = decoder.decode(*encoded);
  REQUIRE(decoded != nullptr);
  CHECK(decoded->size() == 320 * 2);

  const auto* result = reinterpret_cast<const std::int16_t*>(decoded->data());
  double rms = 0.0;
  for (int i = 0; i < 320; ++i)
  {
    rms += static_cast<double>(result[i]) * static_cast<double>(result[i]);
  }
  rms = std::sqrt(rms / 320.0);
  INFO("Decoded RMS = " << rms);
  CHECK(rms > 100.0);
}

// ============================================================================
// Encode/decode round-trip at 48kbps
// ============================================================================
TEST_CASE("G722Codec: encode/decode round-trip at 48kbps", "[g722][codec]")
{
  auto info = G722CodecFactory::makeG722Info();

  G722Codec encoder(info, G722Mode::Encoder, 48000);
  G722Codec decoder(info, G722Mode::Decoder, 48000);

  auto samples = generateSineWave(320, 1000.0, 10000.0);
  auto pcm = makePcmBuffer(samples);

  auto encoded = encoder.encode(*pcm);
  REQUIRE(encoded != nullptr);
  CHECK(encoded->size() > 0);

  auto decoded = decoder.decode(*encoded);
  REQUIRE(decoded != nullptr);
  CHECK(decoded->size() == 320 * 2);

  const auto* result = reinterpret_cast<const std::int16_t*>(decoded->data());
  double rms = 0.0;
  for (int i = 0; i < 320; ++i)
  {
    rms += static_cast<double>(result[i]) * static_cast<double>(result[i]);
  }
  rms = std::sqrt(rms / 320.0);
  INFO("Decoded RMS = " << rms);
  CHECK(rms > 100.0);
}

// ============================================================================
// CodecInfo fields
// ============================================================================
TEST_CASE("G722Codec: CodecInfo fields", "[g722][codec]")
{
  auto info = G722CodecFactory::makeG722Info();
  CHECK(info.name == "G722");
  CHECK(info.type == CodecType::Audio);
  CHECK(info.mediaSubtype == "G722");
  CHECK(info.clockRate == 8000); // RTP clock rate per RFC 3551
  CHECK(info.channels == 1);
  CHECK(info.defaultPayloadType == 9);
  CHECK(info.defaultBitrate == 64000);
  CHECK(info.frameSize == 20000us);
  CHECK(info.features == CodecFeatures::Cbr);
}

// ============================================================================
// Compressed frame size at 64kbps
// ============================================================================
TEST_CASE("G722Codec: compressed frame size at 64kbps", "[g722][codec]")
{
  auto info = G722CodecFactory::makeG722Info();
  G722Codec encoder(info, G722Mode::Encoder, 64000);

  auto samples = generateSineWave(320, 1000.0, 10000.0);
  auto pcm = makePcmBuffer(samples);

  auto encoded = encoder.encode(*pcm);
  REQUIRE(encoded != nullptr);
  // At 64kbps: 320 samples -> 160 bytes
  CHECK(encoded->size() == 160);
}

// ============================================================================
// PLC produces zero-filled buffer
// ============================================================================
TEST_CASE("G722Codec: PLC produces zero-filled buffer", "[g722][codec]")
{
  auto info = G722CodecFactory::makeG722Info();
  G722Codec decoder(info, G722Mode::Decoder);

  constexpr std::size_t frameSamples = 320;
  auto plcBuf = decoder.plc(frameSamples);

  REQUIRE(plcBuf != nullptr);
  REQUIRE(plcBuf->size() == frameSamples * 2);

  const auto* pcm = reinterpret_cast<const std::int16_t*>(plcBuf->data());
  for (std::size_t i = 0; i < frameSamples; ++i)
  {
    CHECK(pcm[i] == 0);
  }
}

// ============================================================================
// Factory supports() matching and rejection
// ============================================================================
TEST_CASE("G722CodecFactory: supports matching info", "[g722][factory]")
{
  G722CodecFactory factory(G722CodecFactory::makeG722Info());

  CHECK(factory.supports(G722CodecFactory::makeG722Info()));

  // Wrong name
  CodecInfo wrong;
  wrong.name = "opus";
  wrong.clockRate = 48000;
  wrong.channels = 2;
  CHECK_FALSE(factory.supports(wrong));

  // Wrong clock rate
  CodecInfo wrongRate;
  wrongRate.name = "G722";
  wrongRate.clockRate = 16000;
  wrongRate.channels = 1;
  CHECK_FALSE(factory.supports(wrongRate));

  // Wrong channels
  CodecInfo wrongChannels;
  wrongChannels.name = "G722";
  wrongChannels.clockRate = 8000;
  wrongChannels.channels = 2;
  CHECK_FALSE(factory.supports(wrongChannels));
}

// ============================================================================
// Factory createEncoder/createDecoder
// ============================================================================
TEST_CASE("G722CodecFactory: createEncoder and createDecoder", "[g722][factory]")
{
  G722CodecFactory factory(G722CodecFactory::makeG722Info());

  auto encoder = factory.createEncoder(G722CodecFactory::makeG722Info());
  REQUIRE(encoder != nullptr);
  CHECK(encoder->info().name == "G722");

  auto decoder = factory.createDecoder(G722CodecFactory::makeG722Info());
  REQUIRE(decoder != nullptr);
  CHECK(decoder->info().name == "G722");
}

// ============================================================================
// Factory rejects unsupported params
// ============================================================================
TEST_CASE("G722CodecFactory: create rejects unsupported params", "[g722][factory]")
{
  G722CodecFactory factory(G722CodecFactory::makeG722Info());

  CodecInfo wrong;
  wrong.name = "PCMU";
  wrong.clockRate = 8000;
  wrong.channels = 1;

  CHECK(factory.createEncoder(wrong) == nullptr);
  CHECK(factory.createDecoder(wrong) == nullptr);
}

// ============================================================================
// setParameter mode changes bitrate
// ============================================================================
TEST_CASE("G722Codec: setParameter mode changes bitrate", "[g722][codec]")
{
  auto info = G722CodecFactory::makeG722Info();
  G722Codec encoder(info, G722Mode::Encoder, 64000);

  CHECK(encoder.getParameter("mode") == 64000);

  CHECK(encoder.setParameter("mode", 48000));
  CHECK(encoder.getParameter("mode") == 48000);

  CHECK(encoder.setParameter("mode", 56000));
  CHECK(encoder.getParameter("mode") == 56000);

  CHECK(encoder.setParameter("mode", 64000));
  CHECK(encoder.getParameter("mode") == 64000);

  // Invalid mode
  CHECK_FALSE(encoder.setParameter("mode", 32000));
  CHECK(encoder.getParameter("mode") == 64000); // unchanged

  // Verify encoding works after mode switch
  CHECK(encoder.setParameter("mode", 48000));
  auto samples = generateSineWave(320, 1000.0, 10000.0);
  auto pcm = makePcmBuffer(samples);
  auto encoded = encoder.encode(*pcm);
  REQUIRE(encoded != nullptr);
  CHECK(encoded->size() > 0);
}

// ============================================================================
// getParameter mode on decoder
// ============================================================================
TEST_CASE("G722Codec: getParameter mode on decoder", "[g722][codec]")
{
  auto info = G722CodecFactory::makeG722Info();
  G722Codec decoder(info, G722Mode::Decoder, 64000);

  CHECK(decoder.getParameter("mode") == 64000);
}

// ============================================================================
// Multiple sequential frames
// ============================================================================
TEST_CASE("G722Codec: multiple sequential frames", "[g722][codec]")
{
  auto info = G722CodecFactory::makeG722Info();
  G722Codec encoder(info, G722Mode::Encoder);
  G722Codec decoder(info, G722Mode::Decoder);

  auto samples = generateSineWave(320, 1000.0, 10000.0);
  auto pcm = makePcmBuffer(samples);

  for (int i = 0; i < 50; ++i)
  {
    auto encoded = encoder.encode(*pcm);
    REQUIRE(encoded != nullptr);
    CHECK(encoded->size() > 0);

    auto decoded = decoder.decode(*encoded);
    REQUIRE(decoded != nullptr);
    CHECK(decoded->size() == 320 * 2);
  }
}

// ============================================================================
// Silence encode/decode
// ============================================================================
TEST_CASE("G722Codec: silence encode/decode", "[g722][codec]")
{
  auto info = G722CodecFactory::makeG722Info();
  G722Codec encoder(info, G722Mode::Encoder);
  G722Codec decoder(info, G722Mode::Decoder);

  std::vector<std::int16_t> silence(320, 0);
  auto pcm = makePcmBuffer(silence);

  auto encoded = encoder.encode(*pcm);
  REQUIRE(encoded != nullptr);

  auto decoded = decoder.decode(*encoded);
  REQUIRE(decoded != nullptr);
  CHECK(decoded->size() == 320 * 2);

  const auto* result = reinterpret_cast<const std::int16_t*>(decoded->data());
  double rms = 0.0;
  for (std::size_t i = 0; i < 320; ++i)
  {
    rms += static_cast<double>(result[i]) * static_cast<double>(result[i]);
  }
  rms = std::sqrt(rms / 320.0);
  INFO("Silence RMS = " << rms);
  CHECK(rms < 500.0);
}

// ============================================================================
// Metadata copied through encode/decode
// ============================================================================
TEST_CASE("G722Codec: metadata copied through encode/decode", "[g722][codec]")
{
  auto info = G722CodecFactory::makeG722Info();
  G722Codec encoder(info, G722Mode::Encoder);
  G722Codec decoder(info, G722Mode::Decoder);

  auto samples = generateSineWave(320, 1000.0, 10000.0);
  auto pcm = makePcmBuffer(samples);
  pcm->setTimestamp(12345);
  pcm->setSequenceNumber(42);
  pcm->setSsrc(0xDEADBEEF);
  pcm->setPayloadType(9);
  pcm->setMarker(true);

  auto encoded = encoder.encode(*pcm);
  REQUIRE(encoded != nullptr);
  CHECK(encoded->timestamp() == 12345);
  CHECK(encoded->sequenceNumber() == 42);
  CHECK(encoded->ssrc() == 0xDEADBEEF);
  CHECK(encoded->payloadType() == 9);
  CHECK(encoded->marker() == true);

  auto decoded = decoder.decode(*encoded);
  REQUIRE(decoded != nullptr);
  CHECK(decoded->timestamp() == 12345);
  CHECK(decoded->sequenceNumber() == 42);
  CHECK(decoded->ssrc() == 0xDEADBEEF);
  CHECK(decoded->payloadType() == 9);
  CHECK(decoded->marker() == true);
}

// ============================================================================
// CodecRegistry integration
// ============================================================================
TEST_CASE("G722CodecFactory: CodecRegistry integration", "[g722][registry]")
{
  CodecRegistry registry;

  auto factory = std::make_shared<G722CodecFactory>(G722CodecFactory::makeG722Info());
  registry.registerFactory(factory);

  auto byName = registry.findByName("G722");
  REQUIRE(byName.has_value());
  CHECK(byName->clockRate == 8000);
  CHECK(byName->channels == 1);
  CHECK(byName->defaultPayloadType == 9);

  auto byPt = registry.findByPayloadType(9);
  REQUIRE(byPt.has_value());
  CHECK(byPt->name == "G722");

  auto encoder = registry.createEncoder(G722CodecFactory::makeG722Info());
  REQUIRE(encoder != nullptr);
  CHECK(encoder->info().name == "G722");

  auto decoder = registry.createDecoder(G722CodecFactory::makeG722Info());
  REQUIRE(decoder != nullptr);
  CHECK(decoder->info().name == "G722");

  auto codecs = registry.enumerateCodecs();
  CHECK(codecs.size() == 1);
}

// ============================================================================
// setParameter on decoder returns false
// ============================================================================
TEST_CASE("G722Codec: setParameter on decoder returns false", "[g722][codec]")
{
  auto info = G722CodecFactory::makeG722Info();
  G722Codec decoder(info, G722Mode::Decoder);

  CHECK_FALSE(decoder.setParameter("mode", 48000));
}

// ============================================================================
// Unknown parameter returns false
// ============================================================================
TEST_CASE("G722Codec: unknown parameter returns false", "[g722][codec]")
{
  auto info = G722CodecFactory::makeG722Info();
  G722Codec encoder(info, G722Mode::Encoder);

  CHECK_FALSE(encoder.setParameter("nonexistent", 42));
  CHECK(encoder.getParameter("nonexistent") == 0);
}

// ============================================================================
// Wrong-mode operations return nullptr
// ============================================================================
TEST_CASE("G722Codec: wrong-mode operations return nullptr", "[g722][codec]")
{
  auto info = G722CodecFactory::makeG722Info();
  G722Codec encoder(info, G722Mode::Encoder);
  G722Codec decoder(info, G722Mode::Decoder);

  auto samples = generateSineWave(320, 1000.0, 10000.0);
  auto pcm = makePcmBuffer(samples);

  auto encoded = encoder.encode(*pcm);
  REQUIRE(encoded != nullptr);

  auto decoded = decoder.decode(*encoded);
  REQUIRE(decoded != nullptr);

  // encode on decoder returns nullptr
  CHECK(decoder.encode(*pcm) == nullptr);

  // decode on encoder returns nullptr
  CHECK(encoder.decode(*encoded) == nullptr);
}

// ============================================================================
// plc on encoder returns nullptr
// ============================================================================
TEST_CASE("G722Codec: plc on encoder returns nullptr", "[g722][codec]")
{
  auto info = G722CodecFactory::makeG722Info();
  G722Codec encoder(info, G722Mode::Encoder);

  CHECK(encoder.plc(320) == nullptr);
}

// ============================================================================
// Zero-sample encode returns nullptr
// ============================================================================
TEST_CASE("G722Codec: zero-sample encode returns nullptr", "[g722][codec]")
{
  auto info = G722CodecFactory::makeG722Info();
  G722Codec encoder(info, G722Mode::Encoder);

  auto empty = MediaBuffer::create(0);
  empty->setSize(0);

  auto result = encoder.encode(*empty);
  CHECK(result == nullptr);
}
