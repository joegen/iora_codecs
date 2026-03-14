#pragma once

/// @file h264_codec.hpp
/// @brief H.264 codec implementation wrapping OpenH264 encoder/decoder.

#include "iora/codecs/codec/i_codec.hpp"

#include <cstdint>
#include <memory>
#include <string>

class ISVCEncoder;
class ISVCDecoder;

namespace iora {
namespace codecs {

/// Operation mode for H264Codec — encoder or decoder.
enum class H264Mode : std::uint8_t
{
  Encoder,
  Decoder
};

/// ICodec implementation wrapping Cisco OpenH264 for H.264.
///
/// H.264 is a stateful video codec. The encoder produces Annex-B NAL
/// bitstream from I420 input. The decoder accepts Annex-B NAL bitstream
/// and outputs I420 frames. Separate instances must be created for
/// encoding and decoding.
class H264Codec : public ICodec
{
public:
  /// Construct an H.264 encoder.
  /// @param info CodecInfo describing this codec instance.
  /// @param width Frame width in pixels.
  /// @param height Frame height in pixels.
  /// @param bitrate Target bitrate in bps (default 500000).
  /// @param framerate Max frame rate (default 30.0).
  /// @param profileIdc H.264 profile IDC: 66=Baseline, 77=Main, 100=High.
  H264Codec(CodecInfo info, H264Mode mode,
            std::uint32_t width = 640, std::uint32_t height = 480,
            std::uint32_t bitrate = 500000, float framerate = 30.0f,
            std::uint32_t profileIdc = 66);
  ~H264Codec() override;

  // Non-copyable, non-movable (opaque handles).
  H264Codec(const H264Codec&) = delete;
  H264Codec& operator=(const H264Codec&) = delete;
  H264Codec(H264Codec&&) = delete;
  H264Codec& operator=(H264Codec&&) = delete;

  const CodecInfo& info() const override;

  /// Encode I420 frame -> Annex-B NAL bitstream.
  /// Input must be packed I420: width*height*3/2 bytes (stride = width).
  /// Returns nullptr if encoder skips the frame or on error.
  std::shared_ptr<MediaBuffer> encode(const MediaBuffer& input) override;

  /// Decode Annex-B NAL bitstream -> I420 frame.
  /// Returns nullptr if decoder is still buffering (no output yet).
  /// Output MediaBuffer has video metadata (width, height, stride, pixelFormat).
  std::shared_ptr<MediaBuffer> decode(const MediaBuffer& input) override;

  /// Not applicable for video codecs — always returns nullptr.
  std::shared_ptr<MediaBuffer> plc(std::size_t frameSamples) override;

  /// Supported parameters:
  ///   "bitrate" — target bitrate in bps (encoder only)
  ///   "framerate" — max frame rate in fps (encoder only)
  ///   "requestKeyFrame" — force IDR on next encode (encoder only, any value)
  bool setParameter(const std::string& key, std::uint32_t value) override;
  std::uint32_t getParameter(const std::string& key) const override;

private:
  CodecInfo _info;
  H264Mode _mode;
  ISVCEncoder* _encoder = nullptr;
  ISVCDecoder* _decoder = nullptr;
  std::uint32_t _width;
  std::uint32_t _height;
  std::uint32_t _bitrate;
  float _framerate;
  std::uint32_t _profileIdc;
  bool _forceIdr = false;
};

} // namespace codecs
} // namespace iora
