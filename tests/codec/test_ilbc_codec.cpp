#include <catch2/catch_test_macros.hpp>

#include "ilbc/ilbc_codec.hpp"
#include "ilbc/ilbc_codec_factory.hpp"
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
// Helper: generate a sine wave at 8kHz sample rate
// ============================================================================
static std::vector<std::int16_t> generateSineWave(int frameSamples, double frequency, double amplitude)
{
  std::vector<std::int16_t> samples(static_cast<std::size_t>(frameSamples));
  for (int i = 0; i < frameSamples; ++i)
  {
    samples[i] = static_cast<std::int16_t>(
      amplitude * std::sin(2.0 * kPi * frequency * i / 8000.0));
  }
  return samples;
}

// ============================================================================
// Encode/decode round-trip at 30ms mode
// ============================================================================
TEST_CASE("IlbcCodec: encode/decode round-trip 30ms", "[ilbc][codec]")
{
  auto info = IlbcCodecFactory::makeIlbcInfo();

  IlbcCodec encoder(info, IlbcMode::Encoder, 30);
  IlbcCodec decoder(info, IlbcMode::Decoder, 30);

  // 30ms at 8kHz = 240 samples
  auto samples = generateSineWave(240, 400.0, 10000.0);
  auto pcm = makePcmBuffer(samples);

  auto encoded = encoder.encode(*pcm);
  REQUIRE(encoded != nullptr);
  CHECK(encoded->size() > 0);
  CHECK(encoded->size() <= 50); // 30ms mode: 50 bytes

  auto decoded = decoder.decode(*encoded);
  REQUIRE(decoded != nullptr);
  CHECK(decoded->size() == 240 * 2); // 240 samples * 2 bytes

  const auto* result = reinterpret_cast<const std::int16_t*>(decoded->data());
  double rms = 0.0;
  for (int i = 0; i < 240; ++i)
  {
    rms += static_cast<double>(result[i]) * static_cast<double>(result[i]);
  }
  rms = std::sqrt(rms / 240.0);
  INFO("Decoded RMS = " << rms);
  CHECK(rms > 100.0);
}

// ============================================================================
// Encode/decode round-trip at 20ms mode
// ============================================================================
TEST_CASE("IlbcCodec: encode/decode round-trip 20ms", "[ilbc][codec]")
{
  auto info = IlbcCodecFactory::makeIlbcInfo();

  IlbcCodec encoder(info, IlbcMode::Encoder, 20);
  IlbcCodec decoder(info, IlbcMode::Decoder, 20);

  // 20ms at 8kHz = 160 samples
  auto samples = generateSineWave(160, 400.0, 10000.0);
  auto pcm = makePcmBuffer(samples);

  auto encoded = encoder.encode(*pcm);
  REQUIRE(encoded != nullptr);
  CHECK(encoded->size() > 0);
  CHECK(encoded->size() <= 38); // 20ms mode: 38 bytes

  auto decoded = decoder.decode(*encoded);
  REQUIRE(decoded != nullptr);
  CHECK(decoded->size() == 160 * 2);

  const auto* result = reinterpret_cast<const std::int16_t*>(decoded->data());
  double rms = 0.0;
  for (int i = 0; i < 160; ++i)
  {
    rms += static_cast<double>(result[i]) * static_cast<double>(result[i]);
  }
  rms = std::sqrt(rms / 160.0);
  INFO("Decoded RMS = " << rms);
  CHECK(rms > 100.0);
}

// ============================================================================
// CodecInfo fields
// ============================================================================
TEST_CASE("IlbcCodec: CodecInfo fields", "[ilbc][codec]")
{
  auto info = IlbcCodecFactory::makeIlbcInfo();
  CHECK(info.name == "iLBC");
  CHECK(info.type == CodecType::Audio);
  CHECK(info.mediaSubtype == "iLBC");
  CHECK(info.clockRate == 8000);
  CHECK(info.channels == 1);
  CHECK(info.defaultPayloadType == 0); // dynamic
  CHECK(info.defaultBitrate == 13330);
  CHECK(info.frameSize == 30000us);
  CHECK(hasFeature(info.features, CodecFeatures::Plc));
  CHECK(hasFeature(info.features, CodecFeatures::Cbr));
}

// ============================================================================
// Compressed frame size at 30ms
// ============================================================================
TEST_CASE("IlbcCodec: compressed frame size 30ms", "[ilbc][codec]")
{
  auto info = IlbcCodecFactory::makeIlbcInfo();
  IlbcCodec encoder(info, IlbcMode::Encoder, 30);

  auto samples = generateSineWave(240, 400.0, 10000.0);
  auto pcm = makePcmBuffer(samples);

  auto encoded = encoder.encode(*pcm);
  REQUIRE(encoded != nullptr);
  CHECK(encoded->size() == 50); // 30ms mode: exactly 50 bytes
}

// ============================================================================
// Compressed frame size at 20ms
// ============================================================================
TEST_CASE("IlbcCodec: compressed frame size 20ms", "[ilbc][codec]")
{
  auto info = IlbcCodecFactory::makeIlbcInfo();
  IlbcCodec encoder(info, IlbcMode::Encoder, 20);

  auto samples = generateSineWave(160, 400.0, 10000.0);
  auto pcm = makePcmBuffer(samples);

  auto encoded = encoder.encode(*pcm);
  REQUIRE(encoded != nullptr);
  CHECK(encoded->size() == 38); // 20ms mode: exactly 38 bytes
}

// ============================================================================
// PLC via built-in WebRtcIlbcfix_DecodePlc
// ============================================================================
TEST_CASE("IlbcCodec: PLC produces output", "[ilbc][codec]")
{
  auto info = IlbcCodecFactory::makeIlbcInfo();
  IlbcCodec decoder(info, IlbcMode::Decoder, 30);

  constexpr std::size_t frameSamples = 240;
  auto plcBuf = decoder.plc(frameSamples);

  REQUIRE(plcBuf != nullptr);
  // PLC output should be frameSamples * 2 bytes (or close to it)
  CHECK(plcBuf->size() > 0);
  CHECK(plcBuf->size() <= frameSamples * 2);
}

// ============================================================================
// PLC after decode uses decoder state
// ============================================================================
TEST_CASE("IlbcCodec: PLC after decode produces non-zero output", "[ilbc][codec]")
{
  auto info = IlbcCodecFactory::makeIlbcInfo();
  IlbcCodec encoder(info, IlbcMode::Encoder, 30);
  IlbcCodec decoder(info, IlbcMode::Decoder, 30);

  // Feed a real frame first to populate decoder state
  auto samples = generateSineWave(240, 400.0, 10000.0);
  auto pcm = makePcmBuffer(samples);
  auto encoded = encoder.encode(*pcm);
  REQUIRE(encoded != nullptr);

  auto decoded = decoder.decode(*encoded);
  REQUIRE(decoded != nullptr);

  // Now PLC should use the decoded state for concealment
  auto plcBuf = decoder.plc(240);
  REQUIRE(plcBuf != nullptr);
  CHECK(plcBuf->size() > 0);

  const auto* plcPcm = reinterpret_cast<const std::int16_t*>(plcBuf->data());
  double rms = 0.0;
  std::size_t plcSamples = plcBuf->size() / 2;
  for (std::size_t i = 0; i < plcSamples; ++i)
  {
    rms += static_cast<double>(plcPcm[i]) * static_cast<double>(plcPcm[i]);
  }
  rms = std::sqrt(rms / static_cast<double>(plcSamples));
  INFO("PLC RMS after decode = " << rms);
  CHECK(rms > 10.0); // Should have non-trivial energy from concealment
}

// ============================================================================
// Factory supports() matching and rejection
// ============================================================================
TEST_CASE("IlbcCodecFactory: supports matching info", "[ilbc][factory]")
{
  IlbcCodecFactory factory(IlbcCodecFactory::makeIlbcInfo());

  CHECK(factory.supports(IlbcCodecFactory::makeIlbcInfo()));

  // Wrong name
  CodecInfo wrong;
  wrong.name = "opus";
  wrong.clockRate = 48000;
  wrong.channels = 2;
  CHECK_FALSE(factory.supports(wrong));

  // Wrong clock rate
  CodecInfo wrongRate;
  wrongRate.name = "iLBC";
  wrongRate.clockRate = 16000;
  wrongRate.channels = 1;
  CHECK_FALSE(factory.supports(wrongRate));

  // Wrong channels
  CodecInfo wrongChannels;
  wrongChannels.name = "iLBC";
  wrongChannels.clockRate = 8000;
  wrongChannels.channels = 2;
  CHECK_FALSE(factory.supports(wrongChannels));
}

// ============================================================================
// Factory createEncoder/createDecoder
// ============================================================================
TEST_CASE("IlbcCodecFactory: createEncoder and createDecoder", "[ilbc][factory]")
{
  IlbcCodecFactory factory(IlbcCodecFactory::makeIlbcInfo());

  auto encoder = factory.createEncoder(IlbcCodecFactory::makeIlbcInfo());
  REQUIRE(encoder != nullptr);
  CHECK(encoder->info().name == "iLBC");

  auto decoder = factory.createDecoder(IlbcCodecFactory::makeIlbcInfo());
  REQUIRE(decoder != nullptr);
  CHECK(decoder->info().name == "iLBC");
}

// ============================================================================
// Factory rejects unsupported params
// ============================================================================
TEST_CASE("IlbcCodecFactory: create rejects unsupported params", "[ilbc][factory]")
{
  IlbcCodecFactory factory(IlbcCodecFactory::makeIlbcInfo());

  CodecInfo wrong;
  wrong.name = "PCMU";
  wrong.clockRate = 8000;
  wrong.channels = 1;

  CHECK(factory.createEncoder(wrong) == nullptr);
  CHECK(factory.createDecoder(wrong) == nullptr);
}

// ============================================================================
// setParameter frameMode switches between 20ms and 30ms
// ============================================================================
TEST_CASE("IlbcCodec: setParameter frameMode", "[ilbc][codec]")
{
  auto info = IlbcCodecFactory::makeIlbcInfo();
  IlbcCodec encoder(info, IlbcMode::Encoder, 30);

  CHECK(encoder.getParameter("frameMode") == 30);

  CHECK(encoder.setParameter("frameMode", 20));
  CHECK(encoder.getParameter("frameMode") == 20);

  CHECK(encoder.setParameter("frameMode", 30));
  CHECK(encoder.getParameter("frameMode") == 30);

  // Invalid frame mode
  CHECK_FALSE(encoder.setParameter("frameMode", 10));
  CHECK(encoder.getParameter("frameMode") == 30); // unchanged

  // Verify encoding works after mode switch
  CHECK(encoder.setParameter("frameMode", 20));
  auto samples = generateSineWave(160, 400.0, 10000.0);
  auto pcm = makePcmBuffer(samples);
  auto encoded = encoder.encode(*pcm);
  REQUIRE(encoded != nullptr);
  CHECK(encoded->size() == 38); // 20ms = 38 bytes
}

// ============================================================================
// setParameter frameMode on decoder
// ============================================================================
TEST_CASE("IlbcCodec: setParameter frameMode on decoder", "[ilbc][codec]")
{
  auto info = IlbcCodecFactory::makeIlbcInfo();
  IlbcCodec decoder(info, IlbcMode::Decoder, 30);

  CHECK(decoder.getParameter("frameMode") == 30);

  CHECK(decoder.setParameter("frameMode", 20));
  CHECK(decoder.getParameter("frameMode") == 20);

  // Verify decode works after mode switch to 20ms
  IlbcCodec encoder20(info, IlbcMode::Encoder, 20);
  auto samples = generateSineWave(160, 400.0, 10000.0);
  auto pcm = makePcmBuffer(samples);
  auto encoded = encoder20.encode(*pcm);
  REQUIRE(encoded != nullptr);
  CHECK(encoded->size() == 38);

  auto decoded = decoder.decode(*encoded);
  REQUIRE(decoded != nullptr);
  CHECK(decoded->size() == 160 * 2);
}

// ============================================================================
// Multiple sequential frames
// ============================================================================
TEST_CASE("IlbcCodec: multiple sequential frames", "[ilbc][codec]")
{
  auto info = IlbcCodecFactory::makeIlbcInfo();
  IlbcCodec encoder(info, IlbcMode::Encoder, 30);
  IlbcCodec decoder(info, IlbcMode::Decoder, 30);

  auto samples = generateSineWave(240, 400.0, 10000.0);
  auto pcm = makePcmBuffer(samples);

  for (int i = 0; i < 50; ++i)
  {
    auto encoded = encoder.encode(*pcm);
    REQUIRE(encoded != nullptr);
    CHECK(encoded->size() == 50);

    auto decoded = decoder.decode(*encoded);
    REQUIRE(decoded != nullptr);
    CHECK(decoded->size() == 240 * 2);
  }
}

// ============================================================================
// Silence encode/decode
// ============================================================================
TEST_CASE("IlbcCodec: silence encode/decode", "[ilbc][codec]")
{
  auto info = IlbcCodecFactory::makeIlbcInfo();
  IlbcCodec encoder(info, IlbcMode::Encoder, 30);
  IlbcCodec decoder(info, IlbcMode::Decoder, 30);

  std::vector<std::int16_t> silence(240, 0);
  auto pcm = makePcmBuffer(silence);

  auto encoded = encoder.encode(*pcm);
  REQUIRE(encoded != nullptr);

  auto decoded = decoder.decode(*encoded);
  REQUIRE(decoded != nullptr);
  CHECK(decoded->size() == 240 * 2);

  const auto* result = reinterpret_cast<const std::int16_t*>(decoded->data());
  double rms = 0.0;
  for (std::size_t i = 0; i < 240; ++i)
  {
    rms += static_cast<double>(result[i]) * static_cast<double>(result[i]);
  }
  rms = std::sqrt(rms / 240.0);
  INFO("Silence RMS = " << rms);
  CHECK(rms < 500.0);
}

// ============================================================================
// Metadata copied through encode/decode
// ============================================================================
TEST_CASE("IlbcCodec: metadata copied through encode/decode", "[ilbc][codec]")
{
  auto info = IlbcCodecFactory::makeIlbcInfo();
  IlbcCodec encoder(info, IlbcMode::Encoder, 30);
  IlbcCodec decoder(info, IlbcMode::Decoder, 30);

  auto samples = generateSineWave(240, 400.0, 10000.0);
  auto pcm = makePcmBuffer(samples);
  pcm->setTimestamp(12345);
  pcm->setSequenceNumber(42);
  pcm->setSsrc(0xDEADBEEF);
  pcm->setPayloadType(97);
  pcm->setMarker(true);

  auto encoded = encoder.encode(*pcm);
  REQUIRE(encoded != nullptr);
  CHECK(encoded->timestamp() == 12345);
  CHECK(encoded->sequenceNumber() == 42);
  CHECK(encoded->ssrc() == 0xDEADBEEF);
  CHECK(encoded->payloadType() == 97);
  CHECK(encoded->marker() == true);

  auto decoded = decoder.decode(*encoded);
  REQUIRE(decoded != nullptr);
  CHECK(decoded->timestamp() == 12345);
  CHECK(decoded->sequenceNumber() == 42);
  CHECK(decoded->ssrc() == 0xDEADBEEF);
  CHECK(decoded->payloadType() == 97);
  CHECK(decoded->marker() == true);
}

// ============================================================================
// CodecRegistry integration
// ============================================================================
TEST_CASE("IlbcCodecFactory: CodecRegistry integration", "[ilbc][registry]")
{
  CodecRegistry registry;

  auto factory = std::make_shared<IlbcCodecFactory>(IlbcCodecFactory::makeIlbcInfo());
  registry.registerFactory(factory);

  auto byName = registry.findByName("iLBC");
  REQUIRE(byName.has_value());
  CHECK(byName->clockRate == 8000);
  CHECK(byName->channels == 1);

  // iLBC uses dynamic PT (defaultPayloadType=0); findByPayloadType(0) not tested
  // because PT 0 is PCMU in the static RTP table — ambiguous semantics.

  auto encoder = registry.createEncoder(IlbcCodecFactory::makeIlbcInfo());
  REQUIRE(encoder != nullptr);
  CHECK(encoder->info().name == "iLBC");

  auto decoder = registry.createDecoder(IlbcCodecFactory::makeIlbcInfo());
  REQUIRE(decoder != nullptr);
  CHECK(decoder->info().name == "iLBC");

  auto codecs = registry.enumerateCodecs();
  CHECK(codecs.size() == 1);
}

// ============================================================================
// Wrong-mode operations return nullptr
// ============================================================================
TEST_CASE("IlbcCodec: wrong-mode operations return nullptr", "[ilbc][codec]")
{
  auto info = IlbcCodecFactory::makeIlbcInfo();
  IlbcCodec encoder(info, IlbcMode::Encoder, 30);
  IlbcCodec decoder(info, IlbcMode::Decoder, 30);

  auto samples = generateSineWave(240, 400.0, 10000.0);
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
TEST_CASE("IlbcCodec: plc on encoder returns nullptr", "[ilbc][codec]")
{
  auto info = IlbcCodecFactory::makeIlbcInfo();
  IlbcCodec encoder(info, IlbcMode::Encoder, 30);

  CHECK(encoder.plc(240) == nullptr);
}

// ============================================================================
// Zero-sample encode returns nullptr
// ============================================================================
TEST_CASE("IlbcCodec: zero-sample encode returns nullptr", "[ilbc][codec]")
{
  auto info = IlbcCodecFactory::makeIlbcInfo();
  IlbcCodec encoder(info, IlbcMode::Encoder, 30);

  auto empty = MediaBuffer::create(0);
  empty->setSize(0);

  auto result = encoder.encode(*empty);
  CHECK(result == nullptr);
}

// ============================================================================
// Unknown parameter returns false / 0
// ============================================================================
TEST_CASE("IlbcCodec: unknown parameter returns false", "[ilbc][codec]")
{
  auto info = IlbcCodecFactory::makeIlbcInfo();
  IlbcCodec encoder(info, IlbcMode::Encoder, 30);

  CHECK_FALSE(encoder.setParameter("nonexistent", 42));
  CHECK(encoder.getParameter("nonexistent") == 0);
}
