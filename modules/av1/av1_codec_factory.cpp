#include "av1_codec_factory.hpp"

#include <stdexcept>

namespace iora {
namespace codecs {

Av1CodecFactory::Av1CodecFactory(CodecInfo info)
  : _info(std::move(info))
{
}

const CodecInfo& Av1CodecFactory::codecInfo() const
{
  return _info;
}

bool Av1CodecFactory::supports(const CodecInfo& info) const
{
  return _info.name == info.name &&
         _info.clockRate == info.clockRate &&
         _info.channels == info.channels;
}

std::unique_ptr<ICodec> Av1CodecFactory::createEncoder(const CodecInfo& params)
{
  if (!supports(params))
  {
    return nullptr;
  }
  std::lock_guard<std::mutex> lock(_mutex);
  auto codec = std::make_unique<Av1Codec>(_info, Av1Mode::Encoder,
                                           _width, _height, _bitrate, _framerate,
                                           _speed);
  if (_pendingKeyFrame)
  {
    codec->setParameter("requestKeyFrame", 1);
    _pendingKeyFrame = false;
  }
  return codec;
}

std::unique_ptr<ICodec> Av1CodecFactory::createDecoder(const CodecInfo& params)
{
  if (!supports(params))
  {
    return nullptr;
  }
  std::lock_guard<std::mutex> lock(_mutex);
  return std::make_unique<Av1Codec>(_info, Av1Mode::Decoder,
                                     _width, _height);
}

void Av1CodecFactory::setVideoParams(std::uint32_t width, std::uint32_t height,
                                      std::uint32_t bitrate, float framerate)
{
  if (width == 0 || height == 0)
  {
    throw std::invalid_argument("Av1CodecFactory: width and height must be non-zero");
  }
  if ((width % 2) != 0 || (height % 2) != 0)
  {
    throw std::invalid_argument("Av1CodecFactory: width and height must be even for I420");
  }

  std::lock_guard<std::mutex> lock(_mutex);
  _width = width;
  _height = height;
  _bitrate = bitrate;
  _framerate = framerate;
}

void Av1CodecFactory::setDefaultBitrate(std::uint32_t bps)
{
  std::lock_guard<std::mutex> lock(_mutex);
  _bitrate = bps;
}

void Av1CodecFactory::setDefaultSpeed(std::uint32_t speed)
{
  std::lock_guard<std::mutex> lock(_mutex);
  _speed = speed;
}

void Av1CodecFactory::requestKeyFrame()
{
  std::lock_guard<std::mutex> lock(_mutex);
  _pendingKeyFrame = true;
}

CodecInfo Av1CodecFactory::makeAv1Info()
{
  CodecInfo info;
  info.name = "AV1";
  info.type = CodecType::Video;
  info.mediaSubtype = "AV1";
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
