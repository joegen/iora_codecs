#include <catch2/catch_test_macros.hpp>

#include "av1/av1_codec.hpp"
#include "av1/av1_codec_factory.hpp"
#include "iora/codecs/codec/codec_registry.hpp"

#include <cmath>
#include <cstring>
#include <vector>

using namespace iora::codecs;

// ============================================================================
// Helper: create a gradient I420 frame (Y ramp 0-255, U/V=128)
// ============================================================================
static std::shared_ptr<MediaBuffer> makeI420Frame(std::uint32_t width, std::uint32_t height)
{
  std::size_t ySize = static_cast<std::size_t>(width) * height;
  std::size_t uvSize = static_cast<std::size_t>(width / 2) * (height / 2);
  std::size_t totalSize = ySize + uvSize * 2;
  auto buf = MediaBuffer::create(totalSize);

  std::uint8_t* data = buf->data();

  // Y plane: horizontal gradient 0-255
  for (std::uint32_t row = 0; row < height; ++row)
  {
    for (std::uint32_t col = 0; col < width; ++col)
    {
      data[row * width + col] = static_cast<std::uint8_t>((col * 255) / (width - 1));
    }
  }

  // U plane: neutral 128
  std::memset(data + ySize, 128, uvSize);

  // V plane: neutral 128
  std::memset(data + ySize + uvSize, 128, uvSize);

  buf->setSize(totalSize);
  return buf;
}

// ============================================================================
// Helper: compute PSNR between two I420 Y planes
// ============================================================================
static double computePsnr(const std::uint8_t* a, const std::uint8_t* b, std::size_t count)
{
  double mse = 0.0;
  for (std::size_t i = 0; i < count; ++i)
  {
    double diff = static_cast<double>(a[i]) - static_cast<double>(b[i]);
    mse += diff * diff;
  }
  mse /= static_cast<double>(count);
  if (mse == 0.0)
  {
    return 100.0; // identical
  }
  return 10.0 * std::log10(255.0 * 255.0 / mse);
}

// ============================================================================
// AV1: encode/decode round-trip
// ============================================================================
TEST_CASE("Av1Codec: encode/decode round-trip", "[av1][codec]")
{
  auto info = Av1CodecFactory::makeAv1Info();

  constexpr std::uint32_t w = 320;
  constexpr std::uint32_t h = 240;

  Av1Codec encoder(info, Av1Mode::Encoder, w, h, 200000, 30.0f, 8);
  Av1Codec decoder(info, Av1Mode::Decoder, w, h);

  auto frame = makeI420Frame(w, h);

  std::shared_ptr<MediaBuffer> encoded;
  std::shared_ptr<MediaBuffer> decoded;

  for (int i = 0; i < 15; ++i)
  {
    encoded = encoder.encode(*frame);
    if (encoded != nullptr)
    {
      decoded = decoder.decode(*encoded);
      if (decoded != nullptr)
      {
        break;
      }
    }
  }

  REQUIRE(encoded != nullptr);
  REQUIRE(decoded != nullptr);

  std::size_t expectedSize = static_cast<std::size_t>(w) * h * 3 / 2;
  CHECK(decoded->size() == expectedSize);

  double psnr = computePsnr(frame->data(), decoded->data(), static_cast<std::size_t>(w) * h);
  INFO("AV1 round-trip PSNR = " << psnr << " dB");
  CHECK(psnr > 20.0);
}

// ============================================================================
// AV1: CodecInfo fields
// ============================================================================
TEST_CASE("Av1Codec: CodecInfo fields", "[av1][codec]")
{
  auto info = Av1CodecFactory::makeAv1Info();
  CHECK(info.name == "AV1");
  CHECK(info.type == CodecType::Video);
  CHECK(info.mediaSubtype == "AV1");
  CHECK(info.clockRate == 90000);
  CHECK(info.channels == 0);
  CHECK(info.defaultPayloadType == 0);
  CHECK(info.defaultBitrate == 300000);
  CHECK(info.frameSize == std::chrono::microseconds{33333});
  CHECK((info.features & CodecFeatures::Cbr) != CodecFeatures::None);
  CHECK((info.features & CodecFeatures::Vbr) != CodecFeatures::None);
}

// ============================================================================
// AV1: setParameter bitrate
// ============================================================================
TEST_CASE("Av1Codec: setParameter bitrate", "[av1][codec]")
{
  auto info = Av1CodecFactory::makeAv1Info();
  Av1Codec encoder(info, Av1Mode::Encoder, 320, 240);

  CHECK(encoder.getParameter("bitrate") == 300000);

  CHECK(encoder.setParameter("bitrate", 150000));
  CHECK(encoder.getParameter("bitrate") == 150000);

  auto frame = makeI420Frame(320, 240);
  auto encoded = encoder.encode(*frame);
  REQUIRE(encoded != nullptr);
  CHECK(encoded->size() > 0);
}

// ============================================================================
// AV1: setParameter speed
// ============================================================================
TEST_CASE("Av1Codec: setParameter speed", "[av1][codec]")
{
  auto info = Av1CodecFactory::makeAv1Info();
  Av1Codec encoder(info, Av1Mode::Encoder, 320, 240, 300000, 30.0f, 8);

  CHECK(encoder.getParameter("speed") == 8);

  CHECK(encoder.setParameter("speed", 10));
  CHECK(encoder.getParameter("speed") == 10);
}

// ============================================================================
// AV1: setParameter framerate
// ============================================================================
TEST_CASE("Av1Codec: setParameter framerate", "[av1][codec]")
{
  auto info = Av1CodecFactory::makeAv1Info();
  Av1Codec encoder(info, Av1Mode::Encoder, 320, 240);

  CHECK(encoder.getParameter("framerate") == 30);

  CHECK(encoder.setParameter("framerate", 15));
  CHECK(encoder.getParameter("framerate") == 15);
}

// ============================================================================
// AV1: requestKeyFrame produces keyframe
// ============================================================================
TEST_CASE("Av1Codec: requestKeyFrame produces keyframe", "[av1][codec]")
{
  auto info = Av1CodecFactory::makeAv1Info();
  Av1Codec encoder(info, Av1Mode::Encoder, 320, 240, 300000, 30.0f, 8);
  Av1Codec decoder(info, Av1Mode::Decoder, 320, 240);

  auto frame = makeI420Frame(320, 240);

  // Encode several frames to get past initial keyframe
  for (int i = 0; i < 10; ++i)
  {
    auto enc = encoder.encode(*frame);
    if (enc != nullptr)
    {
      decoder.decode(*enc);
    }
  }

  CHECK(encoder.setParameter("requestKeyFrame", 1));

  auto encoded = encoder.encode(*frame);
  REQUIRE(encoded != nullptr);
  CHECK(encoded->size() > 0);

  auto decoded = decoder.decode(*encoded);
  REQUIRE(decoded != nullptr);
}

// ============================================================================
// AV1: factory supports/create/reject
// ============================================================================
TEST_CASE("Av1CodecFactory: supports matching info", "[av1][factory]")
{
  Av1CodecFactory factory(Av1CodecFactory::makeAv1Info());

  CHECK(factory.supports(Av1CodecFactory::makeAv1Info()));

  CodecInfo wrong;
  wrong.name = "H264";
  wrong.clockRate = 90000;
  wrong.channels = 0;
  CHECK_FALSE(factory.supports(wrong));
}

TEST_CASE("Av1CodecFactory: createEncoder and createDecoder", "[av1][factory]")
{
  Av1CodecFactory factory(Av1CodecFactory::makeAv1Info());

  auto encoder = factory.createEncoder(Av1CodecFactory::makeAv1Info());
  REQUIRE(encoder != nullptr);
  CHECK(encoder->info().name == "AV1");

  auto decoder = factory.createDecoder(Av1CodecFactory::makeAv1Info());
  REQUIRE(decoder != nullptr);
  CHECK(decoder->info().name == "AV1");
}

TEST_CASE("Av1CodecFactory: create rejects unsupported params", "[av1][factory]")
{
  Av1CodecFactory factory(Av1CodecFactory::makeAv1Info());

  CodecInfo wrong;
  wrong.name = "VP9";
  wrong.clockRate = 90000;
  wrong.channels = 0;

  CHECK(factory.createEncoder(wrong) == nullptr);
  CHECK(factory.createDecoder(wrong) == nullptr);
}

// ============================================================================
// AV1: wrong-mode operations return nullptr
// ============================================================================
TEST_CASE("Av1Codec: wrong-mode operations return nullptr", "[av1][codec]")
{
  auto info = Av1CodecFactory::makeAv1Info();
  Av1Codec encoder(info, Av1Mode::Encoder, 320, 240);
  Av1Codec decoder(info, Av1Mode::Decoder, 320, 240);

  auto frame = makeI420Frame(320, 240);

  // Decoder can't encode
  CHECK(decoder.encode(*frame) == nullptr);

  // Encoder can't decode
  auto encoded = encoder.encode(*frame);
  REQUIRE(encoded != nullptr);
  CHECK(encoder.decode(*encoded) == nullptr);
}

// ============================================================================
// AV1: plc returns nullptr for video
// ============================================================================
TEST_CASE("Av1Codec: plc returns nullptr", "[av1][codec]")
{
  auto info = Av1CodecFactory::makeAv1Info();
  Av1Codec encoder(info, Av1Mode::Encoder, 320, 240);
  Av1Codec decoder(info, Av1Mode::Decoder, 320, 240);

  CHECK(encoder.plc(320 * 240) == nullptr);
  CHECK(decoder.plc(320 * 240) == nullptr);
}

// ============================================================================
// AV1: undersized encode input returns nullptr
// ============================================================================
TEST_CASE("Av1Codec: undersized encode input returns nullptr", "[av1][codec]")
{
  auto info = Av1CodecFactory::makeAv1Info();
  Av1Codec encoder(info, Av1Mode::Encoder, 320, 240);

  auto empty = MediaBuffer::create(0);
  empty->setSize(0);
  CHECK(encoder.encode(*empty) == nullptr);

  auto small = MediaBuffer::create(1000);
  small->setSize(1000);
  CHECK(encoder.encode(*small) == nullptr);
}

// ============================================================================
// AV1: zero-size decode returns nullptr
// ============================================================================
TEST_CASE("Av1Codec: zero-size decode returns nullptr", "[av1][codec]")
{
  auto info = Av1CodecFactory::makeAv1Info();
  Av1Codec decoder(info, Av1Mode::Decoder, 320, 240);

  auto empty = MediaBuffer::create(0);
  empty->setSize(0);
  CHECK(decoder.decode(*empty) == nullptr);
}

// ============================================================================
// AV1: setParameter on decoder returns false
// ============================================================================
TEST_CASE("Av1Codec: setParameter on decoder returns false", "[av1][codec]")
{
  auto info = Av1CodecFactory::makeAv1Info();
  Av1Codec decoder(info, Av1Mode::Decoder, 320, 240);

  CHECK_FALSE(decoder.setParameter("bitrate", 250000));
  CHECK_FALSE(decoder.setParameter("speed", 5));
  CHECK_FALSE(decoder.setParameter("requestKeyFrame", 1));
}

// ============================================================================
// AV1: unknown parameter
// ============================================================================
TEST_CASE("Av1Codec: unknown parameter returns false", "[av1][codec]")
{
  auto info = Av1CodecFactory::makeAv1Info();
  Av1Codec encoder(info, Av1Mode::Encoder, 320, 240);

  CHECK_FALSE(encoder.setParameter("nonexistent", 42));
  CHECK(encoder.getParameter("nonexistent") == 0);
}

// ============================================================================
// AV1: getParameter width/height
// ============================================================================
TEST_CASE("Av1Codec: getParameter width/height", "[av1][codec]")
{
  auto info = Av1CodecFactory::makeAv1Info();
  Av1Codec encoder(info, Av1Mode::Encoder, 640, 480);

  CHECK(encoder.getParameter("width") == 640);
  CHECK(encoder.getParameter("height") == 480);
}

// ============================================================================
// AV1: metadata copied through encode/decode
// ============================================================================
TEST_CASE("Av1Codec: metadata copied through encode/decode", "[av1][codec]")
{
  auto info = Av1CodecFactory::makeAv1Info();
  Av1Codec encoder(info, Av1Mode::Encoder, 320, 240, 200000, 30.0f, 8);
  Av1Codec decoder(info, Av1Mode::Decoder, 320, 240);

  auto frame = makeI420Frame(320, 240);
  frame->setTimestamp(90000);
  frame->setSequenceNumber(42);
  frame->setSsrc(0xDEADBEEF);
  frame->setPayloadType(96);
  frame->setMarker(true);

  std::shared_ptr<MediaBuffer> encoded;
  std::shared_ptr<MediaBuffer> decoded;

  for (int i = 0; i < 15; ++i)
  {
    encoded = encoder.encode(*frame);
    if (encoded != nullptr)
    {
      CHECK(encoded->timestamp() == 90000);
      CHECK(encoded->sequenceNumber() == 42);
      CHECK(encoded->ssrc() == 0xDEADBEEF);
      CHECK(encoded->payloadType() == 96);
      CHECK(encoded->marker() == true);

      decoded = decoder.decode(*encoded);
      if (decoded != nullptr)
      {
        break;
      }
    }
  }

  REQUIRE(decoded != nullptr);
  CHECK(decoded->timestamp() == 90000);
  CHECK(decoded->sequenceNumber() == 42);
  CHECK(decoded->ssrc() == 0xDEADBEEF);
  CHECK(decoded->payloadType() == 96);
  CHECK(decoded->marker() == true);
}

// ============================================================================
// AV1: decoded output has video metadata
// ============================================================================
TEST_CASE("Av1Codec: decoded output has video metadata", "[av1][codec]")
{
  auto info = Av1CodecFactory::makeAv1Info();
  Av1Codec encoder(info, Av1Mode::Encoder, 320, 240, 200000, 30.0f, 8);
  Av1Codec decoder(info, Av1Mode::Decoder, 320, 240);

  auto frame = makeI420Frame(320, 240);

  std::shared_ptr<MediaBuffer> decoded;
  for (int i = 0; i < 15; ++i)
  {
    auto encoded = encoder.encode(*frame);
    if (encoded != nullptr)
    {
      decoded = decoder.decode(*encoded);
      if (decoded != nullptr)
      {
        break;
      }
    }
  }
  REQUIRE(decoded != nullptr);

  CHECK(decoded->width() == 320);
  CHECK(decoded->height() == 240);
  CHECK(decoded->stride(0) == 320);
  CHECK(decoded->stride(1) == 160);
  CHECK(decoded->stride(2) == 160);
  CHECK(decoded->pixelFormat() == PixelFormat::I420);
}

// ============================================================================
// AV1: multiple sequential frames
// ============================================================================
TEST_CASE("Av1Codec: multiple sequential frames", "[av1][codec]")
{
  auto info = Av1CodecFactory::makeAv1Info();
  Av1Codec encoder(info, Av1Mode::Encoder, 320, 240, 200000, 30.0f, 8);
  Av1Codec decoder(info, Av1Mode::Decoder, 320, 240);

  auto frame = makeI420Frame(320, 240);

  int decodedCount = 0;
  for (int i = 0; i < 30; ++i)
  {
    auto encoded = encoder.encode(*frame);
    if (encoded != nullptr)
    {
      auto decoded = decoder.decode(*encoded);
      if (decoded != nullptr)
      {
        CHECK(decoded->size() == 320u * 240 * 3 / 2);
        ++decodedCount;
      }
    }
  }

  INFO("Decoded " << decodedCount << " out of 30 frames");
  CHECK(decodedCount >= 15);
}

// ============================================================================
// AV1: garbage bitstream does not crash decoder
// ============================================================================
TEST_CASE("Av1Codec: garbage bitstream does not crash decoder", "[av1][codec]")
{
  auto info = Av1CodecFactory::makeAv1Info();
  Av1Codec decoder(info, Av1Mode::Decoder, 320, 240);

  auto garbage = MediaBuffer::create(1024);
  for (std::size_t i = 0; i < 1024; ++i)
  {
    garbage->data()[i] = static_cast<std::uint8_t>(i * 37 + 13);
  }
  garbage->setSize(1024);

  auto result = decoder.decode(*garbage);
  (void)result;
}

// ============================================================================
// AV1: odd dimensions rejected
// ============================================================================
TEST_CASE("Av1Codec: odd dimensions rejected", "[av1][codec]")
{
  auto info = Av1CodecFactory::makeAv1Info();

  CHECK_THROWS(Av1Codec(info, Av1Mode::Encoder, 321, 240));
  CHECK_THROWS(Av1Codec(info, Av1Mode::Encoder, 320, 241));
  CHECK_THROWS(Av1Codec(info, Av1Mode::Encoder, 321, 241));
}

// ============================================================================
// AV1: CodecRegistry integration
// ============================================================================
TEST_CASE("Av1CodecFactory: CodecRegistry integration", "[av1][registry]")
{
  CodecRegistry registry;

  auto factory = std::make_shared<Av1CodecFactory>(Av1CodecFactory::makeAv1Info());
  registry.registerFactory(factory);

  auto byName = registry.findByName("AV1");
  REQUIRE(byName.has_value());
  CHECK(byName->clockRate == 90000);
  CHECK(byName->type == CodecType::Video);

  auto encoder = registry.createEncoder(Av1CodecFactory::makeAv1Info());
  REQUIRE(encoder != nullptr);
  CHECK(encoder->info().name == "AV1");

  auto codecs = registry.enumerateCodecs();
  CHECK(codecs.size() == 1);
}
