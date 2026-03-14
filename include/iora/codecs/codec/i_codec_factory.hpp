#pragma once

/// @file i_codec_factory.hpp
/// @brief Abstract factory for creating codec encoder/decoder instances.

#include "iora/codecs/codec/codec_info.hpp"
#include "iora/codecs/codec/i_codec.hpp"

#include <memory>

namespace iora {
namespace codecs {

/// Abstract factory for creating ICodec encoder and decoder instances.
///
/// Each codec plugin module provides one ICodecFactory implementation.
/// The factory is registered with CodecRegistry during plugin load
/// and is shared across all sessions. Individual ICodec instances
/// are created per-session.
class ICodecFactory
{
public:
  virtual ~ICodecFactory() = default;

  /// Returns the CodecInfo for the codec this factory produces.
  virtual const CodecInfo& codecInfo() const = 0;

  /// Check if this factory can produce a codec matching @p info
  /// (compares name, clockRate, channels).
  virtual bool supports(const CodecInfo& info) const = 0;

  /// Create an encoder instance configured per @p params.
  virtual std::unique_ptr<ICodec> createEncoder(const CodecInfo& params) = 0;

  /// Create a decoder instance configured per @p params.
  virtual std::unique_ptr<ICodec> createDecoder(const CodecInfo& params) = 0;
};

} // namespace codecs
} // namespace iora
