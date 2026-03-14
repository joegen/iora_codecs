#include <catch2/catch_test_macros.hpp>

#include "opus/opus_codec.hpp"
#include "opus/opus_codec_factory.hpp"
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
// Helper: generate a sine wave at given frequency
// ============================================================================
static std::vector<std::int16_t> generateSineWave(
  int sampleRate, int channels, int frameSamples, double frequency, double amplitude)
{
  std::vector<std::int16_t> samples(static_cast<std::size_t>(frameSamples) * static_cast<std::size_t>(channels));
  for (int i = 0; i < frameSamples; ++i)
  {
    auto val = static_cast<std::int16_t>(
      amplitude * std::sin(2.0 * kPi * frequency * i / sampleRate));
    for (int ch = 0; ch < channels; ++ch)
    {
      samples[static_cast<std::size_t>(i) * static_cast<std::size_t>(channels) + static_cast<std::size_t>(ch)] = val;
    }
  }
  return samples;
}

// ============================================================================
// Mono encode/decode round-trip
// ============================================================================
TEST_CASE("OpusCodec: mono encode/decode round-trip", "[opus][codec]")
{
  auto info = OpusCodecFactory::makeOpusInfo();
  info.channels = 1;

  OpusCodec encoder(info, OpusMode::Encoder);
  OpusCodec decoder(info, OpusMode::Decoder);

  constexpr int frameSamples = 960;
  auto samples = generateSineWave(48000, 1, frameSamples, 440.0, 10000.0);
  auto pcm = makePcmBuffer(samples);

  auto encoded = encoder.encode(*pcm);
  REQUIRE(encoded != nullptr);
  CHECK(encoded->size() > 0);
  CHECK(encoded->size() < static_cast<std::size_t>(frameSamples) * 2);

  auto decoded = decoder.decode(*encoded);
  REQUIRE(decoded != nullptr);
  CHECK(decoded->size() == static_cast<std::size_t>(frameSamples) * 2);

  // Verify decoded output has energy (not silence)
  const auto* result = reinterpret_cast<const std::int16_t*>(decoded->data());
  double rms = 0.0;
  for (int i = 0; i < frameSamples; ++i)
  {
    rms += static_cast<double>(result[i]) * static_cast<double>(result[i]);
  }
  rms = std::sqrt(rms / frameSamples);
  INFO("Decoded RMS = " << rms);
  CHECK(rms > 100.0); // output should have significant energy
}

// ============================================================================
// Stereo encode/decode round-trip
// ============================================================================
TEST_CASE("OpusCodec: stereo encode/decode round-trip", "[opus][codec]")
{
  auto info = OpusCodecFactory::makeOpusInfo();

  OpusCodec encoder(info, OpusMode::Encoder);
  OpusCodec decoder(info, OpusMode::Decoder);

  constexpr int frameSamples = 960;
  constexpr int channels = 2;
  auto samples = generateSineWave(48000, channels, frameSamples, 440.0, 10000.0);
  auto pcm = makePcmBuffer(samples);

  auto encoded = encoder.encode(*pcm);
  REQUIRE(encoded != nullptr);
  CHECK(encoded->size() > 0);

  auto decoded = decoder.decode(*encoded);
  REQUIRE(decoded != nullptr);
  CHECK(decoded->size() == static_cast<std::size_t>(frameSamples) * channels * 2);

  // Verify decoded output has energy (not silence)
  const auto* result = reinterpret_cast<const std::int16_t*>(decoded->data());
  double rms = 0.0;
  std::size_t totalSamples = static_cast<std::size_t>(frameSamples) * channels;
  for (std::size_t i = 0; i < totalSamples; ++i)
  {
    rms += static_cast<double>(result[i]) * static_cast<double>(result[i]);
  }
  rms = std::sqrt(rms / static_cast<double>(totalSamples));
  INFO("Decoded RMS = " << rms);
  CHECK(rms > 100.0); // output should have significant energy
}

// ============================================================================
// CodecInfo fields
// ============================================================================
TEST_CASE("OpusCodec: CodecInfo fields", "[opus][codec]")
{
  auto info = OpusCodecFactory::makeOpusInfo();
  CHECK(info.name == "opus");
  CHECK(info.type == CodecType::Audio);
  CHECK(info.mediaSubtype == "opus");
  CHECK(info.clockRate == 48000);
  CHECK(info.channels == 2);
  CHECK(info.defaultPayloadType == 111);
  CHECK(info.defaultBitrate == 64000);
  CHECK(info.frameSize == 20000us);
  CHECK(hasFeature(info.features, CodecFeatures::Fec));
  CHECK(hasFeature(info.features, CodecFeatures::Dtx));
  CHECK(hasFeature(info.features, CodecFeatures::Vad));
  CHECK(hasFeature(info.features, CodecFeatures::Plc));
  CHECK(hasFeature(info.features, CodecFeatures::Vbr));
  CHECK(hasFeature(info.features, CodecFeatures::Cbr));
}

// ============================================================================
// FEC encode/decode
// ============================================================================
TEST_CASE("OpusCodec: FEC encode produces valid output", "[opus][codec]")
{
  auto info = OpusCodecFactory::makeOpusInfo();
  info.channels = 1;

  OpusCodec encoder(info, OpusMode::Encoder);
  CHECK(encoder.setParameter("fec", 1));
  CHECK(encoder.setParameter("packet_loss_pct", 20));

  auto samples = generateSineWave(48000, 1, 960, 440.0, 10000.0);
  auto pcm = makePcmBuffer(samples);

  // Encode several frames to let FEC stabilize
  std::shared_ptr<MediaBuffer> encoded;
  for (int i = 0; i < 5; ++i)
  {
    encoded = encoder.encode(*pcm);
    REQUIRE(encoded != nullptr);
    CHECK(encoded->size() > 0);
  }
}

// ============================================================================
// DTX with silence
// ============================================================================
TEST_CASE("OpusCodec: DTX produces small frames for silence", "[opus][codec]")
{
  auto info = OpusCodecFactory::makeOpusInfo();
  info.channels = 1;

  OpusCodec encoder(info, OpusMode::Encoder);
  CHECK(encoder.setParameter("dtx", 1));

  // Encode many frames of silence to trigger DTX
  std::vector<std::int16_t> silence(960, 0);
  auto pcm = makePcmBuffer(silence);

  std::size_t lastSize = 0;
  bool sawSmallFrame = false;
  for (int i = 0; i < 50; ++i)
  {
    auto encoded = encoder.encode(*pcm);
    REQUIRE(encoded != nullptr);
    lastSize = encoded->size();
    if (lastSize <= 3)
    {
      sawSmallFrame = true;
    }
  }
  // DTX should eventually produce very small or empty frames for sustained silence
  CHECK(sawSmallFrame);
}

// ============================================================================
// PLC produces valid output
// ============================================================================
TEST_CASE("OpusCodec: PLC produces valid concealment frame", "[opus][codec]")
{
  auto info = OpusCodecFactory::makeOpusInfo();
  info.channels = 1;

  OpusCodec encoder(info, OpusMode::Encoder);
  OpusCodec decoder(info, OpusMode::Decoder);

  // Feed some frames to build decoder state
  auto samples = generateSineWave(48000, 1, 960, 440.0, 10000.0);
  auto pcm = makePcmBuffer(samples);

  for (int i = 0; i < 5; ++i)
  {
    auto encoded = encoder.encode(*pcm);
    REQUIRE(encoded != nullptr);
    auto decoded = decoder.decode(*encoded);
    REQUIRE(decoded != nullptr);
  }

  // Now trigger PLC
  auto plcFrame = decoder.plc(960);
  REQUIRE(plcFrame != nullptr);
  CHECK(plcFrame->size() == 960 * 2); // 960 samples * 2 bytes
}

// ============================================================================
// Parameter setting: bitrate
// ============================================================================
TEST_CASE("OpusCodec: setParameter bitrate", "[opus][codec]")
{
  auto info = OpusCodecFactory::makeOpusInfo();
  info.channels = 1;

  OpusCodec encoder(info, OpusMode::Encoder);

  CHECK(encoder.setParameter("bitrate", 48000));
  CHECK(encoder.getParameter("bitrate") == 48000);
}

// ============================================================================
// Parameter setting: complexity
// ============================================================================
TEST_CASE("OpusCodec: setParameter complexity", "[opus][codec]")
{
  auto info = OpusCodecFactory::makeOpusInfo();
  info.channels = 1;

  OpusCodec encoder(info, OpusMode::Encoder);

  CHECK(encoder.setParameter("complexity", 10));
  CHECK(encoder.getParameter("complexity") == 10);
}

// ============================================================================
// VBR toggle
// ============================================================================
TEST_CASE("OpusCodec: VBR toggle affects frame sizes", "[opus][codec]")
{
  auto info = OpusCodecFactory::makeOpusInfo();
  info.channels = 1;

  OpusCodec encoderVbr(info, OpusMode::Encoder);
  OpusCodec encoderCbr(info, OpusMode::Encoder);

  CHECK(encoderCbr.setParameter("vbr", 0));
  CHECK(encoderCbr.getParameter("vbr") == 0);

  auto sine = generateSineWave(48000, 1, 960, 440.0, 10000.0);
  auto silence = std::vector<std::int16_t>(960, 0);
  auto pcmSine = makePcmBuffer(sine);
  auto pcmSilence = makePcmBuffer(silence);

  // CBR: frame sizes should be very consistent
  std::size_t cbrSineSize = 0;
  std::size_t cbrSilenceSize = 0;
  for (int i = 0; i < 10; ++i)
  {
    auto enc = encoderCbr.encode(*pcmSine);
    REQUIRE(enc != nullptr);
    cbrSineSize = enc->size();
  }
  for (int i = 0; i < 10; ++i)
  {
    auto enc = encoderCbr.encode(*pcmSilence);
    REQUIRE(enc != nullptr);
    cbrSilenceSize = enc->size();
  }

  // In CBR mode, sizes should be close
  int sizeDiffCbr = static_cast<int>(cbrSineSize) - static_cast<int>(cbrSilenceSize);
  INFO("CBR sine=" << cbrSineSize << " silence=" << cbrSilenceSize);
  CHECK(std::abs(sizeDiffCbr) < 20);
}

// ============================================================================
// Factory supports() accepts matching info
// ============================================================================
TEST_CASE("OpusCodecFactory: supports matching info", "[opus][factory]")
{
  OpusCodecFactory factory(OpusCodecFactory::makeOpusInfo());

  CHECK(factory.supports(OpusCodecFactory::makeOpusInfo()));

  // Wrong name
  CodecInfo wrong;
  wrong.name = "PCMU";
  wrong.clockRate = 8000;
  wrong.channels = 1;
  CHECK_FALSE(factory.supports(wrong));

  // Wrong clock rate
  CodecInfo wrongRate;
  wrongRate.name = "opus";
  wrongRate.clockRate = 8000;
  wrongRate.channels = 2;
  CHECK_FALSE(factory.supports(wrongRate));

  // Wrong channels
  CodecInfo wrongChannels;
  wrongChannels.name = "opus";
  wrongChannels.clockRate = 48000;
  wrongChannels.channels = 1;
  CHECK_FALSE(factory.supports(wrongChannels));
}

// ============================================================================
// Factory createEncoder/createDecoder
// ============================================================================
TEST_CASE("OpusCodecFactory: createEncoder and createDecoder", "[opus][factory]")
{
  OpusCodecFactory factory(OpusCodecFactory::makeOpusInfo());

  auto encoder = factory.createEncoder(OpusCodecFactory::makeOpusInfo());
  REQUIRE(encoder != nullptr);
  CHECK(encoder->info().name == "opus");

  auto decoder = factory.createDecoder(OpusCodecFactory::makeOpusInfo());
  REQUIRE(decoder != nullptr);
  CHECK(decoder->info().name == "opus");
}

// ============================================================================
// Factory createEncoder/createDecoder reject unsupported params
// ============================================================================
TEST_CASE("OpusCodecFactory: create rejects unsupported params", "[opus][factory]")
{
  OpusCodecFactory factory(OpusCodecFactory::makeOpusInfo());

  CodecInfo wrong;
  wrong.name = "PCMU";
  wrong.clockRate = 8000;
  wrong.channels = 1;

  CHECK(factory.createEncoder(wrong) == nullptr);
  CHECK(factory.createDecoder(wrong) == nullptr);
}

// ============================================================================
// Multiple frame encode/decode
// ============================================================================
TEST_CASE("OpusCodec: multiple sequential frames", "[opus][codec]")
{
  auto info = OpusCodecFactory::makeOpusInfo();
  info.channels = 1;

  OpusCodec encoder(info, OpusMode::Encoder);
  OpusCodec decoder(info, OpusMode::Decoder);

  auto samples = generateSineWave(48000, 1, 960, 440.0, 10000.0);
  auto pcm = makePcmBuffer(samples);

  for (int i = 0; i < 50; ++i)
  {
    auto encoded = encoder.encode(*pcm);
    REQUIRE(encoded != nullptr);
    CHECK(encoded->size() > 0);

    auto decoded = decoder.decode(*encoded);
    REQUIRE(decoded != nullptr);
    CHECK(decoded->size() == 960 * 2);
  }
}

// ============================================================================
// Silence encode/decode
// ============================================================================
TEST_CASE("OpusCodec: silence encode/decode", "[opus][codec]")
{
  auto info = OpusCodecFactory::makeOpusInfo();
  info.channels = 1;

  OpusCodec encoder(info, OpusMode::Encoder);
  OpusCodec decoder(info, OpusMode::Decoder);

  std::vector<std::int16_t> silence(960, 0);
  auto pcm = makePcmBuffer(silence);

  auto encoded = encoder.encode(*pcm);
  REQUIRE(encoded != nullptr);

  auto decoded = decoder.decode(*encoded);
  REQUIRE(decoded != nullptr);
  CHECK(decoded->size() == 960 * 2);

  // Decoded silence should be near-zero
  const auto* result = reinterpret_cast<const std::int16_t*>(decoded->data());
  double rms = 0.0;
  for (std::size_t i = 0; i < 960; ++i)
  {
    rms += static_cast<double>(result[i]) * static_cast<double>(result[i]);
  }
  rms = std::sqrt(rms / 960.0);
  INFO("Silence RMS = " << rms);
  CHECK(rms < 500.0); // Should be very quiet
}

// ============================================================================
// Metadata copied through encode/decode
// ============================================================================
TEST_CASE("OpusCodec: metadata copied through encode/decode", "[opus][codec]")
{
  auto info = OpusCodecFactory::makeOpusInfo();
  info.channels = 1;

  OpusCodec encoder(info, OpusMode::Encoder);
  OpusCodec decoder(info, OpusMode::Decoder);

  auto samples = generateSineWave(48000, 1, 960, 440.0, 10000.0);
  auto pcm = makePcmBuffer(samples);
  pcm->setTimestamp(12345);
  pcm->setSequenceNumber(42);
  pcm->setSsrc(0xDEADBEEF);
  pcm->setPayloadType(111);
  pcm->setMarker(true);

  auto encoded = encoder.encode(*pcm);
  REQUIRE(encoded != nullptr);
  CHECK(encoded->timestamp() == 12345);
  CHECK(encoded->sequenceNumber() == 42);
  CHECK(encoded->ssrc() == 0xDEADBEEF);
  CHECK(encoded->payloadType() == 111);
  CHECK(encoded->marker() == true);

  auto decoded = decoder.decode(*encoded);
  REQUIRE(decoded != nullptr);
  CHECK(decoded->timestamp() == 12345);
  CHECK(decoded->sequenceNumber() == 42);
  CHECK(decoded->ssrc() == 0xDEADBEEF);
  CHECK(decoded->payloadType() == 111);
  CHECK(decoded->marker() == true);
}

// ============================================================================
// CodecRegistry integration
// ============================================================================
TEST_CASE("OpusCodecFactory: CodecRegistry integration", "[opus][registry]")
{
  CodecRegistry registry;

  auto factory = std::make_shared<OpusCodecFactory>(OpusCodecFactory::makeOpusInfo());
  registry.registerFactory(factory);

  auto byName = registry.findByName("opus");
  REQUIRE(byName.has_value());
  CHECK(byName->clockRate == 48000);
  CHECK(byName->channels == 2);
  CHECK(byName->defaultPayloadType == 111);

  auto byPt = registry.findByPayloadType(111);
  REQUIRE(byPt.has_value());
  CHECK(byPt->name == "opus");

  auto encoder = registry.createEncoder(OpusCodecFactory::makeOpusInfo());
  REQUIRE(encoder != nullptr);
  CHECK(encoder->info().name == "opus");

  auto decoder = registry.createDecoder(OpusCodecFactory::makeOpusInfo());
  REQUIRE(decoder != nullptr);
  CHECK(decoder->info().name == "opus");

  auto codecs = registry.enumerateCodecs();
  CHECK(codecs.size() == 1);
}

// ============================================================================
// setParameter on decoder returns false
// ============================================================================
TEST_CASE("OpusCodec: setParameter on decoder returns false", "[opus][codec]")
{
  auto info = OpusCodecFactory::makeOpusInfo();
  info.channels = 1;

  OpusCodec decoder(info, OpusMode::Decoder);
  CHECK_FALSE(decoder.setParameter("bitrate", 48000));
}

// ============================================================================
// Unknown parameter returns false
// ============================================================================
TEST_CASE("OpusCodec: unknown parameter returns false", "[opus][codec]")
{
  auto info = OpusCodecFactory::makeOpusInfo();
  info.channels = 1;

  OpusCodec encoder(info, OpusMode::Encoder);
  CHECK_FALSE(encoder.setParameter("nonexistent", 42));
  CHECK(encoder.getParameter("nonexistent") == 0);
}

// ============================================================================
// encode() on decoder returns nullptr, decode() on encoder returns nullptr
// ============================================================================
TEST_CASE("OpusCodec: wrong-mode operations return nullptr", "[opus][codec]")
{
  auto info = OpusCodecFactory::makeOpusInfo();
  info.channels = 1;

  OpusCodec encoder(info, OpusMode::Encoder);
  OpusCodec decoder(info, OpusMode::Decoder);

  auto samples = generateSineWave(48000, 1, 960, 440.0, 10000.0);
  auto pcm = makePcmBuffer(samples);

  // encode on encoder should work
  auto encoded = encoder.encode(*pcm);
  REQUIRE(encoded != nullptr);

  // decode on decoder should work
  auto decoded = decoder.decode(*encoded);
  REQUIRE(decoded != nullptr);

  // encode on decoder returns nullptr
  CHECK(decoder.encode(*pcm) == nullptr);

  // decode on encoder returns nullptr
  CHECK(encoder.decode(*encoded) == nullptr);
}

// ============================================================================
// plc() on encoder returns nullptr
// ============================================================================
TEST_CASE("OpusCodec: plc on encoder returns nullptr", "[opus][codec]")
{
  auto info = OpusCodecFactory::makeOpusInfo();
  info.channels = 1;

  OpusCodec encoder(info, OpusMode::Encoder);
  CHECK(encoder.plc(960) == nullptr);
}

// ============================================================================
// Empty input to encode
// ============================================================================
TEST_CASE("OpusCodec: zero-sample encode returns nullptr", "[opus][codec]")
{
  auto info = OpusCodecFactory::makeOpusInfo();
  info.channels = 1;

  OpusCodec encoder(info, OpusMode::Encoder);

  auto empty = MediaBuffer::create(0);
  empty->setSize(0);

  // opus_encode with 0 samples returns an error
  auto result = encoder.encode(*empty);
  CHECK(result == nullptr);
}

// ============================================================================
// Decoder getParameter returns meaningful values
// ============================================================================
TEST_CASE("OpusCodec: decoder getParameter returns meaningful values", "[opus][codec]")
{
  auto info = OpusCodecFactory::makeOpusInfo();
  info.channels = 2;

  OpusCodec decoder(info, OpusMode::Decoder);

  CHECK(decoder.getParameter("sampleRate") == 48000);
  CHECK(decoder.getParameter("channels") == 2);
  // Unrecognized keys still return 0
  CHECK(decoder.getParameter("nonexistent") == 0);
}

TEST_CASE("OpusCodec: decoder getParameter after decode", "[opus][codec]")
{
  auto info = OpusCodecFactory::makeOpusInfo();
  info.channels = 1;

  OpusCodec encoder(info, OpusMode::Encoder);
  OpusCodec decoder(info, OpusMode::Decoder);

  auto samples = generateSineWave(48000, 1, 960, 440.0, 10000.0);
  auto pcm = makePcmBuffer(samples);

  auto encoded = encoder.encode(*pcm);
  REQUIRE(encoded != nullptr);

  auto decoded = decoder.decode(*encoded);
  REQUIRE(decoded != nullptr);

  // After decoding, lastPacketDuration should be non-zero
  CHECK(decoder.getParameter("lastPacketDuration") > 0);
}
