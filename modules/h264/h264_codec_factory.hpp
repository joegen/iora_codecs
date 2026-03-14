#pragma once

/// @file h264_codec_factory.hpp
/// @brief Factory for creating H.264 encoder/decoder instances.

#include "h264_codec.hpp"
#include "iora/codecs/codec/i_codec_factory.hpp"

#include <mutex>

namespace iora {
namespace codecs {

class H264CodecFactory : public ICodecFactory
{
public:
  explicit H264CodecFactory(CodecInfo info,
                            std::uint32_t width = 640,
                            std::uint32_t height = 480,
                            std::uint32_t bitrate = 500000,
                            float framerate = 30.0f);

  const CodecInfo& codecInfo() const override;
  bool supports(const CodecInfo& info) const override;
  std::unique_ptr<ICodec> createEncoder(const CodecInfo& params) override;
  std::unique_ptr<ICodec> createDecoder(const CodecInfo& params) override;

  /// Configure video resolution/bitrate/framerate for subsequently created codecs.
  /// Width and height must be non-zero and even. Throws std::invalid_argument otherwise.
  void setVideoParams(std::uint32_t width, std::uint32_t height,
                      std::uint32_t bitrate = 500000, float framerate = 30.0f);

  /// Configure default bitrate for subsequently created encoders.
  void setDefaultBitrate(std::uint32_t bps);

  /// Configure default profile IDC for subsequently created encoders.
  /// Common values: 66 (Baseline), 77 (Main), 100 (High).
  void setDefaultProfile(std::uint32_t profileIdc);

  /// Store a key-frame request flag consumed by the next created encoder.
  void requestKeyFrame();

  static CodecInfo makeH264Info();

private:
  CodecInfo _info;
  mutable std::mutex _mutex;
  std::uint32_t _width;
  std::uint32_t _height;
  std::uint32_t _bitrate;
  float _framerate;
  std::uint32_t _profileIdc = 66; // Constrained Baseline
  bool _pendingKeyFrame = false;
};

} // namespace codecs
} // namespace iora
