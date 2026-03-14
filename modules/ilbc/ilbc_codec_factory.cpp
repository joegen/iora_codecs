#include "ilbc_codec_factory.hpp"

namespace iora {
namespace codecs {

IlbcCodecFactory::IlbcCodecFactory(CodecInfo info)
  : _info(std::move(info))
{
}

const CodecInfo& IlbcCodecFactory::codecInfo() const
{
  return _info;
}

bool IlbcCodecFactory::supports(const CodecInfo& info) const
{
  return _info.name == info.name &&
         _info.clockRate == info.clockRate &&
         _info.channels == info.channels;
}

std::unique_ptr<ICodec> IlbcCodecFactory::createEncoder(const CodecInfo& params)
{
  if (!supports(params))
  {
    return nullptr;
  }
  std::lock_guard<std::mutex> lock(_mutex);
  return std::make_unique<IlbcCodec>(_info, IlbcMode::Encoder, _defaultFrameLenMs);
}

std::unique_ptr<ICodec> IlbcCodecFactory::createDecoder(const CodecInfo& params)
{
  if (!supports(params))
  {
    return nullptr;
  }
  std::lock_guard<std::mutex> lock(_mutex);
  return std::make_unique<IlbcCodec>(_info, IlbcMode::Decoder, _defaultFrameLenMs);
}

void IlbcCodecFactory::setDefaultFrameMode(std::uint32_t frameLenMs)
{
  std::lock_guard<std::mutex> lock(_mutex);
  _defaultFrameLenMs = (frameLenMs == 20) ? 20 : 30;
}

CodecInfo IlbcCodecFactory::makeIlbcInfo()
{
  CodecInfo info;
  info.name = "iLBC";
  info.type = CodecType::Audio;
  info.mediaSubtype = "iLBC";
  info.clockRate = 8000;
  info.channels = 1;
  info.defaultPayloadType = 0; // dynamic — negotiated via SDP
  info.defaultBitrate = 13330; // 30ms mode default
  info.frameSize = std::chrono::microseconds{30000};
  info.features = CodecFeatures::Plc | CodecFeatures::Cbr;
  return info;
}

} // namespace codecs
} // namespace iora
