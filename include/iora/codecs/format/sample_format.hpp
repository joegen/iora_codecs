#pragma once

/// @file sample_format.hpp
/// @brief Audio sample format descriptors and conversion utilities.

#include <cstddef>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>

namespace iora {
namespace codecs {

/// Audio sample formats (little-endian only).
enum class SampleFormat : std::uint8_t
{
  S16,    ///< Signed 16-bit integer
  S32,    ///< Signed 32-bit integer
  F32,    ///< 32-bit IEEE float
  U8,     ///< Unsigned 8-bit integer
  Mulaw,  ///< G.711 mu-law compressed
  Alaw    ///< G.711 A-law compressed
};

/// Returns the number of bytes occupied by one sample in the given format.
inline constexpr std::size_t bytesPerSample(SampleFormat fmt) noexcept
{
  switch (fmt)
  {
    case SampleFormat::S16:   return 2;
    case SampleFormat::S32:   return 4;
    case SampleFormat::F32:   return 4;
    case SampleFormat::U8:    return 1;
    case SampleFormat::Mulaw: return 1;
    case SampleFormat::Alaw:  return 1;
  }
  return 0;
}

/// True if the format stores floating-point samples.
inline constexpr bool isFloat(SampleFormat fmt) noexcept
{
  return fmt == SampleFormat::F32;
}

/// True if the format stores integer (linear PCM) samples.
inline constexpr bool isInteger(SampleFormat fmt) noexcept
{
  switch (fmt)
  {
    case SampleFormat::S16:
    case SampleFormat::S32:
    case SampleFormat::U8:
      return true;
    default:
      return false;
  }
}

/// True if the format stores signed samples.
inline constexpr bool isSigned(SampleFormat fmt) noexcept
{
  switch (fmt)
  {
    case SampleFormat::S16:
    case SampleFormat::S32:
    case SampleFormat::F32:
      return true;
    default:
      return false;
  }
}

/// Returns a human-readable name for the format.
inline const char* sampleFormatToString(SampleFormat fmt) noexcept
{
  switch (fmt)
  {
    case SampleFormat::S16:   return "S16";
    case SampleFormat::S32:   return "S32";
    case SampleFormat::F32:   return "F32";
    case SampleFormat::U8:    return "U8";
    case SampleFormat::Mulaw: return "Mulaw";
    case SampleFormat::Alaw:  return "Alaw";
  }
  return "Unknown";
}

// ============================================================================
// G.711 mu-law / A-law lookup tables (ITU-T G.711)
// Used by SampleFormat conversion and the G.711 codec module.
// ============================================================================

namespace detail {

/// mu-law decode table: 256 entries mapping compressed byte -> linear S16
inline constexpr std::int16_t kMulawToS16[256] = {
  -32124,-31100,-30076,-29052,-28028,-27004,-25980,-24956,
  -23932,-22908,-21884,-20860,-19836,-18812,-17788,-16764,
  -15996,-15484,-14972,-14460,-13948,-13436,-12924,-12412,
  -11900,-11388,-10876,-10364, -9852, -9340, -8828, -8316,
   -7932, -7676, -7420, -7164, -6908, -6652, -6396, -6140,
   -5884, -5628, -5372, -5116, -4860, -4604, -4348, -4092,
   -3900, -3772, -3644, -3516, -3388, -3260, -3132, -3004,
   -2876, -2748, -2620, -2492, -2364, -2236, -2108, -1980,
   -1884, -1820, -1756, -1692, -1628, -1564, -1500, -1436,
   -1372, -1308, -1244, -1180, -1116, -1052,  -988,  -924,
    -876,  -844,  -812,  -780,  -748,  -716,  -684,  -652,
    -620,  -588,  -556,  -524,  -492,  -460,  -428,  -396,
    -372,  -356,  -340,  -324,  -308,  -292,  -276,  -260,
    -244,  -228,  -212,  -196,  -180,  -164,  -148,  -132,
    -120,  -112,  -104,   -96,   -88,   -80,   -72,   -64,
     -56,   -48,   -40,   -32,   -24,   -16,    -8,     0,
   32124, 31100, 30076, 29052, 28028, 27004, 25980, 24956,
   23932, 22908, 21884, 20860, 19836, 18812, 17788, 16764,
   15996, 15484, 14972, 14460, 13948, 13436, 12924, 12412,
   11900, 11388, 10876, 10364,  9852,  9340,  8828,  8316,
    7932,  7676,  7420,  7164,  6908,  6652,  6396,  6140,
    5884,  5628,  5372,  5116,  4860,  4604,  4348,  4092,
    3900,  3772,  3644,  3516,  3388,  3260,  3132,  3004,
    2876,  2748,  2620,  2492,  2364,  2236,  2108,  1980,
    1884,  1820,  1756,  1692,  1628,  1564,  1500,  1436,
    1372,  1308,  1244,  1180,  1116,  1052,   988,   924,
     876,   844,   812,   780,   748,   716,   684,   652,
     620,   588,   556,   524,   492,   460,   428,   396,
     372,   356,   340,   324,   308,   292,   276,   260,
     244,   228,   212,   196,   180,   164,   148,   132,
     120,   112,   104,    96,    88,    80,    72,    64,
      56,    48,    40,    32,    24,    16,     8,     0
};

/// A-law decode table: 256 entries mapping compressed byte -> linear S16
inline constexpr std::int16_t kAlawToS16[256] = {
   -5504, -5248, -6016, -5760, -4480, -4224, -4992, -4736,
   -7552, -7296, -8064, -7808, -6528, -6272, -7040, -6784,
   -2752, -2624, -3008, -2880, -2240, -2112, -2496, -2368,
   -3776, -3648, -4032, -3904, -3264, -3136, -3520, -3392,
  -22016,-20992,-24064,-23040,-17920,-16896,-19968,-18944,
  -30208,-29184,-32256,-31232,-26112,-25088,-28160,-27136,
  -11008,-10496,-12032,-11520, -8960, -8448, -9984, -9472,
  -15104,-14592,-16128,-15616,-13056,-12544,-14080,-13568,
    -344,  -328,  -376,  -360,  -280,  -264,  -312,  -296,
    -472,  -456,  -504,  -488,  -408,  -392,  -440,  -424,
     -88,   -72,  -120,  -104,   -24,    -8,   -56,   -40,
    -216,  -200,  -248,  -232,  -152,  -136,  -184,  -168,
   -1376, -1312, -1504, -1440, -1120, -1056, -1248, -1184,
   -1888, -1824, -2016, -1952, -1632, -1568, -1760, -1696,
    -688,  -656,  -752,  -720,  -560,  -528,  -624,  -592,
    -944,  -912, -1008,  -976,  -816,  -784,  -880,  -848,
    5504,  5248,  6016,  5760,  4480,  4224,  4992,  4736,
    7552,  7296,  8064,  7808,  6528,  6272,  7040,  6784,
    2752,  2624,  3008,  2880,  2240,  2112,  2496,  2368,
    3776,  3648,  4032,  3904,  3264,  3136,  3520,  3392,
   22016, 20992, 24064, 23040, 17920, 16896, 19968, 18944,
   30208, 29184, 32256, 31232, 26112, 25088, 28160, 27136,
   11008, 10496, 12032, 11520,  8960,  8448,  9984,  9472,
   15104, 14592, 16128, 15616, 13056, 12544, 14080, 13568,
     344,   328,   376,   360,   280,   264,   312,   296,
     472,   456,   504,   488,   408,   392,   440,   424,
      88,    72,   120,   104,    24,     8,    56,    40,
     216,   200,   248,   232,   152,   136,   184,   168,
    1376,  1312,  1504,  1440,  1120,  1056,  1248,  1184,
    1888,  1824,  2016,  1952,  1632,  1568,  1760,  1696,
     688,   656,   752,   720,   560,   528,   624,   592,
     944,   912,  1008,   976,   816,   784,   880,   848
};

/// Encode a linear S16 sample to mu-law byte (ITU-T G.711).
inline std::uint8_t s16ToMulaw(std::int16_t sample) noexcept
{
  constexpr std::int16_t kClip = 32635;
  constexpr std::int16_t kBias = 0x84;
  constexpr std::uint8_t kExpTable[256] = {
    0,0,1,1,2,2,2,2,3,3,3,3,3,3,3,3,
    4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
    5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,
    5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,
    6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,
    6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,
    6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,
    6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,
    7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
    7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
    7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
    7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
    7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
    7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
    7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
    7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7
  };

  std::uint8_t sign = 0;
  int magnitude = static_cast<int>(sample);
  if (magnitude < 0)
  {
    sign = 0x80;
    magnitude = -magnitude;
  }
  if (magnitude > kClip)
  {
    magnitude = kClip;
  }
  magnitude += kBias;

  auto exponent = kExpTable[static_cast<std::uint8_t>(magnitude >> 7)];
  auto mantissa = static_cast<std::uint8_t>((magnitude >> (exponent + 3)) & 0x0F);

  return static_cast<std::uint8_t>(~(sign | (exponent << 4) | mantissa));
}

/// Encode a linear S16 sample to A-law byte (ITU-T G.711).
inline std::uint8_t s16ToAlaw(std::int16_t sample) noexcept
{
  // ITU-T G.711 A-law: operates on 13-bit magnitude.
  // Use division instead of right-shift to avoid implementation-defined
  // behavior for negative values in C++17.
  std::uint8_t mask;
  int pcmVal = static_cast<int>(sample) / 8; // convert 16-bit to 13-bit

  if (pcmVal >= 0)
  {
    mask = 0xD5; // sign bit = 1, invert even bits
  }
  else
  {
    mask = 0x55; // sign bit = 0, invert even bits
    pcmVal = -pcmVal - 1;
  }

  // Segment endpoints for 13-bit magnitude
  constexpr int kSegEnd[8] = {
    0x1F, 0x3F, 0x7F, 0xFF, 0x1FF, 0x3FF, 0x7FF, 0xFFF
  };

  // Find segment
  int seg = 0;
  for (; seg < 8; ++seg)
  {
    if (pcmVal <= kSegEnd[seg])
    {
      break;
    }
  }

  if (seg >= 8)
  {
    return static_cast<std::uint8_t>(0x7F ^ mask);
  }

  std::uint8_t aval;
  if (seg < 2)
  {
    aval = static_cast<std::uint8_t>((seg << 4) | ((pcmVal >> 1) & 0x0F));
  }
  else
  {
    aval = static_cast<std::uint8_t>((seg << 4) | ((pcmVal >> seg) & 0x0F));
  }

  return static_cast<std::uint8_t>(aval ^ mask);
}

} // namespace detail

// ============================================================================
// Sample format conversion
// ============================================================================

/// Convert audio samples between formats.
///
/// Supported conversions: S16<->F32, S16<->S32, U8<->S16, Mulaw<->S16, Alaw<->S16.
/// @throws std::invalid_argument for unsupported conversion pairs.
inline void convertSamples(
  const void* src,
  SampleFormat srcFmt,
  void* dst,
  SampleFormat dstFmt,
  std::size_t sampleCount)
{
  if (srcFmt == dstFmt)
  {
    std::memcpy(dst, src, sampleCount * bytesPerSample(srcFmt));
    return;
  }

  const auto* srcBytes = static_cast<const std::uint8_t*>(src);
  auto* dstBytes = static_cast<std::uint8_t*>(dst);

  // S16 -> F32
  if (srcFmt == SampleFormat::S16 && dstFmt == SampleFormat::F32)
  {
    const auto* in = reinterpret_cast<const std::int16_t*>(srcBytes);
    auto* out = reinterpret_cast<float*>(dstBytes);
    for (std::size_t i = 0; i < sampleCount; ++i)
    {
      out[i] = static_cast<float>(in[i]) / 32768.0f;
    }
    return;
  }

  // F32 -> S16
  if (srcFmt == SampleFormat::F32 && dstFmt == SampleFormat::S16)
  {
    const auto* in = reinterpret_cast<const float*>(srcBytes);
    auto* out = reinterpret_cast<std::int16_t*>(dstBytes);
    for (std::size_t i = 0; i < sampleCount; ++i)
    {
      float clamped = in[i];
      if (clamped > 1.0f)
      {
        clamped = 1.0f;
      }
      if (clamped < -1.0f)
      {
        clamped = -1.0f;
      }
      out[i] = static_cast<std::int16_t>(clamped * 32767.0f);
    }
    return;
  }

  // S16 -> S32
  if (srcFmt == SampleFormat::S16 && dstFmt == SampleFormat::S32)
  {
    const auto* in = reinterpret_cast<const std::int16_t*>(srcBytes);
    auto* out = reinterpret_cast<std::int32_t*>(dstBytes);
    for (std::size_t i = 0; i < sampleCount; ++i)
    {
      out[i] = static_cast<std::int32_t>(in[i]) << 16;
    }
    return;
  }

  // S32 -> S16
  if (srcFmt == SampleFormat::S32 && dstFmt == SampleFormat::S16)
  {
    const auto* in = reinterpret_cast<const std::int32_t*>(srcBytes);
    auto* out = reinterpret_cast<std::int16_t*>(dstBytes);
    for (std::size_t i = 0; i < sampleCount; ++i)
    {
      out[i] = static_cast<std::int16_t>(in[i] >> 16);
    }
    return;
  }

  // U8 -> S16
  if (srcFmt == SampleFormat::U8 && dstFmt == SampleFormat::S16)
  {
    const auto* in = srcBytes;
    auto* out = reinterpret_cast<std::int16_t*>(dstBytes);
    for (std::size_t i = 0; i < sampleCount; ++i)
    {
      out[i] = static_cast<std::int16_t>((static_cast<std::int16_t>(in[i]) - 128) << 8);
    }
    return;
  }

  // S16 -> U8
  if (srcFmt == SampleFormat::S16 && dstFmt == SampleFormat::U8)
  {
    const auto* in = reinterpret_cast<const std::int16_t*>(srcBytes);
    auto* out = dstBytes;
    for (std::size_t i = 0; i < sampleCount; ++i)
    {
      out[i] = static_cast<std::uint8_t>((in[i] >> 8) + 128);
    }
    return;
  }

  // Mulaw -> S16
  if (srcFmt == SampleFormat::Mulaw && dstFmt == SampleFormat::S16)
  {
    const auto* in = srcBytes;
    auto* out = reinterpret_cast<std::int16_t*>(dstBytes);
    for (std::size_t i = 0; i < sampleCount; ++i)
    {
      out[i] = detail::kMulawToS16[in[i]];
    }
    return;
  }

  // S16 -> Mulaw
  if (srcFmt == SampleFormat::S16 && dstFmt == SampleFormat::Mulaw)
  {
    const auto* in = reinterpret_cast<const std::int16_t*>(srcBytes);
    auto* out = dstBytes;
    for (std::size_t i = 0; i < sampleCount; ++i)
    {
      out[i] = detail::s16ToMulaw(in[i]);
    }
    return;
  }

  // Alaw -> S16
  if (srcFmt == SampleFormat::Alaw && dstFmt == SampleFormat::S16)
  {
    const auto* in = srcBytes;
    auto* out = reinterpret_cast<std::int16_t*>(dstBytes);
    for (std::size_t i = 0; i < sampleCount; ++i)
    {
      out[i] = detail::kAlawToS16[in[i]];
    }
    return;
  }

  // S16 -> Alaw
  if (srcFmt == SampleFormat::S16 && dstFmt == SampleFormat::Alaw)
  {
    const auto* in = reinterpret_cast<const std::int16_t*>(srcBytes);
    auto* out = dstBytes;
    for (std::size_t i = 0; i < sampleCount; ++i)
    {
      out[i] = detail::s16ToAlaw(in[i]);
    }
    return;
  }

  throw std::invalid_argument(
    std::string("Unsupported sample format conversion: ") +
    sampleFormatToString(srcFmt) + " -> " + sampleFormatToString(dstFmt));
}

} // namespace codecs
} // namespace iora
