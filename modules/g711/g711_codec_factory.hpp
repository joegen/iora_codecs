#pragma once

/// @file g711_codec_factory.hpp
/// @brief Factory for creating G.711 PCMU and PCMA codec instances.

#include "g711_codec.hpp"
#include "iora/codecs/codec/i_codec_factory.hpp"

#include <chrono>

namespace iora {
namespace codecs {

/// ICodecFactory implementation for G.711 PCMU or PCMA.
///
/// Each factory instance is configured for one variant (PCMU or PCMA).
/// Two factory instances are created by the plugin — one per variant.
class G711CodecFactory : public ICodecFactory
{
public:
  explicit G711CodecFactory(CodecInfo info)
    : _info(std::move(info))
  {
  }

  const CodecInfo& codecInfo() const override
  {
    return _info;
  }

  bool supports(const CodecInfo& info) const override
  {
    return _info.name == info.name &&
           _info.clockRate == info.clockRate &&
           _info.channels == info.channels;
  }

  std::unique_ptr<ICodec> createEncoder(const CodecInfo& params) override
  {
    if (!supports(params))
    {
      return nullptr;
    }
    return std::make_unique<G711Codec>(_info);
  }

  std::unique_ptr<ICodec> createDecoder(const CodecInfo& params) override
  {
    if (!supports(params))
    {
      return nullptr;
    }
    return std::make_unique<G711Codec>(_info);
  }

  /// Pre-filled CodecInfo for PCMU (mu-law, PT=0).
  static CodecInfo makePcmuInfo()
  {
    CodecInfo info;
    info.name = "PCMU";
    info.type = CodecType::Audio;
    info.mediaSubtype = "PCMU";
    info.clockRate = 8000;
    info.channels = 1;
    info.defaultPayloadType = 0;
    info.defaultBitrate = 64000;
    info.frameSize = std::chrono::microseconds{20000};
    info.features = CodecFeatures::Cbr;
    return info;
  }

  /// Pre-filled CodecInfo for PCMA (A-law, PT=8).
  static CodecInfo makePcmaInfo()
  {
    CodecInfo info;
    info.name = "PCMA";
    info.type = CodecType::Audio;
    info.mediaSubtype = "PCMA";
    info.clockRate = 8000;
    info.channels = 1;
    info.defaultPayloadType = 8;
    info.defaultBitrate = 64000;
    info.frameSize = std::chrono::microseconds{20000};
    info.features = CodecFeatures::Cbr;
    return info;
  }

private:
  CodecInfo _info;
};

} // namespace codecs
} // namespace iora
