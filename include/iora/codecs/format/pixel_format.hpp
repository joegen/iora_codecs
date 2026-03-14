#pragma once

/// @file pixel_format.hpp
/// @brief Video pixel format descriptors and frame size utilities.

#include <cstddef>
#include <cstdint>

namespace iora {
namespace codecs {

/// Video pixel formats.
enum class PixelFormat : std::uint8_t
{
  None,   ///< Unset / not applicable (e.g., audio buffers)
  I420,   ///< Planar YUV 4:2:0 (Y + U + V planes)
  NV12,   ///< Semi-planar YUV 4:2:0 (Y plane + interleaved UV)
  NV21,   ///< Semi-planar YUV 4:2:0 (Y plane + interleaved VU)
  YUY2,   ///< Packed YUV 4:2:2 (YUYV byte order)
  UYVY,   ///< Packed YUV 4:2:2 (UYVY byte order)
  RGB24,  ///< Packed RGB, 3 bytes per pixel
  BGR24,  ///< Packed BGR, 3 bytes per pixel
  RGBA32, ///< Packed RGBA, 4 bytes per pixel
  BGRA32, ///< Packed BGRA, 4 bytes per pixel
  P010    ///< Planar YUV 4:2:0 10-bit (16-bit storage per component)
};

/// Chroma subsampling factors.
struct ChromaSubsampling
{
  std::uint8_t horizontal; ///< Horizontal subsampling (1 = none, 2 = halved)
  std::uint8_t vertical;   ///< Vertical subsampling (1 = none, 2 = halved)
};

/// True if the format stores pixel data in separate Y/U/V (or Y/UV) planes.
inline constexpr bool isPlanar(PixelFormat fmt) noexcept
{
  switch (fmt)
  {
    case PixelFormat::I420:
    case PixelFormat::NV12:
    case PixelFormat::NV21:
    case PixelFormat::P010:
      return true;
    default:
      return false;
  }
}

/// Returns a human-readable name for the format.
inline const char* pixelFormatToString(PixelFormat fmt) noexcept
{
  switch (fmt)
  {
    case PixelFormat::None:   return "None";
    case PixelFormat::I420:   return "I420";
    case PixelFormat::NV12:   return "NV12";
    case PixelFormat::NV21:   return "NV21";
    case PixelFormat::YUY2:   return "YUY2";
    case PixelFormat::UYVY:   return "UYVY";
    case PixelFormat::RGB24:  return "RGB24";
    case PixelFormat::BGR24:  return "BGR24";
    case PixelFormat::RGBA32: return "RGBA32";
    case PixelFormat::BGRA32: return "BGRA32";
    case PixelFormat::P010:   return "P010";
  }
  return "Unknown";
}

/// Returns the chroma subsampling factors for the given format.
inline constexpr ChromaSubsampling chromaSubsampling(PixelFormat fmt) noexcept
{
  switch (fmt)
  {
    case PixelFormat::I420:
    case PixelFormat::NV12:
    case PixelFormat::NV21:
    case PixelFormat::P010:
      return {2, 2}; // 4:2:0
    case PixelFormat::YUY2:
    case PixelFormat::UYVY:
      return {2, 1}; // 4:2:2
    case PixelFormat::None:
    case PixelFormat::RGB24:
    case PixelFormat::BGR24:
    case PixelFormat::RGBA32:
    case PixelFormat::BGRA32:
      return {1, 1}; // 4:4:4 / N/A
  }
  return {1, 1};
}

/// Returns bytes per pixel for packed formats, 0 for planar formats.
/// For planar formats, use bytesPerFrame() instead.
inline constexpr std::size_t bytesPerPixel(PixelFormat fmt) noexcept
{
  switch (fmt)
  {
    case PixelFormat::YUY2:  return 2; // averaged: 4 bytes per 2 pixels
    case PixelFormat::UYVY:  return 2;
    case PixelFormat::RGB24: return 3;
    case PixelFormat::BGR24: return 3;
    case PixelFormat::RGBA32: return 4;
    case PixelFormat::BGRA32: return 4;
    default:
      return 0; // planar — use bytesPerFrame()
  }
}

/// Returns the total number of bytes required to store one frame.
inline constexpr std::size_t bytesPerFrame(
  PixelFormat fmt,
  std::size_t width,
  std::size_t height) noexcept
{
  switch (fmt)
  {
    case PixelFormat::I420:
      // Y: w*h, U: w/2*h/2, V: w/2*h/2 = w*h * 3/2
      return width * height * 3 / 2;
    case PixelFormat::NV12:
    case PixelFormat::NV21:
      // Y: w*h, UV interleaved: w*h/2 = w*h * 3/2
      return width * height * 3 / 2;
    case PixelFormat::P010:
      // Same as I420 but 16-bit per component
      return width * height * 3; // 2 bytes per sample * 3/2 ratio
    case PixelFormat::YUY2:
    case PixelFormat::UYVY:
      return width * height * 2;
    case PixelFormat::RGB24:
    case PixelFormat::BGR24:
      return width * height * 3;
    case PixelFormat::RGBA32:
    case PixelFormat::BGRA32:
      return width * height * 4;
    case PixelFormat::None:
      return 0;
  }
  return 0;
}

} // namespace codecs
} // namespace iora
