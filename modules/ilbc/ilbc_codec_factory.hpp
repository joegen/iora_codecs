#pragma once

/// @file ilbc_codec_factory.hpp
/// @brief Factory for creating iLBC codec encoder/decoder instances.

#include "ilbc_codec.hpp"
#include "iora/codecs/codec/i_codec_factory.hpp"

#include <mutex>

namespace iora {
namespace codecs {

/// ICodecFactory implementation for iLBC.
///
/// Creates separate IlbcCodec instances for encoding and decoding.
/// iLBC is stateful — encoder and decoder maintain prediction state.
/// Factory supports configurable default frame mode (20 or 30 ms).
class IlbcCodecFactory : public ICodecFactory
{
public:
  explicit IlbcCodecFactory(CodecInfo info);

  const CodecInfo& codecInfo() const override;
  bool supports(const CodecInfo& info) const override;
  std::unique_ptr<ICodec> createEncoder(const CodecInfo& params) override;
  std::unique_ptr<ICodec> createDecoder(const CodecInfo& params) override;

  /// Configure default frame duration for subsequently created codecs.
  /// @param frameLenMs Frame duration: 20 or 30 ms.
  void setDefaultFrameMode(std::uint32_t frameLenMs);

  /// Pre-filled CodecInfo for iLBC (dynamic PT, 8kHz, mono).
  static CodecInfo makeIlbcInfo();

private:
  CodecInfo _info;
  mutable std::mutex _mutex;
  int _defaultFrameLenMs = 30;
};

} // namespace codecs
} // namespace iora
