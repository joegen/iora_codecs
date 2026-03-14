#pragma once

/// @file amr_nb_codec_factory.hpp
/// @brief Factory for creating AMR-NB codec encoder/decoder instances.

#include "amr_nb_codec.hpp"
#include "iora/codecs/codec/i_codec_factory.hpp"

#include <mutex>

namespace iora {
namespace codecs {

/// ICodecFactory implementation for AMR-NB.
///
/// Factory supports configurable default bitrate mode and DTX setting
/// for subsequently created codec instances.
class AmrNbCodecFactory : public ICodecFactory
{
public:
  explicit AmrNbCodecFactory(CodecInfo info);

  const CodecInfo& codecInfo() const override;
  bool supports(const CodecInfo& info) const override;
  std::unique_ptr<ICodec> createEncoder(const CodecInfo& params) override;
  std::unique_ptr<ICodec> createDecoder(const CodecInfo& params) override;

  /// Configure default bitrate mode for subsequently created encoders.
  void setDefaultMode(std::uint32_t bitrateMode);

  /// Configure default DTX setting for subsequently created encoders.
  void setDefaultDtx(bool enable);

  /// Pre-filled CodecInfo for AMR-NB (dynamic PT, 8kHz, mono).
  static CodecInfo makeAmrNbInfo();

private:
  CodecInfo _info;
  mutable std::mutex _mutex;
  AmrNbBitrateMode _defaultBitrateMode = AmrNbBitrateMode::MR122;
  bool _defaultDtx = false;
};

} // namespace codecs
} // namespace iora
