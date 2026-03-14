#pragma once

/// @file g722_codec_factory.hpp
/// @brief Factory for creating G.722 codec encoder/decoder instances.

#include "g722_codec.hpp"
#include "iora/codecs/codec/i_codec_factory.hpp"

#include <mutex>

namespace iora {
namespace codecs {

/// ICodecFactory implementation for G.722.
///
/// Creates separate G722Codec instances for encoding and decoding.
/// G.722 is stateful — encoder and decoder maintain sub-band ADPCM state.
/// Factory supports configurable default bitrate mode.
class G722CodecFactory : public ICodecFactory
{
public:
  explicit G722CodecFactory(CodecInfo info);

  const CodecInfo& codecInfo() const override;
  bool supports(const CodecInfo& info) const override;
  std::unique_ptr<ICodec> createEncoder(const CodecInfo& params) override;
  std::unique_ptr<ICodec> createDecoder(const CodecInfo& params) override;

  /// Configure default bitrate mode for subsequently created codecs.
  /// @param mode Bitrate mode: 1=48kbps, 2=56kbps, 3=64kbps (default 64000).
  void setDefaultMode(std::uint32_t mode);

  /// Pre-filled CodecInfo for G.722 (PT=9, clockRate=8000 per RFC 3551).
  static CodecInfo makeG722Info();

private:
  CodecInfo _info;
  mutable std::mutex _mutex;
  int _defaultRate = 64000;
};

} // namespace codecs
} // namespace iora
