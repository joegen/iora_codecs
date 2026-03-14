#pragma once

/// @file av1_codec_factory.hpp
/// @brief Factory for creating AV1 encoder/decoder instances.

#include "av1_codec.hpp"
#include "iora/codecs/codec/i_codec_factory.hpp"

#include <mutex>

namespace iora {
namespace codecs {

class Av1CodecFactory : public ICodecFactory
{
public:
  explicit Av1CodecFactory(CodecInfo info);

  const CodecInfo& codecInfo() const override;
  bool supports(const CodecInfo& info) const override;
  std::unique_ptr<ICodec> createEncoder(const CodecInfo& params) override;
  std::unique_ptr<ICodec> createDecoder(const CodecInfo& params) override;

  /// Configure video resolution/bitrate/framerate for subsequently created codecs.
  void setVideoParams(std::uint32_t width, std::uint32_t height,
                      std::uint32_t bitrate, float framerate);

  /// Configure default bitrate for subsequently created encoders.
  void setDefaultBitrate(std::uint32_t bps);

  /// Configure default speed preset for subsequently created encoders.
  void setDefaultSpeed(std::uint32_t speed);

  /// Store a key-frame request flag consumed by the next created encoder.
  void requestKeyFrame();

  static CodecInfo makeAv1Info();

private:
  CodecInfo _info;
  mutable std::mutex _mutex;
  std::uint32_t _width = 640;
  std::uint32_t _height = 480;
  std::uint32_t _bitrate = 300000;
  float _framerate = 30.0f;
  std::uint32_t _speed = 8;
  bool _pendingKeyFrame = false;
};

} // namespace codecs
} // namespace iora
