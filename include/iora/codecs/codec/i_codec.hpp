#pragma once

/// @file i_codec.hpp
/// @brief Abstract codec interface — the central contract all codecs implement.

#include "iora/codecs/codec/codec_info.hpp"
#include "iora/codecs/core/media_buffer.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace iora {
namespace codecs {

/// Abstract interface for audio and video codecs.
///
/// Every codec plugin module implements ICodec for its encoder and
/// decoder. Instances are created per-session via ICodecFactory and
/// used on the hot path through C++ virtual dispatch (~1-2ns vtable
/// lookup), NOT through Iora's exported API system.
///
/// ICodec does NOT inherit ILifecycleManaged — codec instances are
/// lightweight encode/decode engines. Construction = ready to use;
/// destruction = cleanup.
class ICodec
{
public:
  virtual ~ICodec() = default;

  /// Returns the codec identity and capability descriptor.
  virtual const CodecInfo& info() const = 0;

  /// Encode raw samples/pixels into a compressed frame.
  virtual std::shared_ptr<MediaBuffer> encode(const MediaBuffer& input) = 0;

  /// Decode a compressed frame into raw samples/pixels.
  virtual std::shared_ptr<MediaBuffer> decode(const MediaBuffer& input) = 0;

  /// Packet loss concealment — generate a synthetic frame to cover
  /// a missing packet. @p frameSamples is the expected frame size
  /// in samples (audio) or 0 (video).
  /// Decoder-side operation by contract. Stateful codecs return nullptr
  /// from encoder instances. Stateless codecs (e.g., G.711) may return
  /// a valid PLC buffer from any instance.
  virtual std::shared_ptr<MediaBuffer> plc(std::size_t frameSamples) = 0;

  /// Set a runtime parameter (e.g., "bitrate", "complexity").
  /// Returns true if the parameter was recognized and applied.
  virtual bool setParameter(const std::string& key, std::uint32_t value) = 0;

  /// Query a current runtime parameter value.
  /// Returns 0 for unrecognized keys.
  virtual std::uint32_t getParameter(const std::string& key) const = 0;
};

} // namespace codecs
} // namespace iora
