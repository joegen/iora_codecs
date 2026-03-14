#include <catch2/catch_test_macros.hpp>

#include "h264/h264_codec.hpp"
#include "h264/h264_codec_factory.hpp"
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
// H.264: encode/decode round-trip
// ============================================================================
TEST_CASE("H264Codec: encode/decode round-trip", "[h264][codec]")
{
  auto info = H264CodecFactory::makeH264Info();

  constexpr std::uint32_t w = 320;
  constexpr std::uint32_t h = 240;

  H264Codec encoder(info, H264Mode::Encoder, w, h, 200000, 30.0f);
  H264Codec decoder(info, H264Mode::Decoder, w, h);

  auto frame = makeI420Frame(w, h);

  // Encode multiple frames — first few may be buffered
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

  // Check PSNR of Y plane — should be decent for a gradient at 200kbps
  double psnr = computePsnr(frame->data(), decoded->data(), static_cast<std::size_t>(w) * h);
  INFO("H264 round-trip PSNR = " << psnr << " dB");
  CHECK(psnr > 20.0);
}

// ============================================================================
// H.264: CodecInfo fields
// ============================================================================
TEST_CASE("H264Codec: CodecInfo fields", "[h264][codec]")
{
  auto info = H264CodecFactory::makeH264Info();
  CHECK(info.name == "H264");
  CHECK(info.type == CodecType::Video);
  CHECK(info.mediaSubtype == "H264");
  CHECK(info.clockRate == 90000);
  CHECK(info.channels == 0);
  CHECK(info.defaultPayloadType == 0);
  CHECK(info.defaultBitrate == 500000);
  CHECK(info.frameSize == std::chrono::microseconds{33333});
  CHECK((info.features & CodecFeatures::Cbr) != CodecFeatures::None);
  CHECK((info.features & CodecFeatures::Vbr) != CodecFeatures::None);
}

// ============================================================================
// H.264: encoder setParameter bitrate
// ============================================================================
TEST_CASE("H264Codec: setParameter bitrate", "[h264][codec]")
{
  auto info = H264CodecFactory::makeH264Info();
  H264Codec encoder(info, H264Mode::Encoder, 320, 240);

  CHECK(encoder.getParameter("bitrate") == 500000);

  CHECK(encoder.setParameter("bitrate", 250000));
  CHECK(encoder.getParameter("bitrate") == 250000);

  // Verify encoding still works after bitrate change
  auto frame = makeI420Frame(320, 240);
  auto encoded = encoder.encode(*frame);
  REQUIRE(encoded != nullptr);
  CHECK(encoded->size() > 0);
}

// ============================================================================
// H.264: encoder setParameter framerate
// ============================================================================
TEST_CASE("H264Codec: setParameter framerate", "[h264][codec]")
{
  auto info = H264CodecFactory::makeH264Info();
  H264Codec encoder(info, H264Mode::Encoder, 320, 240);

  CHECK(encoder.getParameter("framerate") == 30);

  CHECK(encoder.setParameter("framerate", 15));
  CHECK(encoder.getParameter("framerate") == 15);
}

// ============================================================================
// H.264: encoder requestKeyFrame
// ============================================================================
TEST_CASE("H264Codec: requestKeyFrame produces IDR", "[h264][codec]")
{
  auto info = H264CodecFactory::makeH264Info();
  H264Codec encoder(info, H264Mode::Encoder, 320, 240, 500000, 30.0f);
  H264Codec decoder(info, H264Mode::Decoder, 320, 240);

  auto frame = makeI420Frame(320, 240);

  // Encode several frames to get past initial IDR
  for (int i = 0; i < 10; ++i)
  {
    auto enc = encoder.encode(*frame);
    if (enc != nullptr)
    {
      decoder.decode(*enc);
    }
  }

  // Request key frame
  CHECK(encoder.setParameter("requestKeyFrame", 1));

  // Next encoded frame should be an IDR — verify it decodes
  auto encoded = encoder.encode(*frame);
  REQUIRE(encoded != nullptr);
  CHECK(encoded->size() > 0);

  auto decoded = decoder.decode(*encoded);
  REQUIRE(decoded != nullptr);
}

// ============================================================================
// H.264: factory supports/create/reject
// ============================================================================
TEST_CASE("H264CodecFactory: supports matching info", "[h264][factory]")
{
  H264CodecFactory factory(H264CodecFactory::makeH264Info());

  CHECK(factory.supports(H264CodecFactory::makeH264Info()));

  CodecInfo wrong;
  wrong.name = "VP8";
  wrong.clockRate = 90000;
  wrong.channels = 0;
  CHECK_FALSE(factory.supports(wrong));
}

TEST_CASE("H264CodecFactory: createEncoder and createDecoder", "[h264][factory]")
{
  H264CodecFactory factory(H264CodecFactory::makeH264Info());

  auto encoder = factory.createEncoder(H264CodecFactory::makeH264Info());
  REQUIRE(encoder != nullptr);
  CHECK(encoder->info().name == "H264");

  auto decoder = factory.createDecoder(H264CodecFactory::makeH264Info());
  REQUIRE(decoder != nullptr);
  CHECK(decoder->info().name == "H264");
}

TEST_CASE("H264CodecFactory: create rejects unsupported params", "[h264][factory]")
{
  H264CodecFactory factory(H264CodecFactory::makeH264Info());

  CodecInfo wrong;
  wrong.name = "VP9";
  wrong.clockRate = 90000;
  wrong.channels = 0;

  CHECK(factory.createEncoder(wrong) == nullptr);
  CHECK(factory.createDecoder(wrong) == nullptr);
}

// ============================================================================
// H.264: wrong-mode operations return nullptr
// ============================================================================
TEST_CASE("H264Codec: wrong-mode operations return nullptr", "[h264][codec]")
{
  auto info = H264CodecFactory::makeH264Info();
  H264Codec encoder(info, H264Mode::Encoder, 320, 240);
  H264Codec decoder(info, H264Mode::Decoder, 320, 240);

  auto frame = makeI420Frame(320, 240);

  // Decoder can't encode
  CHECK(decoder.encode(*frame) == nullptr);

  // Encoder can't decode
  auto encoded = encoder.encode(*frame);
  REQUIRE(encoded != nullptr);
  CHECK(encoder.decode(*encoded) == nullptr);
}

// ============================================================================
// H.264: plc returns nullptr for video
// ============================================================================
TEST_CASE("H264Codec: plc returns nullptr", "[h264][codec]")
{
  auto info = H264CodecFactory::makeH264Info();
  H264Codec encoder(info, H264Mode::Encoder, 320, 240);
  H264Codec decoder(info, H264Mode::Decoder, 320, 240);

  CHECK(encoder.plc(320 * 240) == nullptr);
  CHECK(decoder.plc(320 * 240) == nullptr);
}

// ============================================================================
// H.264: zero/undersized encode input returns nullptr
// ============================================================================
TEST_CASE("H264Codec: undersized encode input returns nullptr", "[h264][codec]")
{
  auto info = H264CodecFactory::makeH264Info();
  H264Codec encoder(info, H264Mode::Encoder, 320, 240);

  // Empty buffer
  auto empty = MediaBuffer::create(0);
  empty->setSize(0);
  CHECK(encoder.encode(*empty) == nullptr);

  // Too small for 320x240 I420 (need 115200 bytes)
  auto small = MediaBuffer::create(1000);
  small->setSize(1000);
  CHECK(encoder.encode(*small) == nullptr);
}

// ============================================================================
// H.264: zero-size decode returns nullptr
// ============================================================================
TEST_CASE("H264Codec: zero-size decode returns nullptr", "[h264][codec]")
{
  auto info = H264CodecFactory::makeH264Info();
  H264Codec decoder(info, H264Mode::Decoder, 320, 240);

  auto empty = MediaBuffer::create(0);
  empty->setSize(0);
  CHECK(decoder.decode(*empty) == nullptr);
}

// ============================================================================
// H.264: setParameter on decoder returns false
// ============================================================================
TEST_CASE("H264Codec: setParameter on decoder returns false", "[h264][codec]")
{
  auto info = H264CodecFactory::makeH264Info();
  H264Codec decoder(info, H264Mode::Decoder, 320, 240);

  CHECK_FALSE(decoder.setParameter("bitrate", 250000));
  CHECK_FALSE(decoder.setParameter("framerate", 15));
  CHECK_FALSE(decoder.setParameter("requestKeyFrame", 1));
}

// ============================================================================
// H.264: unknown parameter
// ============================================================================
TEST_CASE("H264Codec: unknown parameter returns false", "[h264][codec]")
{
  auto info = H264CodecFactory::makeH264Info();
  H264Codec encoder(info, H264Mode::Encoder, 320, 240);

  CHECK_FALSE(encoder.setParameter("nonexistent", 42));
  CHECK(encoder.getParameter("nonexistent") == 0);
}

// ============================================================================
// H.264: getParameter width/height
// ============================================================================
TEST_CASE("H264Codec: getParameter width/height", "[h264][codec]")
{
  auto info = H264CodecFactory::makeH264Info();
  H264Codec encoder(info, H264Mode::Encoder, 640, 480);

  CHECK(encoder.getParameter("width") == 640);
  CHECK(encoder.getParameter("height") == 480);
}

// ============================================================================
// H.264: metadata copied through encode/decode
// ============================================================================
TEST_CASE("H264Codec: metadata copied through encode/decode", "[h264][codec]")
{
  auto info = H264CodecFactory::makeH264Info();
  H264Codec encoder(info, H264Mode::Encoder, 320, 240, 200000, 30.0f);
  H264Codec decoder(info, H264Mode::Decoder, 320, 240);

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
// H.264: decoded output has video metadata
// ============================================================================
TEST_CASE("H264Codec: decoded output has video metadata", "[h264][codec]")
{
  auto info = H264CodecFactory::makeH264Info();
  H264Codec encoder(info, H264Mode::Encoder, 320, 240, 200000, 30.0f);
  H264Codec decoder(info, H264Mode::Decoder, 320, 240);

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
// H.264: sequential frames
// ============================================================================
TEST_CASE("H264Codec: multiple sequential frames", "[h264][codec]")
{
  auto info = H264CodecFactory::makeH264Info();
  H264Codec encoder(info, H264Mode::Encoder, 320, 240, 200000, 30.0f);
  H264Codec decoder(info, H264Mode::Decoder, 320, 240);

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

  // Should have decoded most frames
  INFO("Decoded " << decodedCount << " out of 30 frames");
  CHECK(decodedCount >= 25);
}

// ============================================================================
// H.264: CodecRegistry integration
// ============================================================================
TEST_CASE("H264CodecFactory: CodecRegistry integration", "[h264][registry]")
{
  CodecRegistry registry;

  auto factory = std::make_shared<H264CodecFactory>(H264CodecFactory::makeH264Info());
  registry.registerFactory(factory);

  auto byName = registry.findByName("H264");
  REQUIRE(byName.has_value());
  CHECK(byName->clockRate == 90000);
  CHECK(byName->type == CodecType::Video);
  CHECK(byName->channels == 0);

  auto encoder = registry.createEncoder(H264CodecFactory::makeH264Info());
  REQUIRE(encoder != nullptr);
  CHECK(encoder->info().name == "H264");

  auto codecs = registry.enumerateCodecs();
  CHECK(codecs.size() == 1);
}

// ============================================================================
// H.264: garbage bitstream does not crash decoder
// ============================================================================
TEST_CASE("H264Codec: garbage bitstream does not crash decoder", "[h264][codec]")
{
  auto info = H264CodecFactory::makeH264Info();
  H264Codec decoder(info, H264Mode::Decoder, 320, 240);

  // Feed random bytes — decoder should return nullptr, not crash
  auto garbage = MediaBuffer::create(1024);
  for (std::size_t i = 0; i < 1024; ++i)
  {
    garbage->data()[i] = static_cast<std::uint8_t>(i * 37 + 13);
  }
  garbage->setSize(1024);

  auto result = decoder.decode(*garbage);
  // May or may not produce output, but must not crash
  (void)result;
}

// ============================================================================
// H.264: odd dimensions rejected
// ============================================================================
TEST_CASE("H264Codec: odd dimensions rejected", "[h264][codec]")
{
  auto info = H264CodecFactory::makeH264Info();

  CHECK_THROWS(H264Codec(info, H264Mode::Encoder, 321, 240));
  CHECK_THROWS(H264Codec(info, H264Mode::Encoder, 320, 241));
  CHECK_THROWS(H264Codec(info, H264Mode::Encoder, 321, 241));
}
