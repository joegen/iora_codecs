#include <catch2/catch_test_macros.hpp>

#include "vpx/vpx_codec.hpp"
#include "vpx/vpx_codec_factory.hpp"
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
// VP8: encode/decode round-trip
// ============================================================================
TEST_CASE("VP8Codec: encode/decode round-trip", "[vp8][codec]")
{
  auto info = VpxCodecFactory::makeVp8Info();

  constexpr std::uint32_t w = 320;
  constexpr std::uint32_t h = 240;

  VpxCodec encoder(info, VpxMode::Encoder, VpxVariant::VP8, w, h, 200000, 30.0f, 6);
  VpxCodec decoder(info, VpxMode::Decoder, VpxVariant::VP8, w, h);

  auto frame = makeI420Frame(w, h);

  std::shared_ptr<MediaBuffer> encoded;
  std::shared_ptr<MediaBuffer> decoded;

  for (int i = 0; i < 5; ++i)
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
  INFO("VP8 round-trip PSNR = " << psnr << " dB");
  CHECK(psnr > 20.0);
}

// ============================================================================
// VP8: CodecInfo fields
// ============================================================================
TEST_CASE("VP8Codec: CodecInfo fields", "[vp8][codec]")
{
  auto info = VpxCodecFactory::makeVp8Info();
  CHECK(info.name == "VP8");
  CHECK(info.type == CodecType::Video);
  CHECK(info.mediaSubtype == "VP8");
  CHECK(info.clockRate == 90000);
  CHECK(info.channels == 0);
  CHECK(info.defaultPayloadType == 0);
  CHECK(info.defaultBitrate == 500000);
  CHECK(info.frameSize == std::chrono::microseconds{33333});
  CHECK((info.features & CodecFeatures::Cbr) != CodecFeatures::None);
  CHECK((info.features & CodecFeatures::Vbr) != CodecFeatures::None);
}

// ============================================================================
// VP8: setParameter bitrate
// ============================================================================
TEST_CASE("VP8Codec: setParameter bitrate", "[vp8][codec]")
{
  auto info = VpxCodecFactory::makeVp8Info();
  VpxCodec encoder(info, VpxMode::Encoder, VpxVariant::VP8, 320, 240);

  CHECK(encoder.getParameter("bitrate") == 500000);

  CHECK(encoder.setParameter("bitrate", 250000));
  CHECK(encoder.getParameter("bitrate") == 250000);

  auto frame = makeI420Frame(320, 240);
  auto encoded = encoder.encode(*frame);
  REQUIRE(encoded != nullptr);
  CHECK(encoded->size() > 0);
}

// ============================================================================
// VP8: setParameter speed
// ============================================================================
TEST_CASE("VP8Codec: setParameter speed", "[vp8][codec]")
{
  auto info = VpxCodecFactory::makeVp8Info();
  VpxCodec encoder(info, VpxMode::Encoder, VpxVariant::VP8, 320, 240, 500000, 30.0f, 6);

  CHECK(encoder.getParameter("speed") == 6);

  CHECK(encoder.setParameter("speed", 10));
  CHECK(encoder.getParameter("speed") == 10);
}

// ============================================================================
// VP8: setParameter framerate
// ============================================================================
TEST_CASE("VP8Codec: setParameter framerate", "[vp8][codec]")
{
  auto info = VpxCodecFactory::makeVp8Info();
  VpxCodec encoder(info, VpxMode::Encoder, VpxVariant::VP8, 320, 240);

  CHECK(encoder.getParameter("framerate") == 30);

  CHECK(encoder.setParameter("framerate", 15));
  CHECK(encoder.getParameter("framerate") == 15);
}

// ============================================================================
// VP8: requestKeyFrame produces keyframe
// ============================================================================
TEST_CASE("VP8Codec: requestKeyFrame produces keyframe", "[vp8][codec]")
{
  auto info = VpxCodecFactory::makeVp8Info();
  VpxCodec encoder(info, VpxMode::Encoder, VpxVariant::VP8, 320, 240, 500000, 30.0f, 6);
  VpxCodec decoder(info, VpxMode::Decoder, VpxVariant::VP8, 320, 240);

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
// VP8: factory supports/create/reject
// ============================================================================
TEST_CASE("VP8CodecFactory: supports matching info", "[vp8][factory]")
{
  VpxCodecFactory factory(VpxCodecFactory::makeVp8Info(), VpxVariant::VP8);

  CHECK(factory.supports(VpxCodecFactory::makeVp8Info()));

  CodecInfo wrong;
  wrong.name = "H264";
  wrong.clockRate = 90000;
  wrong.channels = 0;
  CHECK_FALSE(factory.supports(wrong));
}

TEST_CASE("VP8CodecFactory: createEncoder and createDecoder", "[vp8][factory]")
{
  VpxCodecFactory factory(VpxCodecFactory::makeVp8Info(), VpxVariant::VP8);

  auto encoder = factory.createEncoder(VpxCodecFactory::makeVp8Info());
  REQUIRE(encoder != nullptr);
  CHECK(encoder->info().name == "VP8");

  auto decoder = factory.createDecoder(VpxCodecFactory::makeVp8Info());
  REQUIRE(decoder != nullptr);
  CHECK(decoder->info().name == "VP8");
}

TEST_CASE("VP8CodecFactory: create rejects unsupported params", "[vp8][factory]")
{
  VpxCodecFactory factory(VpxCodecFactory::makeVp8Info(), VpxVariant::VP8);

  CodecInfo wrong;
  wrong.name = "VP9";
  wrong.clockRate = 90000;
  wrong.channels = 0;

  CHECK(factory.createEncoder(wrong) == nullptr);
  CHECK(factory.createDecoder(wrong) == nullptr);
}

// ============================================================================
// VP8: wrong-mode operations return nullptr
// ============================================================================
TEST_CASE("VP8Codec: wrong-mode operations return nullptr", "[vp8][codec]")
{
  auto info = VpxCodecFactory::makeVp8Info();
  VpxCodec encoder(info, VpxMode::Encoder, VpxVariant::VP8, 320, 240);
  VpxCodec decoder(info, VpxMode::Decoder, VpxVariant::VP8, 320, 240);

  auto frame = makeI420Frame(320, 240);

  // Decoder can't encode
  CHECK(decoder.encode(*frame) == nullptr);

  // Encoder can't decode
  auto encoded = encoder.encode(*frame);
  REQUIRE(encoded != nullptr);
  CHECK(encoder.decode(*encoded) == nullptr);
}

// ============================================================================
// VP8: plc returns nullptr for video
// ============================================================================
TEST_CASE("VP8Codec: plc returns nullptr", "[vp8][codec]")
{
  auto info = VpxCodecFactory::makeVp8Info();
  VpxCodec encoder(info, VpxMode::Encoder, VpxVariant::VP8, 320, 240);
  VpxCodec decoder(info, VpxMode::Decoder, VpxVariant::VP8, 320, 240);

  CHECK(encoder.plc(320 * 240) == nullptr);
  CHECK(decoder.plc(320 * 240) == nullptr);
}

// ============================================================================
// VP8: undersized encode input returns nullptr
// ============================================================================
TEST_CASE("VP8Codec: undersized encode input returns nullptr", "[vp8][codec]")
{
  auto info = VpxCodecFactory::makeVp8Info();
  VpxCodec encoder(info, VpxMode::Encoder, VpxVariant::VP8, 320, 240);

  auto empty = MediaBuffer::create(0);
  empty->setSize(0);
  CHECK(encoder.encode(*empty) == nullptr);

  auto small = MediaBuffer::create(1000);
  small->setSize(1000);
  CHECK(encoder.encode(*small) == nullptr);
}

// ============================================================================
// VP8: zero-size decode returns nullptr
// ============================================================================
TEST_CASE("VP8Codec: zero-size decode returns nullptr", "[vp8][codec]")
{
  auto info = VpxCodecFactory::makeVp8Info();
  VpxCodec decoder(info, VpxMode::Decoder, VpxVariant::VP8, 320, 240);

  auto empty = MediaBuffer::create(0);
  empty->setSize(0);
  CHECK(decoder.decode(*empty) == nullptr);
}

// ============================================================================
// VP8: setParameter on decoder returns false
// ============================================================================
TEST_CASE("VP8Codec: setParameter on decoder returns false", "[vp8][codec]")
{
  auto info = VpxCodecFactory::makeVp8Info();
  VpxCodec decoder(info, VpxMode::Decoder, VpxVariant::VP8, 320, 240);

  CHECK_FALSE(decoder.setParameter("bitrate", 250000));
  CHECK_FALSE(decoder.setParameter("speed", 5));
  CHECK_FALSE(decoder.setParameter("requestKeyFrame", 1));
}

// ============================================================================
// VP8: unknown parameter
// ============================================================================
TEST_CASE("VP8Codec: unknown parameter returns false", "[vp8][codec]")
{
  auto info = VpxCodecFactory::makeVp8Info();
  VpxCodec encoder(info, VpxMode::Encoder, VpxVariant::VP8, 320, 240);

  CHECK_FALSE(encoder.setParameter("nonexistent", 42));
  CHECK(encoder.getParameter("nonexistent") == 0);
}

// ============================================================================
// VP8: getParameter width/height
// ============================================================================
TEST_CASE("VP8Codec: getParameter width/height", "[vp8][codec]")
{
  auto info = VpxCodecFactory::makeVp8Info();
  VpxCodec encoder(info, VpxMode::Encoder, VpxVariant::VP8, 640, 480);

  CHECK(encoder.getParameter("width") == 640);
  CHECK(encoder.getParameter("height") == 480);
}

// ============================================================================
// VP8: metadata copied through encode/decode
// ============================================================================
TEST_CASE("VP8Codec: metadata copied through encode/decode", "[vp8][codec]")
{
  auto info = VpxCodecFactory::makeVp8Info();
  VpxCodec encoder(info, VpxMode::Encoder, VpxVariant::VP8, 320, 240, 200000, 30.0f, 6);
  VpxCodec decoder(info, VpxMode::Decoder, VpxVariant::VP8, 320, 240);

  auto frame = makeI420Frame(320, 240);
  frame->setTimestamp(90000);
  frame->setSequenceNumber(42);
  frame->setSsrc(0xDEADBEEF);
  frame->setPayloadType(96);
  frame->setMarker(true);

  auto encoded = encoder.encode(*frame);
  REQUIRE(encoded != nullptr);
  CHECK(encoded->timestamp() == 90000);
  CHECK(encoded->sequenceNumber() == 42);
  CHECK(encoded->ssrc() == 0xDEADBEEF);
  CHECK(encoded->payloadType() == 96);
  CHECK(encoded->marker() == true);

  auto decoded = decoder.decode(*encoded);
  REQUIRE(decoded != nullptr);
  CHECK(decoded->timestamp() == 90000);
  CHECK(decoded->sequenceNumber() == 42);
  CHECK(decoded->ssrc() == 0xDEADBEEF);
  CHECK(decoded->payloadType() == 96);
  CHECK(decoded->marker() == true);
}

// ============================================================================
// VP8: decoded output has video metadata
// ============================================================================
TEST_CASE("VP8Codec: decoded output has video metadata", "[vp8][codec]")
{
  auto info = VpxCodecFactory::makeVp8Info();
  VpxCodec encoder(info, VpxMode::Encoder, VpxVariant::VP8, 320, 240, 200000, 30.0f, 6);
  VpxCodec decoder(info, VpxMode::Decoder, VpxVariant::VP8, 320, 240);

  auto frame = makeI420Frame(320, 240);

  std::shared_ptr<MediaBuffer> decoded;
  for (int i = 0; i < 5; ++i)
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
// VP8: multiple sequential frames
// ============================================================================
TEST_CASE("VP8Codec: multiple sequential frames", "[vp8][codec]")
{
  auto info = VpxCodecFactory::makeVp8Info();
  VpxCodec encoder(info, VpxMode::Encoder, VpxVariant::VP8, 320, 240, 200000, 30.0f, 6);
  VpxCodec decoder(info, VpxMode::Decoder, VpxVariant::VP8, 320, 240);

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
  CHECK(decodedCount >= 25);
}

// ============================================================================
// VP8: garbage bitstream does not crash decoder
// ============================================================================
TEST_CASE("VP8Codec: garbage bitstream does not crash decoder", "[vp8][codec]")
{
  auto info = VpxCodecFactory::makeVp8Info();
  VpxCodec decoder(info, VpxMode::Decoder, VpxVariant::VP8, 320, 240);

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
// VP8: odd dimensions rejected
// ============================================================================
TEST_CASE("VP8Codec: odd dimensions rejected", "[vp8][codec]")
{
  auto info = VpxCodecFactory::makeVp8Info();

  CHECK_THROWS(VpxCodec(info, VpxMode::Encoder, VpxVariant::VP8, 321, 240));
  CHECK_THROWS(VpxCodec(info, VpxMode::Encoder, VpxVariant::VP8, 320, 241));
  CHECK_THROWS(VpxCodec(info, VpxMode::Encoder, VpxVariant::VP8, 321, 241));
}

// ============================================================================
// VP9: encode/decode round-trip
// ============================================================================
TEST_CASE("VP9Codec: encode/decode round-trip", "[vp9][codec]")
{
  auto info = VpxCodecFactory::makeVp9Info();

  constexpr std::uint32_t w = 320;
  constexpr std::uint32_t h = 240;

  VpxCodec encoder(info, VpxMode::Encoder, VpxVariant::VP9, w, h, 200000, 30.0f, 7);
  VpxCodec decoder(info, VpxMode::Decoder, VpxVariant::VP9, w, h);

  auto frame = makeI420Frame(w, h);

  std::shared_ptr<MediaBuffer> encoded;
  std::shared_ptr<MediaBuffer> decoded;

  for (int i = 0; i < 10; ++i)
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
  INFO("VP9 round-trip PSNR = " << psnr << " dB");
  CHECK(psnr > 20.0);
}

// ============================================================================
// VP9: CodecInfo fields
// ============================================================================
TEST_CASE("VP9Codec: CodecInfo fields", "[vp9][codec]")
{
  auto info = VpxCodecFactory::makeVp9Info();
  CHECK(info.name == "VP9");
  CHECK(info.type == CodecType::Video);
  CHECK(info.mediaSubtype == "VP9");
  CHECK(info.clockRate == 90000);
  CHECK(info.channels == 0);
  CHECK(info.defaultPayloadType == 0);
  CHECK(info.defaultBitrate == 300000);
  CHECK((info.features & CodecFeatures::Cbr) != CodecFeatures::None);
  CHECK((info.features & CodecFeatures::Vbr) != CodecFeatures::None);
}

// ============================================================================
// VP9: setParameter bitrate
// ============================================================================
TEST_CASE("VP9Codec: setParameter bitrate", "[vp9][codec]")
{
  auto info = VpxCodecFactory::makeVp9Info();
  VpxCodec encoder(info, VpxMode::Encoder, VpxVariant::VP9, 320, 240, 300000, 30.0f, 7);

  CHECK(encoder.getParameter("bitrate") == 300000);

  CHECK(encoder.setParameter("bitrate", 150000));
  CHECK(encoder.getParameter("bitrate") == 150000);
}

// ============================================================================
// VP9: sequential frames
// ============================================================================
TEST_CASE("VP9Codec: multiple sequential frames", "[vp9][codec]")
{
  auto info = VpxCodecFactory::makeVp9Info();
  VpxCodec encoder(info, VpxMode::Encoder, VpxVariant::VP9, 320, 240, 200000, 30.0f, 7);
  VpxCodec decoder(info, VpxMode::Decoder, VpxVariant::VP9, 320, 240);

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
  CHECK(decodedCount >= 25);
}

// ============================================================================
// VP9: factory supports matching / rejects wrong
// ============================================================================
TEST_CASE("VP9CodecFactory: supports matching / rejects wrong", "[vp9][factory]")
{
  VpxCodecFactory factory(VpxCodecFactory::makeVp9Info(), VpxVariant::VP9);

  CHECK(factory.supports(VpxCodecFactory::makeVp9Info()));
  CHECK_FALSE(factory.supports(VpxCodecFactory::makeVp8Info()));

  auto encoder = factory.createEncoder(VpxCodecFactory::makeVp9Info());
  REQUIRE(encoder != nullptr);
  CHECK(encoder->info().name == "VP9");

  CHECK(factory.createEncoder(VpxCodecFactory::makeVp8Info()) == nullptr);
}

// ============================================================================
// VP8 + VP9: CodecRegistry integration
// ============================================================================
TEST_CASE("VpxCodecFactory: CodecRegistry integration", "[vpx][registry]")
{
  CodecRegistry registry;

  auto vp8Factory = std::make_shared<VpxCodecFactory>(VpxCodecFactory::makeVp8Info(),
                                                       VpxVariant::VP8);
  auto vp9Factory = std::make_shared<VpxCodecFactory>(VpxCodecFactory::makeVp9Info(),
                                                       VpxVariant::VP9);
  registry.registerFactory(vp8Factory);
  registry.registerFactory(vp9Factory);

  auto byVp8 = registry.findByName("VP8");
  REQUIRE(byVp8.has_value());
  CHECK(byVp8->clockRate == 90000);
  CHECK(byVp8->type == CodecType::Video);

  auto byVp9 = registry.findByName("VP9");
  REQUIRE(byVp9.has_value());
  CHECK(byVp9->clockRate == 90000);
  CHECK(byVp9->type == CodecType::Video);

  auto encoder = registry.createEncoder(VpxCodecFactory::makeVp8Info());
  REQUIRE(encoder != nullptr);
  CHECK(encoder->info().name == "VP8");

  auto codecs = registry.enumerateCodecs();
  CHECK(codecs.size() == 2);
}
