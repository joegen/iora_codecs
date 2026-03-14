#include "h264_codec_factory.hpp"

#include <stdexcept>

namespace iora {
namespace codecs {

H264CodecFactory::H264CodecFactory(CodecInfo info,
                                   std::uint32_t width,
                                   std::uint32_t height,
                                   std::uint32_t bitrate,
                                   float framerate)
  : _info(std::move(info))
  , _width(width)
  , _height(height)
  , _bitrate(bitrate)
  , _framerate(framerate)
{
}

const CodecInfo& H264CodecFactory::codecInfo() const
{
  return _info;
}

bool H264CodecFactory::supports(const CodecInfo& info) const
{
  return _info.name == info.name &&
         _info.clockRate == info.clockRate &&
         _info.channels == info.channels;
}

std::unique_ptr<ICodec> H264CodecFactory::createEncoder(const CodecInfo& params)
{
  if (!supports(params))
  {
    return nullptr;
  }
  std::lock_guard<std::mutex> lock(_mutex);
  auto codec = std::make_unique<H264Codec>(_info, H264Mode::Encoder,
                                            _width, _height, _bitrate, _framerate,
                                            _profileIdc);
  if (_pendingKeyFrame)
  {
    codec->setParameter("requestKeyFrame", 1);
    _pendingKeyFrame = false;
  }
  return codec;
}

std::unique_ptr<ICodec> H264CodecFactory::createDecoder(const CodecInfo& params)
{
  if (!supports(params))
  {
    return nullptr;
  }
  std::lock_guard<std::mutex> lock(_mutex);
  return std::make_unique<H264Codec>(_info, H264Mode::Decoder,
                                     _width, _height);
}

void H264CodecFactory::setVideoParams(std::uint32_t width, std::uint32_t height,
                                      std::uint32_t bitrate, float framerate)
{
  if (width == 0 || height == 0)
  {
    throw std::invalid_argument("H264CodecFactory: width and height must be non-zero");
  }
  if ((width % 2) != 0 || (height % 2) != 0)
  {
    throw std::invalid_argument("H264CodecFactory: width and height must be even for I420");
  }

  std::lock_guard<std::mutex> lock(_mutex);
  _width = width;
  _height = height;
  _bitrate = bitrate;
  _framerate = framerate;
}

void H264CodecFactory::setDefaultBitrate(std::uint32_t bps)
{
  std::lock_guard<std::mutex> lock(_mutex);
  _bitrate = bps;
}

void H264CodecFactory::setDefaultProfile(std::uint32_t profileIdc)
{
  std::lock_guard<std::mutex> lock(_mutex);
  _profileIdc = profileIdc;
}

void H264CodecFactory::requestKeyFrame()
{
  std::lock_guard<std::mutex> lock(_mutex);
  _pendingKeyFrame = true;
}

CodecInfo H264CodecFactory::makeH264Info()
{
  CodecInfo info;
  info.name = "H264";
  info.type = CodecType::Video;
  info.mediaSubtype = "H264";
  info.clockRate = 90000;
  info.channels = 0;
  info.defaultPayloadType = 0; // dynamic — negotiated via SDP
  info.defaultBitrate = 500000;
  info.frameSize = std::chrono::microseconds{33333}; // ~30fps
  info.features = CodecFeatures::Cbr | CodecFeatures::Vbr;
  return info;
}

} // namespace codecs
} // namespace iora
