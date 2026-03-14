#include <catch2/catch_test_macros.hpp>

#include "amr/amr_nb_codec.hpp"
#include "amr/amr_nb_codec_factory.hpp"
#include "amr/amr_wb_codec.hpp"
#include "amr/amr_wb_codec_factory.hpp"
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
// Helper: generate a sine wave
// ============================================================================
static std::vector<std::int16_t> generateSineWave(int frameSamples, double frequency,
                                                   double amplitude, int sampleRate)
{
  std::vector<std::int16_t> samples(static_cast<std::size_t>(frameSamples));
  for (int i = 0; i < frameSamples; ++i)
  {
    samples[i] = static_cast<std::int16_t>(
      amplitude * std::sin(2.0 * kPi * frequency * i / sampleRate));
  }
  return samples;
}

// ============================================================================
// AMR-NB: encode/decode round-trip
// ============================================================================
TEST_CASE("AmrNbCodec: encode/decode round-trip", "[amr-nb][codec]")
{
  auto info = AmrNbCodecFactory::makeAmrNbInfo();

  AmrNbCodec encoder(info, AmrNbMode::Encoder);
  AmrNbCodec decoder(info, AmrNbMode::Decoder);

  // 20ms at 8kHz = 160 samples
  auto samples = generateSineWave(160, 400.0, 10000.0, 8000);
  auto pcm = makePcmBuffer(samples);

  auto encoded = encoder.encode(*pcm);
  REQUIRE(encoded != nullptr);
  CHECK(encoded->size() > 0);
  CHECK(encoded->size() <= 33); // MR122 max

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
  INFO("AMR-NB decoded RMS = " << rms);
  CHECK(rms > 100.0);
}

// ============================================================================
// AMR-NB: CodecInfo fields
// ============================================================================
TEST_CASE("AmrNbCodec: CodecInfo fields", "[amr-nb][codec]")
{
  auto info = AmrNbCodecFactory::makeAmrNbInfo();
  CHECK(info.name == "AMR");
  CHECK(info.type == CodecType::Audio);
  CHECK(info.mediaSubtype == "AMR");
  CHECK(info.clockRate == 8000);
  CHECK(info.channels == 1);
  CHECK(info.defaultPayloadType == 0); // dynamic
  CHECK(info.defaultBitrate == 12200);
  CHECK(info.frameSize == 20000us);
  CHECK((info.features & CodecFeatures::Dtx) != CodecFeatures::None);
  CHECK((info.features & CodecFeatures::Vad) != CodecFeatures::None);
  CHECK((info.features & CodecFeatures::Plc) != CodecFeatures::None);
}

// ============================================================================
// AMR-NB: encode at different bitrate modes
// ============================================================================
TEST_CASE("AmrNbCodec: encode at MR475 (lowest bitrate)", "[amr-nb][codec]")
{
  auto info = AmrNbCodecFactory::makeAmrNbInfo();
  AmrNbCodec encoder(info, AmrNbMode::Encoder, AmrNbBitrateMode::MR475);
  AmrNbCodec decoder(info, AmrNbMode::Decoder);

  auto samples = generateSineWave(160, 400.0, 10000.0, 8000);
  auto pcm = makePcmBuffer(samples);

  auto encoded = encoder.encode(*pcm);
  REQUIRE(encoded != nullptr);
  CHECK(encoded->size() > 0);
  CHECK(encoded->size() < 20); // MR475 is much smaller than MR122

  auto decoded = decoder.decode(*encoded);
  REQUIRE(decoded != nullptr);
  CHECK(decoded->size() == 160 * 2);
}

// ============================================================================
// AMR-NB: setParameter bitrateMode
// ============================================================================
TEST_CASE("AmrNbCodec: setParameter bitrateMode", "[amr-nb][codec]")
{
  auto info = AmrNbCodecFactory::makeAmrNbInfo();
  AmrNbCodec encoder(info, AmrNbMode::Encoder);

  CHECK(encoder.getParameter("bitrateMode") == 7); // MR122

  CHECK(encoder.setParameter("bitrateMode", 0)); // MR475
  CHECK(encoder.getParameter("bitrateMode") == 0);

  CHECK(encoder.setParameter("bitrateMode", 6)); // MR102
  CHECK(encoder.getParameter("bitrateMode") == 6);

  // Invalid mode
  CHECK_FALSE(encoder.setParameter("bitrateMode", 8));
  CHECK(encoder.getParameter("bitrateMode") == 6); // unchanged

  // Verify encoding works after mode switch
  CHECK(encoder.setParameter("bitrateMode", 0));
  auto samples = generateSineWave(160, 400.0, 10000.0, 8000);
  auto pcm = makePcmBuffer(samples);
  auto encoded = encoder.encode(*pcm);
  REQUIRE(encoded != nullptr);
  CHECK(encoded->size() > 0);
}

// ============================================================================
// AMR-NB: DTX parameter
// ============================================================================
TEST_CASE("AmrNbCodec: dtx parameter", "[amr-nb][codec]")
{
  auto info = AmrNbCodecFactory::makeAmrNbInfo();
  AmrNbCodec encoder(info, AmrNbMode::Encoder);

  CHECK(encoder.getParameter("dtx") == 0);

  CHECK(encoder.setParameter("dtx", 1));
  CHECK(encoder.getParameter("dtx") == 1);

  CHECK(encoder.setParameter("dtx", 0));
  CHECK(encoder.getParameter("dtx") == 0);
}

// ============================================================================
// AMR-NB: PLC produces output
// ============================================================================
TEST_CASE("AmrNbCodec: PLC produces output", "[amr-nb][codec]")
{
  auto info = AmrNbCodecFactory::makeAmrNbInfo();
  AmrNbCodec decoder(info, AmrNbMode::Decoder);

  auto plcBuf = decoder.plc(160);
  REQUIRE(plcBuf != nullptr);
  CHECK(plcBuf->size() == 160 * 2);
}

// ============================================================================
// AMR-NB: PLC after decode
// ============================================================================
TEST_CASE("AmrNbCodec: PLC after decode", "[amr-nb][codec]")
{
  auto info = AmrNbCodecFactory::makeAmrNbInfo();
  AmrNbCodec encoder(info, AmrNbMode::Encoder);
  AmrNbCodec decoder(info, AmrNbMode::Decoder);

  auto samples = generateSineWave(160, 400.0, 10000.0, 8000);
  auto pcm = makePcmBuffer(samples);
  auto encoded = encoder.encode(*pcm);
  REQUIRE(encoded != nullptr);

  auto decoded = decoder.decode(*encoded);
  REQUIRE(decoded != nullptr);

  auto plcBuf = decoder.plc(160);
  REQUIRE(plcBuf != nullptr);
  CHECK(plcBuf->size() == 160 * 2);
}

// ============================================================================
// AMR-NB: Factory supports/create/reject
// ============================================================================
TEST_CASE("AmrNbCodecFactory: supports matching info", "[amr-nb][factory]")
{
  AmrNbCodecFactory factory(AmrNbCodecFactory::makeAmrNbInfo());

  CHECK(factory.supports(AmrNbCodecFactory::makeAmrNbInfo()));

  CodecInfo wrong;
  wrong.name = "opus";
  wrong.clockRate = 48000;
  wrong.channels = 2;
  CHECK_FALSE(factory.supports(wrong));
}

TEST_CASE("AmrNbCodecFactory: createEncoder and createDecoder", "[amr-nb][factory]")
{
  AmrNbCodecFactory factory(AmrNbCodecFactory::makeAmrNbInfo());

  auto encoder = factory.createEncoder(AmrNbCodecFactory::makeAmrNbInfo());
  REQUIRE(encoder != nullptr);
  CHECK(encoder->info().name == "AMR");

  auto decoder = factory.createDecoder(AmrNbCodecFactory::makeAmrNbInfo());
  REQUIRE(decoder != nullptr);
  CHECK(decoder->info().name == "AMR");
}

TEST_CASE("AmrNbCodecFactory: create rejects unsupported params", "[amr-nb][factory]")
{
  AmrNbCodecFactory factory(AmrNbCodecFactory::makeAmrNbInfo());

  CodecInfo wrong;
  wrong.name = "PCMU";
  wrong.clockRate = 8000;
  wrong.channels = 1;

  CHECK(factory.createEncoder(wrong) == nullptr);
  CHECK(factory.createDecoder(wrong) == nullptr);
}

// ============================================================================
// AMR-NB: Wrong-mode operations return nullptr
// ============================================================================
TEST_CASE("AmrNbCodec: wrong-mode operations return nullptr", "[amr-nb][codec]")
{
  auto info = AmrNbCodecFactory::makeAmrNbInfo();
  AmrNbCodec encoder(info, AmrNbMode::Encoder);
  AmrNbCodec decoder(info, AmrNbMode::Decoder);

  auto samples = generateSineWave(160, 400.0, 10000.0, 8000);
  auto pcm = makePcmBuffer(samples);

  CHECK(decoder.encode(*pcm) == nullptr);

  auto encoded = encoder.encode(*pcm);
  REQUIRE(encoded != nullptr);
  CHECK(encoder.decode(*encoded) == nullptr);
}

// ============================================================================
// AMR-NB: plc on encoder returns nullptr
// ============================================================================
TEST_CASE("AmrNbCodec: plc on encoder returns nullptr", "[amr-nb][codec]")
{
  auto info = AmrNbCodecFactory::makeAmrNbInfo();
  AmrNbCodec encoder(info, AmrNbMode::Encoder);

  CHECK(encoder.plc(160) == nullptr);
}

// ============================================================================
// AMR-NB: zero-sample encode returns nullptr
// ============================================================================
TEST_CASE("AmrNbCodec: zero-sample encode returns nullptr", "[amr-nb][codec]")
{
  auto info = AmrNbCodecFactory::makeAmrNbInfo();
  AmrNbCodec encoder(info, AmrNbMode::Encoder);

  auto empty = MediaBuffer::create(0);
  empty->setSize(0);
  CHECK(encoder.encode(*empty) == nullptr);
}

// ============================================================================
// AMR-NB: unknown parameter
// ============================================================================
TEST_CASE("AmrNbCodec: unknown parameter returns false", "[amr-nb][codec]")
{
  auto info = AmrNbCodecFactory::makeAmrNbInfo();
  AmrNbCodec encoder(info, AmrNbMode::Encoder);

  CHECK_FALSE(encoder.setParameter("nonexistent", 42));
  CHECK(encoder.getParameter("nonexistent") == 0);
}

// ============================================================================
// AMR-NB: multiple sequential frames
// ============================================================================
TEST_CASE("AmrNbCodec: multiple sequential frames", "[amr-nb][codec]")
{
  auto info = AmrNbCodecFactory::makeAmrNbInfo();
  AmrNbCodec encoder(info, AmrNbMode::Encoder);
  AmrNbCodec decoder(info, AmrNbMode::Decoder);

  auto samples = generateSineWave(160, 400.0, 10000.0, 8000);
  auto pcm = makePcmBuffer(samples);

  for (int i = 0; i < 50; ++i)
  {
    auto encoded = encoder.encode(*pcm);
    REQUIRE(encoded != nullptr);

    auto decoded = decoder.decode(*encoded);
    REQUIRE(decoded != nullptr);
    CHECK(decoded->size() == 160 * 2);
  }
}

// ============================================================================
// AMR-NB: silence encode/decode
// ============================================================================
TEST_CASE("AmrNbCodec: silence encode/decode", "[amr-nb][codec]")
{
  auto info = AmrNbCodecFactory::makeAmrNbInfo();
  AmrNbCodec encoder(info, AmrNbMode::Encoder);
  AmrNbCodec decoder(info, AmrNbMode::Decoder);

  std::vector<std::int16_t> silence(160, 0);
  auto pcm = makePcmBuffer(silence);

  auto encoded = encoder.encode(*pcm);
  REQUIRE(encoded != nullptr);

  auto decoded = decoder.decode(*encoded);
  REQUIRE(decoded != nullptr);
  CHECK(decoded->size() == 160 * 2);

  const auto* result = reinterpret_cast<const std::int16_t*>(decoded->data());
  double rms = 0.0;
  for (std::size_t i = 0; i < 160; ++i)
  {
    rms += static_cast<double>(result[i]) * static_cast<double>(result[i]);
  }
  rms = std::sqrt(rms / 160.0);
  INFO("AMR-NB silence RMS = " << rms);
  CHECK(rms < 500.0);
}

// ============================================================================
// AMR-NB: metadata copied through encode/decode
// ============================================================================
TEST_CASE("AmrNbCodec: metadata copied through encode/decode", "[amr-nb][codec]")
{
  auto info = AmrNbCodecFactory::makeAmrNbInfo();
  AmrNbCodec encoder(info, AmrNbMode::Encoder);
  AmrNbCodec decoder(info, AmrNbMode::Decoder);

  auto samples = generateSineWave(160, 400.0, 10000.0, 8000);
  auto pcm = makePcmBuffer(samples);
  pcm->setTimestamp(12345);
  pcm->setSequenceNumber(42);
  pcm->setSsrc(0xDEADBEEF);
  pcm->setPayloadType(96);
  pcm->setMarker(true);

  auto encoded = encoder.encode(*pcm);
  REQUIRE(encoded != nullptr);
  CHECK(encoded->timestamp() == 12345);
  CHECK(encoded->sequenceNumber() == 42);
  CHECK(encoded->ssrc() == 0xDEADBEEF);
  CHECK(encoded->payloadType() == 96);
  CHECK(encoded->marker() == true);

  auto decoded = decoder.decode(*encoded);
  REQUIRE(decoded != nullptr);
  CHECK(decoded->timestamp() == 12345);
  CHECK(decoded->sequenceNumber() == 42);
  CHECK(decoded->ssrc() == 0xDEADBEEF);
  CHECK(decoded->payloadType() == 96);
  CHECK(decoded->marker() == true);
}

// ============================================================================
// AMR-NB: CodecRegistry integration
// ============================================================================
TEST_CASE("AmrNbCodecFactory: CodecRegistry integration", "[amr-nb][registry]")
{
  CodecRegistry registry;

  auto factory = std::make_shared<AmrNbCodecFactory>(AmrNbCodecFactory::makeAmrNbInfo());
  registry.registerFactory(factory);

  auto byName = registry.findByName("AMR");
  REQUIRE(byName.has_value());
  CHECK(byName->clockRate == 8000);
  CHECK(byName->channels == 1);

  // Dynamic PT — findByPayloadType(0) not tested (ambiguous with PCMU).

  auto encoder = registry.createEncoder(AmrNbCodecFactory::makeAmrNbInfo());
  REQUIRE(encoder != nullptr);
  CHECK(encoder->info().name == "AMR");

  auto codecs = registry.enumerateCodecs();
  CHECK(codecs.size() == 1);
}

// ============================================================================
// AMR-WB: encode/decode round-trip
// ============================================================================
TEST_CASE("AmrWbCodec: encode/decode round-trip", "[amr-wb][codec]")
{
  auto info = AmrWbCodecFactory::makeAmrWbInfo();

  AmrWbCodec encoder(info, AmrWbMode::Encoder);
  AmrWbCodec decoder(info, AmrWbMode::Decoder);

  // 20ms at 16kHz = 320 samples
  auto samples = generateSineWave(320, 400.0, 10000.0, 16000);
  auto pcm = makePcmBuffer(samples);

  auto encoded = encoder.encode(*pcm);
  REQUIRE(encoded != nullptr);
  CHECK(encoded->size() > 0);
  CHECK(encoded->size() <= 64);

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
  INFO("AMR-WB decoded RMS = " << rms);
  CHECK(rms > 100.0);
}

// ============================================================================
// AMR-WB: CodecInfo fields
// ============================================================================
TEST_CASE("AmrWbCodec: CodecInfo fields", "[amr-wb][codec]")
{
  auto info = AmrWbCodecFactory::makeAmrWbInfo();
  CHECK(info.name == "AMR-WB");
  CHECK(info.type == CodecType::Audio);
  CHECK(info.mediaSubtype == "AMR-WB");
  CHECK(info.clockRate == 16000);
  CHECK(info.channels == 1);
  CHECK(info.defaultPayloadType == 0); // dynamic
  CHECK(info.defaultBitrate == 23850);
  CHECK(info.frameSize == 20000us);
  CHECK((info.features & CodecFeatures::Dtx) != CodecFeatures::None);
  CHECK((info.features & CodecFeatures::Vad) != CodecFeatures::None);
  CHECK((info.features & CodecFeatures::Plc) != CodecFeatures::None);
}

// ============================================================================
// AMR-WB: encode at different bitrate modes
// ============================================================================
TEST_CASE("AmrWbCodec: encode at mode 0 (lowest bitrate)", "[amr-wb][codec]")
{
  auto info = AmrWbCodecFactory::makeAmrWbInfo();
  AmrWbCodec encoder(info, AmrWbMode::Encoder, AmrWbBitrateMode::MD66);
  AmrWbCodec decoder(info, AmrWbMode::Decoder);

  auto samples = generateSineWave(320, 400.0, 10000.0, 16000);
  auto pcm = makePcmBuffer(samples);

  auto encoded = encoder.encode(*pcm);
  REQUIRE(encoded != nullptr);
  CHECK(encoded->size() > 0);

  auto decoded = decoder.decode(*encoded);
  REQUIRE(decoded != nullptr);
  CHECK(decoded->size() == 320 * 2);
}

// ============================================================================
// AMR-WB: setParameter bitrateMode
// ============================================================================
TEST_CASE("AmrWbCodec: setParameter bitrateMode", "[amr-wb][codec]")
{
  auto info = AmrWbCodecFactory::makeAmrWbInfo();
  AmrWbCodec encoder(info, AmrWbMode::Encoder);

  CHECK(encoder.getParameter("bitrateMode") == 8);

  CHECK(encoder.setParameter("bitrateMode", 0));
  CHECK(encoder.getParameter("bitrateMode") == 0);

  CHECK(encoder.setParameter("bitrateMode", 4));
  CHECK(encoder.getParameter("bitrateMode") == 4);

  // Invalid mode
  CHECK_FALSE(encoder.setParameter("bitrateMode", 9));
  CHECK(encoder.getParameter("bitrateMode") == 4); // unchanged

  // Verify encoding works after mode switch
  auto samples = generateSineWave(320, 400.0, 10000.0, 16000);
  auto pcm = makePcmBuffer(samples);
  auto encoded = encoder.encode(*pcm);
  REQUIRE(encoded != nullptr);
  CHECK(encoded->size() > 0);
}

// ============================================================================
// AMR-WB: PLC produces output
// ============================================================================
TEST_CASE("AmrWbCodec: PLC produces output", "[amr-wb][codec]")
{
  auto info = AmrWbCodecFactory::makeAmrWbInfo();
  AmrWbCodec decoder(info, AmrWbMode::Decoder);

  auto plcBuf = decoder.plc(320);
  REQUIRE(plcBuf != nullptr);
  CHECK(plcBuf->size() == 320 * 2);
}

// ============================================================================
// AMR-WB: PLC after decode
// ============================================================================
TEST_CASE("AmrWbCodec: PLC after decode", "[amr-wb][codec]")
{
  auto info = AmrWbCodecFactory::makeAmrWbInfo();
  AmrWbCodec encoder(info, AmrWbMode::Encoder);
  AmrWbCodec decoder(info, AmrWbMode::Decoder);

  auto samples = generateSineWave(320, 400.0, 10000.0, 16000);
  auto pcm = makePcmBuffer(samples);
  auto encoded = encoder.encode(*pcm);
  REQUIRE(encoded != nullptr);

  auto decoded = decoder.decode(*encoded);
  REQUIRE(decoded != nullptr);

  auto plcBuf = decoder.plc(320);
  REQUIRE(plcBuf != nullptr);
  CHECK(plcBuf->size() == 320 * 2);
}

// ============================================================================
// AMR-WB: DTX parameter
// ============================================================================
TEST_CASE("AmrWbCodec: dtx parameter", "[amr-wb][codec]")
{
  auto info = AmrWbCodecFactory::makeAmrWbInfo();
  AmrWbCodec encoder(info, AmrWbMode::Encoder);

  CHECK(encoder.getParameter("dtx") == 0);

  CHECK(encoder.setParameter("dtx", 1));
  CHECK(encoder.getParameter("dtx") == 1);

  CHECK(encoder.setParameter("dtx", 0));
  CHECK(encoder.getParameter("dtx") == 0);

  // Verify encoding works with DTX enabled
  CHECK(encoder.setParameter("dtx", 1));
  auto samples = generateSineWave(320, 400.0, 10000.0, 16000);
  auto pcm = makePcmBuffer(samples);
  auto encoded = encoder.encode(*pcm);
  REQUIRE(encoded != nullptr);
  CHECK(encoded->size() > 0);
}

// ============================================================================
// AMR-WB: Factory supports/create/reject
// ============================================================================
TEST_CASE("AmrWbCodecFactory: supports matching info", "[amr-wb][factory]")
{
  AmrWbCodecFactory factory(AmrWbCodecFactory::makeAmrWbInfo());

  CHECK(factory.supports(AmrWbCodecFactory::makeAmrWbInfo()));

  CodecInfo wrong;
  wrong.name = "AMR";
  wrong.clockRate = 8000;
  wrong.channels = 1;
  CHECK_FALSE(factory.supports(wrong));
}

TEST_CASE("AmrWbCodecFactory: createEncoder and createDecoder", "[amr-wb][factory]")
{
  AmrWbCodecFactory factory(AmrWbCodecFactory::makeAmrWbInfo());

  auto encoder = factory.createEncoder(AmrWbCodecFactory::makeAmrWbInfo());
  REQUIRE(encoder != nullptr);
  CHECK(encoder->info().name == "AMR-WB");

  auto decoder = factory.createDecoder(AmrWbCodecFactory::makeAmrWbInfo());
  REQUIRE(decoder != nullptr);
  CHECK(decoder->info().name == "AMR-WB");
}

TEST_CASE("AmrWbCodecFactory: create rejects unsupported params", "[amr-wb][factory]")
{
  AmrWbCodecFactory factory(AmrWbCodecFactory::makeAmrWbInfo());

  CodecInfo wrong;
  wrong.name = "PCMU";
  wrong.clockRate = 8000;
  wrong.channels = 1;

  CHECK(factory.createEncoder(wrong) == nullptr);
  CHECK(factory.createDecoder(wrong) == nullptr);
}

// ============================================================================
// AMR-WB: Wrong-mode operations return nullptr
// ============================================================================
TEST_CASE("AmrWbCodec: wrong-mode operations return nullptr", "[amr-wb][codec]")
{
  auto info = AmrWbCodecFactory::makeAmrWbInfo();
  AmrWbCodec encoder(info, AmrWbMode::Encoder);
  AmrWbCodec decoder(info, AmrWbMode::Decoder);

  auto samples = generateSineWave(320, 400.0, 10000.0, 16000);
  auto pcm = makePcmBuffer(samples);

  CHECK(decoder.encode(*pcm) == nullptr);

  auto encoded = encoder.encode(*pcm);
  REQUIRE(encoded != nullptr);
  CHECK(encoder.decode(*encoded) == nullptr);
}

// ============================================================================
// AMR-WB: plc on encoder returns nullptr
// ============================================================================
TEST_CASE("AmrWbCodec: plc on encoder returns nullptr", "[amr-wb][codec]")
{
  auto info = AmrWbCodecFactory::makeAmrWbInfo();
  AmrWbCodec encoder(info, AmrWbMode::Encoder);

  CHECK(encoder.plc(320) == nullptr);
}

// ============================================================================
// AMR-WB: zero-sample encode returns nullptr
// ============================================================================
TEST_CASE("AmrWbCodec: zero-sample encode returns nullptr", "[amr-wb][codec]")
{
  auto info = AmrWbCodecFactory::makeAmrWbInfo();
  AmrWbCodec encoder(info, AmrWbMode::Encoder);

  auto empty = MediaBuffer::create(0);
  empty->setSize(0);
  CHECK(encoder.encode(*empty) == nullptr);
}

// ============================================================================
// AMR-WB: unknown parameter
// ============================================================================
TEST_CASE("AmrWbCodec: unknown parameter returns false", "[amr-wb][codec]")
{
  auto info = AmrWbCodecFactory::makeAmrWbInfo();
  AmrWbCodec encoder(info, AmrWbMode::Encoder);

  CHECK_FALSE(encoder.setParameter("nonexistent", 42));
  CHECK(encoder.getParameter("nonexistent") == 0);
}

// ============================================================================
// AMR-WB: multiple sequential frames
// ============================================================================
TEST_CASE("AmrWbCodec: multiple sequential frames", "[amr-wb][codec]")
{
  auto info = AmrWbCodecFactory::makeAmrWbInfo();
  AmrWbCodec encoder(info, AmrWbMode::Encoder);
  AmrWbCodec decoder(info, AmrWbMode::Decoder);

  auto samples = generateSineWave(320, 400.0, 10000.0, 16000);
  auto pcm = makePcmBuffer(samples);

  for (int i = 0; i < 50; ++i)
  {
    auto encoded = encoder.encode(*pcm);
    REQUIRE(encoded != nullptr);

    auto decoded = decoder.decode(*encoded);
    REQUIRE(decoded != nullptr);
    CHECK(decoded->size() == 320 * 2);
  }
}

// ============================================================================
// AMR-WB: silence encode/decode
// ============================================================================
TEST_CASE("AmrWbCodec: silence encode/decode", "[amr-wb][codec]")
{
  auto info = AmrWbCodecFactory::makeAmrWbInfo();
  AmrWbCodec encoder(info, AmrWbMode::Encoder);
  AmrWbCodec decoder(info, AmrWbMode::Decoder);

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
  INFO("AMR-WB silence RMS = " << rms);
  CHECK(rms < 500.0);
}

// ============================================================================
// AMR-WB: metadata copied through encode/decode
// ============================================================================
TEST_CASE("AmrWbCodec: metadata copied through encode/decode", "[amr-wb][codec]")
{
  auto info = AmrWbCodecFactory::makeAmrWbInfo();
  AmrWbCodec encoder(info, AmrWbMode::Encoder);
  AmrWbCodec decoder(info, AmrWbMode::Decoder);

  auto samples = generateSineWave(320, 400.0, 10000.0, 16000);
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
// AMR-WB: CodecRegistry integration
// ============================================================================
TEST_CASE("AmrWbCodecFactory: CodecRegistry integration", "[amr-wb][registry]")
{
  CodecRegistry registry;

  auto factory = std::make_shared<AmrWbCodecFactory>(AmrWbCodecFactory::makeAmrWbInfo());
  registry.registerFactory(factory);

  auto byName = registry.findByName("AMR-WB");
  REQUIRE(byName.has_value());
  CHECK(byName->clockRate == 16000);
  CHECK(byName->channels == 1);

  auto encoder = registry.createEncoder(AmrWbCodecFactory::makeAmrWbInfo());
  REQUIRE(encoder != nullptr);
  CHECK(encoder->info().name == "AMR-WB");

  auto codecs = registry.enumerateCodecs();
  CHECK(codecs.size() == 1);
}

// ============================================================================
// Both codecs in same registry
// ============================================================================
TEST_CASE("AMR: both NB and WB in same CodecRegistry", "[amr][registry]")
{
  CodecRegistry registry;

  auto nbFactory = std::make_shared<AmrNbCodecFactory>(AmrNbCodecFactory::makeAmrNbInfo());
  auto wbFactory = std::make_shared<AmrWbCodecFactory>(AmrWbCodecFactory::makeAmrWbInfo());
  registry.registerFactory(nbFactory);
  registry.registerFactory(wbFactory);

  auto codecs = registry.enumerateCodecs();
  CHECK(codecs.size() == 2);

  auto nbInfo = registry.findByName("AMR");
  REQUIRE(nbInfo.has_value());
  CHECK(nbInfo->clockRate == 8000);

  auto wbInfo = registry.findByName("AMR-WB");
  REQUIRE(wbInfo.has_value());
  CHECK(wbInfo->clockRate == 16000);
}

// ============================================================================
// AMR-NB: DTX re-init verified with encode
// ============================================================================
TEST_CASE("AmrNbCodec: DTX re-init produces valid output", "[amr-nb][codec]")
{
  auto info = AmrNbCodecFactory::makeAmrNbInfo();
  AmrNbCodec encoder(info, AmrNbMode::Encoder);
  AmrNbCodec decoder(info, AmrNbMode::Decoder);

  auto samples = generateSineWave(160, 400.0, 10000.0, 8000);
  auto pcm = makePcmBuffer(samples);

  // Encode a frame before DTX toggle
  auto before = encoder.encode(*pcm);
  REQUIRE(before != nullptr);

  // Toggle DTX on
  REQUIRE(encoder.setParameter("dtx", 1));

  // Encode after re-init — must still produce valid output
  auto afterOn = encoder.encode(*pcm);
  REQUIRE(afterOn != nullptr);
  CHECK(afterOn->size() > 0);

  // Verify round-trip works after DTX re-init
  auto decoded = decoder.decode(*afterOn);
  REQUIRE(decoded != nullptr);
  CHECK(decoded->size() == 160 * 2);

  // Toggle DTX off again
  REQUIRE(encoder.setParameter("dtx", 0));

  auto afterOff = encoder.encode(*pcm);
  REQUIRE(afterOff != nullptr);

  decoded = decoder.decode(*afterOff);
  REQUIRE(decoded != nullptr);
  CHECK(decoded->size() == 160 * 2);
}

// ============================================================================
// AMR-NB: multiple DTX toggles with interleaved encoding
// ============================================================================
TEST_CASE("AmrNbCodec: multiple DTX toggles mid-stream", "[amr-nb][codec]")
{
  auto info = AmrNbCodecFactory::makeAmrNbInfo();
  AmrNbCodec encoder(info, AmrNbMode::Encoder);
  AmrNbCodec decoder(info, AmrNbMode::Decoder);

  auto samples = generateSineWave(160, 400.0, 10000.0, 8000);
  auto pcm = makePcmBuffer(samples);

  for (int i = 0; i < 10; ++i)
  {
    auto encoded = encoder.encode(*pcm);
    REQUIRE(encoded != nullptr);
    auto decoded = decoder.decode(*encoded);
    REQUIRE(decoded != nullptr);
  }

  // Toggle DTX on mid-stream
  REQUIRE(encoder.setParameter("dtx", 1));

  for (int i = 0; i < 10; ++i)
  {
    auto encoded = encoder.encode(*pcm);
    REQUIRE(encoded != nullptr);
    auto decoded = decoder.decode(*encoded);
    REQUIRE(decoded != nullptr);
  }

  // Toggle DTX off again
  REQUIRE(encoder.setParameter("dtx", 0));

  for (int i = 0; i < 10; ++i)
  {
    auto encoded = encoder.encode(*pcm);
    REQUIRE(encoded != nullptr);
    auto decoded = decoder.decode(*encoded);
    REQUIRE(decoded != nullptr);
    CHECK(decoded->size() == 160 * 2);
  }
}

// ============================================================================
// AMR-NB: zero-size decode returns nullptr
// ============================================================================
TEST_CASE("AmrNbCodec: zero-size decode returns nullptr", "[amr-nb][codec]")
{
  auto info = AmrNbCodecFactory::makeAmrNbInfo();
  AmrNbCodec decoder(info, AmrNbMode::Decoder);

  auto empty = MediaBuffer::create(0);
  empty->setSize(0);
  CHECK(decoder.decode(*empty) == nullptr);
}

// ============================================================================
// AMR-WB: zero-size decode returns nullptr
// ============================================================================
TEST_CASE("AmrWbCodec: zero-size decode returns nullptr", "[amr-wb][codec]")
{
  auto info = AmrWbCodecFactory::makeAmrWbInfo();
  AmrWbCodec decoder(info, AmrWbMode::Decoder);

  auto empty = MediaBuffer::create(0);
  empty->setSize(0);
  CHECK(decoder.decode(*empty) == nullptr);
}

// ============================================================================
// AMR-NB: undersized encode input returns nullptr
// ============================================================================
TEST_CASE("AmrNbCodec: undersized encode input returns nullptr", "[amr-nb][codec]")
{
  auto info = AmrNbCodecFactory::makeAmrNbInfo();
  AmrNbCodec encoder(info, AmrNbMode::Encoder);

  // 80 samples = 160 bytes, but NB needs 160 samples = 320 bytes
  std::vector<std::int16_t> tooSmall(80, 1000);
  auto pcm = makePcmBuffer(tooSmall);
  CHECK(encoder.encode(*pcm) == nullptr);

  // 1 sample = 2 bytes
  std::vector<std::int16_t> tiny(1, 1000);
  auto pcm2 = makePcmBuffer(tiny);
  CHECK(encoder.encode(*pcm2) == nullptr);

  // 159 samples — one short
  std::vector<std::int16_t> almostEnough(159, 1000);
  auto pcm3 = makePcmBuffer(almostEnough);
  CHECK(encoder.encode(*pcm3) == nullptr);
}

// ============================================================================
// AMR-WB: undersized encode input returns nullptr
// ============================================================================
TEST_CASE("AmrWbCodec: undersized encode input returns nullptr", "[amr-wb][codec]")
{
  auto info = AmrWbCodecFactory::makeAmrWbInfo();
  AmrWbCodec encoder(info, AmrWbMode::Encoder);

  // 160 samples = 320 bytes, but WB needs 320 samples = 640 bytes
  std::vector<std::int16_t> tooSmall(160, 1000);
  auto pcm = makePcmBuffer(tooSmall);
  CHECK(encoder.encode(*pcm) == nullptr);

  // 319 samples — one short
  std::vector<std::int16_t> almostEnough(319, 1000);
  auto pcm2 = makePcmBuffer(almostEnough);
  CHECK(encoder.encode(*pcm2) == nullptr);
}

// ============================================================================
// AMR-NB: setParameter on decoder returns false
// ============================================================================
TEST_CASE("AmrNbCodec: setParameter on decoder returns false", "[amr-nb][codec]")
{
  auto info = AmrNbCodecFactory::makeAmrNbInfo();
  AmrNbCodec decoder(info, AmrNbMode::Decoder);

  CHECK_FALSE(decoder.setParameter("bitrateMode", 0));
  CHECK_FALSE(decoder.setParameter("dtx", 1));
}

// ============================================================================
// AMR-WB: setParameter on decoder returns false
// ============================================================================
TEST_CASE("AmrWbCodec: setParameter on decoder returns false", "[amr-wb][codec]")
{
  auto info = AmrWbCodecFactory::makeAmrWbInfo();
  AmrWbCodec decoder(info, AmrWbMode::Decoder);

  CHECK_FALSE(decoder.setParameter("bitrateMode", 0));
  CHECK_FALSE(decoder.setParameter("dtx", 1));
}
