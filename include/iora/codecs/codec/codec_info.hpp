#pragma once

/// @file codec_info.hpp
/// @brief Codec type, feature flags, and codec identity/capability descriptor.

#include <chrono>
#include <cstdint>
#include <string>

namespace iora {
namespace codecs {

/// Distinguishes audio from video codecs.
enum class CodecType : std::uint8_t
{
  Audio,
  Video
};

/// Returns a human-readable name for the codec type.
inline const char* codecTypeToString(CodecType type) noexcept
{
  switch (type)
  {
    case CodecType::Audio: return "Audio";
    case CodecType::Video: return "Video";
  }
  return "Unknown";
}

/// Bitfield describing optional codec capabilities.
enum class CodecFeatures : std::uint16_t
{
  None = 0,
  Fec  = 1 << 0, ///< Forward error correction
  Dtx  = 1 << 1, ///< Discontinuous transmission
  Vad  = 1 << 2, ///< Voice activity detection
  Plc  = 1 << 3, ///< Packet loss concealment
  Vbr  = 1 << 4, ///< Variable bitrate
  Cbr  = 1 << 5, ///< Constant bitrate
  Svc  = 1 << 6  ///< Scalable video coding
};

/// Combine two feature sets.
inline constexpr CodecFeatures operator|(CodecFeatures lhs, CodecFeatures rhs) noexcept
{
  return static_cast<CodecFeatures>(
    static_cast<std::uint16_t>(lhs) | static_cast<std::uint16_t>(rhs));
}

/// Intersect two feature sets.
inline constexpr CodecFeatures operator&(CodecFeatures lhs, CodecFeatures rhs) noexcept
{
  return static_cast<CodecFeatures>(
    static_cast<std::uint16_t>(lhs) & static_cast<std::uint16_t>(rhs));
}

/// Compound-OR assignment.
inline constexpr CodecFeatures& operator|=(CodecFeatures& lhs, CodecFeatures rhs) noexcept
{
  lhs = lhs | rhs;
  return lhs;
}

/// Test whether a specific feature flag is present in a feature set.
inline constexpr bool hasFeature(CodecFeatures set, CodecFeatures flag) noexcept
{
  return (set & flag) == flag;
}

/// Codec identity and capability descriptor.
///
/// Describes what a codec can do: its SDP encoding name, clock rate,
/// channel count, default payload type, bitrate, frame size, and
/// supported features. Used by ICodecFactory, CodecRegistry, and
/// SDP negotiation.
struct CodecInfo
{
  std::string name;                       ///< Codec name (e.g., "opus", "PCMU", "H264")
  CodecType type = CodecType::Audio;      ///< Audio or video
  std::string mediaSubtype;               ///< SDP encoding name (e.g., "opus", "PCMU", "H264")
  std::uint32_t clockRate = 0;            ///< RTP clock rate in Hz
  std::uint8_t channels = 1;              ///< Number of channels (audio only; video = 0)
  /// Static RTP payload type per RFC 3551, or 0 for dynamic codecs
  /// that must be negotiated via SDP (PTs 96-127). PCMU owns static PT 0.
  /// Opus uses PT=111 by convention (widely adopted in WebRTC).
  /// The registry's findByPayloadType is for static PTs only;
  /// dynamic codecs are found by name.
  std::uint8_t defaultPayloadType = 0;
  std::uint32_t defaultBitrate = 0;       ///< Default bitrate in bits/sec
  std::chrono::microseconds frameSize{0}; ///< Typical frame duration
  CodecFeatures features = CodecFeatures::None;

  /// Identity comparison: two CodecInfo are equal if name, clockRate,
  /// and channels all match.
  bool operator==(const CodecInfo& other) const noexcept
  {
    return name == other.name &&
           clockRate == other.clockRate &&
           channels == other.channels;
  }

  bool operator!=(const CodecInfo& other) const noexcept
  {
    return !(*this == other);
  }

  /// Check if another CodecInfo is compatible for codec selection
  /// (name, clockRate, and channels must match).
  bool matches(const CodecInfo& other) const noexcept
  {
    return name == other.name &&
           clockRate == other.clockRate &&
           channels == other.channels;
  }
};

} // namespace codecs
} // namespace iora
