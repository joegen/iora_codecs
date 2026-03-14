#pragma once

/// @file vpx_codec.hpp
/// @brief VP8/VP9 codec implementation wrapping libvpx encoder/decoder.

#include "iora/codecs/codec/i_codec.hpp"

#include <cstdint>
#include <memory>
#include <string>

#include <vpx/vpx_codec.h>
#include <vpx/vpx_encoder.h>
#include <vpx/vpx_decoder.h>
#include <vpx/vpx_image.h>

namespace iora {
namespace codecs {

/// Operation mode for VpxCodec — encoder or decoder.
enum class VpxMode : std::uint8_t
{
  Encoder,
  Decoder
};

/// Codec variant — VP8 or VP9.
enum class VpxVariant : std::uint8_t
{
  VP8,
  VP9
};

/// ICodec implementation wrapping libvpx for VP8/VP9.
///
/// VP8 and VP9 are stateful video codecs. The encoder accepts I420 input
/// and produces a VPx bitstream. The decoder accepts a VPx bitstream and
/// outputs I420 frames. Separate instances must be created for encoding
/// and decoding. The variant (VP8/VP9) is selected at construction.
class VpxCodec : public ICodec
{
public:
  VpxCodec(CodecInfo info, VpxMode mode, VpxVariant variant,
           std::uint32_t width = 640, std::uint32_t height = 480,
           std::uint32_t bitrate = 500000, float framerate = 30.0f,
           std::uint32_t speed = 6);
  ~VpxCodec() override;

  // Non-copyable, non-movable (opaque codec context).
  VpxCodec(const VpxCodec&) = delete;
  VpxCodec& operator=(const VpxCodec&) = delete;
  VpxCodec(VpxCodec&&) = delete;
  VpxCodec& operator=(VpxCodec&&) = delete;

  const CodecInfo& info() const override;

  /// Encode I420 frame -> VPx bitstream.
  std::shared_ptr<MediaBuffer> encode(const MediaBuffer& input) override;

  /// Decode VPx bitstream -> I420 frame.
  std::shared_ptr<MediaBuffer> decode(const MediaBuffer& input) override;

  /// Not applicable for video codecs — always returns nullptr.
  std::shared_ptr<MediaBuffer> plc(std::size_t frameSamples) override;

  /// Supported parameters:
  ///   "bitrate" — target bitrate in bps (encoder only)
  ///   "speed" — CPU usage / speed preset (encoder only)
  ///   "framerate" — max frame rate in fps (encoder only)
  ///   "requestKeyFrame" — force keyframe on next encode (encoder only)
  bool setParameter(const std::string& key, std::uint32_t value) override;
  std::uint32_t getParameter(const std::string& key) const override;

private:
  CodecInfo _info;
  VpxMode _mode;
  VpxVariant _variant;
  vpx_codec_ctx_t _ctx;
  vpx_codec_enc_cfg_t _cfg; // encoder only — kept for runtime config changes
  vpx_image_t* _img = nullptr; // encoder only — allocated I420 wrapper
  bool _initialized = false;
  std::uint32_t _width;
  std::uint32_t _height;
  std::uint32_t _bitrate;
  float _framerate;
  std::uint32_t _speed;
  bool _forceKeyFrame = false;
};

} // namespace codecs
} // namespace iora
