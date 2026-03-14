#pragma once

/// @file amr_wb_codec_factory.hpp
/// @brief Factory for creating AMR-WB codec encoder/decoder instances.

#include "amr_wb_codec.hpp"
#include "iora/codecs/codec/i_codec_factory.hpp"

#include <mutex>

namespace iora {
namespace codecs {

/// ICodecFactory implementation for AMR-WB.
///
/// Factory supports configurable default bitrate mode and DTX setting
/// for subsequently created codec instances.
class AmrWbCodecFactory : public ICodecFactory
{
public:
  explicit AmrWbCodecFactory(CodecInfo info);

  const CodecInfo& codecInfo() const override;
  bool supports(const CodecInfo& info) const override;
  std::unique_ptr<ICodec> createEncoder(const CodecInfo& params) override;
  std::unique_ptr<ICodec> createDecoder(const CodecInfo& params) override;

  /// Configure default bitrate mode for subsequently created encoders.
  void setDefaultMode(std::uint32_t bitrateMode);

  /// Configure default DTX setting for subsequently created encoders.
  void setDefaultDtx(bool enable);

  /// Pre-filled CodecInfo for AMR-WB (dynamic PT, 16kHz, mono).
  static CodecInfo makeAmrWbInfo();

private:
  CodecInfo _info;
  mutable std::mutex _mutex;
  AmrWbBitrateMode _defaultBitrateMode = AmrWbBitrateMode::MD2385;
  bool _defaultDtx = false;
};

} // namespace codecs
} // namespace iora
