#include "opus_codec_factory.hpp"

namespace iora {
namespace codecs {

OpusCodecFactory::OpusCodecFactory(CodecInfo info)
  : _info(std::move(info))
{
}

const CodecInfo& OpusCodecFactory::codecInfo() const
{
  return _info;
}

bool OpusCodecFactory::supports(const CodecInfo& info) const
{
  return _info.name == info.name &&
         _info.clockRate == info.clockRate &&
         _info.channels == info.channels;
}

std::unique_ptr<ICodec> OpusCodecFactory::createEncoder(const CodecInfo& params)
{
  if (!supports(params))
  {
    return nullptr;
  }
  std::lock_guard<std::mutex> lock(_mutex);
  auto codec = std::make_unique<OpusCodec>(_info, OpusMode::Encoder);
  if (_hasBitrate)
  {
    codec->setParameter("bitrate", _defaultBitrate);
  }
  codec->setParameter("complexity", _defaultComplexity);
  codec->setParameter("fec", _defaultFec ? 1 : 0);
  codec->setParameter("dtx", _defaultDtx ? 1 : 0);
  return codec;
}

std::unique_ptr<ICodec> OpusCodecFactory::createDecoder(const CodecInfo& params)
{
  if (!supports(params))
  {
    return nullptr;
  }
  return std::make_unique<OpusCodec>(_info, OpusMode::Decoder);
}

void OpusCodecFactory::setDefaultBitrate(std::uint32_t bps)
{
  std::lock_guard<std::mutex> lock(_mutex);
  _defaultBitrate = bps;
  _hasBitrate = true;
}

void OpusCodecFactory::setDefaultComplexity(std::uint32_t complexity)
{
  std::lock_guard<std::mutex> lock(_mutex);
  _defaultComplexity = complexity;
}

void OpusCodecFactory::setDefaultFec(bool enable)
{
  std::lock_guard<std::mutex> lock(_mutex);
  _defaultFec = enable;
}

void OpusCodecFactory::setDefaultDtx(bool enable)
{
  std::lock_guard<std::mutex> lock(_mutex);
  _defaultDtx = enable;
}

CodecInfo OpusCodecFactory::makeOpusInfo()
{
  CodecInfo info;
  info.name = "opus";
  info.type = CodecType::Audio;
  info.mediaSubtype = "opus";
  info.clockRate = 48000;
  info.channels = 2;
  info.defaultPayloadType = 111;
  info.defaultBitrate = 64000;
  info.frameSize = std::chrono::microseconds{20000};
  info.features = CodecFeatures::Fec | CodecFeatures::Dtx |
                  CodecFeatures::Vad | CodecFeatures::Plc |
                  CodecFeatures::Vbr | CodecFeatures::Cbr;
  return info;
}

} // namespace codecs
} // namespace iora
