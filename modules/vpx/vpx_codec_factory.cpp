#include "vpx_codec_factory.hpp"

#include <stdexcept>

namespace iora {
namespace codecs {

VpxCodecFactory::VpxCodecFactory(CodecInfo info, VpxVariant variant)
  : _info(std::move(info))
  , _variant(variant)
  , _bitrate(variant == VpxVariant::VP8 ? 500000u : 300000u)
  , _speed(variant == VpxVariant::VP8 ? 6u : 7u)
{
}

const CodecInfo& VpxCodecFactory::codecInfo() const
{
  return _info;
}

bool VpxCodecFactory::supports(const CodecInfo& info) const
{
  return _info.name == info.name &&
         _info.clockRate == info.clockRate &&
         _info.channels == info.channels;
}

std::unique_ptr<ICodec> VpxCodecFactory::createEncoder(const CodecInfo& params)
{
  if (!supports(params))
  {
    return nullptr;
  }
  std::lock_guard<std::mutex> lock(_mutex);
  auto codec = std::make_unique<VpxCodec>(_info, VpxMode::Encoder, _variant,
                                           _width, _height, _bitrate, _framerate,
                                           _speed);
  if (_pendingKeyFrame)
  {
    codec->setParameter("requestKeyFrame", 1);
    _pendingKeyFrame = false;
  }
  return codec;
}

std::unique_ptr<ICodec> VpxCodecFactory::createDecoder(const CodecInfo& params)
{
  if (!supports(params))
  {
    return nullptr;
  }
  std::lock_guard<std::mutex> lock(_mutex);
  return std::make_unique<VpxCodec>(_info, VpxMode::Decoder, _variant,
                                     _width, _height);
}

void VpxCodecFactory::setVideoParams(std::uint32_t width, std::uint32_t height,
                                      std::uint32_t bitrate, float framerate)
{
  if (width == 0 || height == 0)
  {
    throw std::invalid_argument("VpxCodecFactory: width and height must be non-zero");
  }
  if ((width % 2) != 0 || (height % 2) != 0)
  {
    throw std::invalid_argument("VpxCodecFactory: width and height must be even for I420");
  }

  std::lock_guard<std::mutex> lock(_mutex);
  _width = width;
  _height = height;
  _bitrate = bitrate;
  _framerate = framerate;
}

void VpxCodecFactory::setDefaultBitrate(std::uint32_t bps)
{
  std::lock_guard<std::mutex> lock(_mutex);
  _bitrate = bps;
}

void VpxCodecFactory::setDefaultSpeed(std::uint32_t speed)
{
  std::lock_guard<std::mutex> lock(_mutex);
  _speed = speed;
}

void VpxCodecFactory::requestKeyFrame()
{
  std::lock_guard<std::mutex> lock(_mutex);
  _pendingKeyFrame = true;
}

CodecInfo VpxCodecFactory::makeVp8Info()
{
  CodecInfo info;
  info.name = "VP8";
  info.type = CodecType::Video;
  info.mediaSubtype = "VP8";
  info.clockRate = 90000;
  info.channels = 0;
  info.defaultPayloadType = 0; // dynamic — negotiated via SDP
  info.defaultBitrate = 500000;
  info.frameSize = std::chrono::microseconds{33333}; // ~30fps
  info.features = CodecFeatures::Cbr | CodecFeatures::Vbr;
  return info;
}

CodecInfo VpxCodecFactory::makeVp9Info()
{
  CodecInfo info;
  info.name = "VP9";
  info.type = CodecType::Video;
  info.mediaSubtype = "VP9";
  info.clockRate = 90000;
  info.channels = 0;
  info.defaultPayloadType = 0; // dynamic — negotiated via SDP
  info.defaultBitrate = 300000;
  info.frameSize = std::chrono::microseconds{33333}; // ~30fps
  info.features = CodecFeatures::Cbr | CodecFeatures::Vbr;
  return info;
}

} // namespace codecs
} // namespace iora
