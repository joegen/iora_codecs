#pragma once

/// @file g729_codec_factory.hpp
/// @brief Factory for creating G.729 codec encoder/decoder instances.

#include "g729_codec.hpp"
#include "iora/codecs/codec/i_codec_factory.hpp"

namespace iora {
namespace codecs {

/// ICodecFactory implementation for G.729 Annex A.
///
/// Creates separate G729Codec instances for encoding and decoding.
/// G.729 is fixed 8 kbps with no configurable parameters.
class G729CodecFactory : public ICodecFactory
{
public:
  explicit G729CodecFactory(CodecInfo info);

  const CodecInfo& codecInfo() const override;
  bool supports(const CodecInfo& info) const override;
  std::unique_ptr<ICodec> createEncoder(const CodecInfo& params) override;
  std::unique_ptr<ICodec> createDecoder(const CodecInfo& params) override;

  /// Pre-filled CodecInfo for G.729 (PT=18, 8kHz, mono).
  static CodecInfo makeG729Info();

private:
  CodecInfo _info;
};

} // namespace codecs
} // namespace iora
