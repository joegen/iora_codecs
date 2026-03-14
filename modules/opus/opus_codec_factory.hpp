#pragma once

/// @file opus_codec_factory.hpp
/// @brief Factory for creating Opus codec encoder/decoder instances.

#include "opus_codec.hpp"
#include "iora/codecs/codec/i_codec_factory.hpp"

#include <mutex>

namespace iora {
namespace codecs {

/// ICodecFactory implementation for Opus.
///
/// Creates separate OpusCodec instances for encoding and decoding.
/// Opus is stateful — encoder and decoder maintain distinct internal state.
/// Factory supports configurable defaults for subsequently created encoders.
class OpusCodecFactory : public ICodecFactory
{
public:
  explicit OpusCodecFactory(CodecInfo info);

  const CodecInfo& codecInfo() const override;
  bool supports(const CodecInfo& info) const override;
  std::unique_ptr<ICodec> createEncoder(const CodecInfo& params) override;
  std::unique_ptr<ICodec> createDecoder(const CodecInfo& params) override;

  /// Configure default bitrate for subsequently created encoders.
  void setDefaultBitrate(std::uint32_t bps);

  /// Configure default complexity for subsequently created encoders.
  void setDefaultComplexity(std::uint32_t complexity);

  /// Configure default FEC setting for subsequently created encoders.
  void setDefaultFec(bool enable);

  /// Configure default DTX setting for subsequently created encoders.
  void setDefaultDtx(bool enable);

  /// Pre-filled CodecInfo for Opus (PT=111, 48kHz, stereo per RFC 7587).
  static CodecInfo makeOpusInfo();

private:
  CodecInfo _info;
  mutable std::mutex _mutex;
  std::uint32_t _defaultBitrate = 0;
  std::uint32_t _defaultComplexity = 5;
  bool _defaultFec = false;
  bool _defaultDtx = false;
  bool _hasBitrate = false;
};

} // namespace codecs
} // namespace iora
