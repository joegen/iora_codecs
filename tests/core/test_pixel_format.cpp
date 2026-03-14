#include <catch2/catch_test_macros.hpp>

#include "iora/codecs/format/pixel_format.hpp"

#include <string>

using namespace iora::codecs;

TEST_CASE("PixelFormat: None format", "[format][pixel]")
{
  CHECK_FALSE(isPlanar(PixelFormat::None));
  CHECK(bytesPerPixel(PixelFormat::None) == 0);
  CHECK(bytesPerFrame(PixelFormat::None, 640, 480) == 0);
  auto csNone = chromaSubsampling(PixelFormat::None);
  CHECK(csNone.horizontal == 1);
  CHECK(csNone.vertical == 1);
  CHECK(std::string(pixelFormatToString(PixelFormat::None)) == "None");
}

TEST_CASE("PixelFormat: isPlanar", "[format][pixel]")
{
  CHECK(isPlanar(PixelFormat::I420));
  CHECK(isPlanar(PixelFormat::NV12));
  CHECK(isPlanar(PixelFormat::NV21));
  CHECK(isPlanar(PixelFormat::P010));

  CHECK_FALSE(isPlanar(PixelFormat::YUY2));
  CHECK_FALSE(isPlanar(PixelFormat::UYVY));
  CHECK_FALSE(isPlanar(PixelFormat::RGB24));
  CHECK_FALSE(isPlanar(PixelFormat::BGR24));
  CHECK_FALSE(isPlanar(PixelFormat::RGBA32));
  CHECK_FALSE(isPlanar(PixelFormat::BGRA32));
}

TEST_CASE("PixelFormat: bytesPerPixel for packed formats", "[format][pixel]")
{
  CHECK(bytesPerPixel(PixelFormat::YUY2) == 2);
  CHECK(bytesPerPixel(PixelFormat::UYVY) == 2);
  CHECK(bytesPerPixel(PixelFormat::RGB24) == 3);
  CHECK(bytesPerPixel(PixelFormat::BGR24) == 3);
  CHECK(bytesPerPixel(PixelFormat::RGBA32) == 4);
  CHECK(bytesPerPixel(PixelFormat::BGRA32) == 4);
}

TEST_CASE("PixelFormat: bytesPerPixel returns 0 for planar", "[format][pixel]")
{
  CHECK(bytesPerPixel(PixelFormat::I420) == 0);
  CHECK(bytesPerPixel(PixelFormat::NV12) == 0);
  CHECK(bytesPerPixel(PixelFormat::NV21) == 0);
  CHECK(bytesPerPixel(PixelFormat::P010) == 0);
}

TEST_CASE("PixelFormat: chromaSubsampling", "[format][pixel]")
{
  // 4:2:0
  auto cs420 = chromaSubsampling(PixelFormat::I420);
  CHECK(cs420.horizontal == 2);
  CHECK(cs420.vertical == 2);

  auto csNv12 = chromaSubsampling(PixelFormat::NV12);
  CHECK(csNv12.horizontal == 2);
  CHECK(csNv12.vertical == 2);

  auto csP010 = chromaSubsampling(PixelFormat::P010);
  CHECK(csP010.horizontal == 2);
  CHECK(csP010.vertical == 2);

  // 4:2:2
  auto cs422 = chromaSubsampling(PixelFormat::YUY2);
  CHECK(cs422.horizontal == 2);
  CHECK(cs422.vertical == 1);

  // 4:4:4 (RGB)
  auto cs444 = chromaSubsampling(PixelFormat::RGB24);
  CHECK(cs444.horizontal == 1);
  CHECK(cs444.vertical == 1);
}

TEST_CASE("PixelFormat: bytesPerFrame", "[format][pixel]")
{
  // I420 640x480: Y=640*480 + U=320*240 + V=320*240 = 460800
  CHECK(bytesPerFrame(PixelFormat::I420, 640, 480) == 460800);

  // NV12 same dimensions
  CHECK(bytesPerFrame(PixelFormat::NV12, 640, 480) == 460800);

  // P010 640x480: same layout as I420 but 2 bytes per component
  CHECK(bytesPerFrame(PixelFormat::P010, 640, 480) == 921600);

  // YUY2 640x480: 2 bytes per pixel
  CHECK(bytesPerFrame(PixelFormat::YUY2, 640, 480) == 614400);

  // RGB24 640x480: 3 bytes per pixel
  CHECK(bytesPerFrame(PixelFormat::RGB24, 640, 480) == 921600);

  // RGBA32 640x480: 4 bytes per pixel
  CHECK(bytesPerFrame(PixelFormat::RGBA32, 640, 480) == 1228800);

  // 1920x1080 I420
  CHECK(bytesPerFrame(PixelFormat::I420, 1920, 1080) == 3110400);
}

TEST_CASE("PixelFormat: pixelFormatToString", "[format][pixel]")
{
  CHECK(std::string(pixelFormatToString(PixelFormat::None)) == "None");
  CHECK(std::string(pixelFormatToString(PixelFormat::I420)) == "I420");
  CHECK(std::string(pixelFormatToString(PixelFormat::NV12)) == "NV12");
  CHECK(std::string(pixelFormatToString(PixelFormat::NV21)) == "NV21");
  CHECK(std::string(pixelFormatToString(PixelFormat::YUY2)) == "YUY2");
  CHECK(std::string(pixelFormatToString(PixelFormat::UYVY)) == "UYVY");
  CHECK(std::string(pixelFormatToString(PixelFormat::RGB24)) == "RGB24");
  CHECK(std::string(pixelFormatToString(PixelFormat::BGR24)) == "BGR24");
  CHECK(std::string(pixelFormatToString(PixelFormat::RGBA32)) == "RGBA32");
  CHECK(std::string(pixelFormatToString(PixelFormat::BGRA32)) == "BGRA32");
  CHECK(std::string(pixelFormatToString(PixelFormat::P010)) == "P010");
}
