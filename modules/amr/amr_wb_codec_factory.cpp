#include "amr_wb_codec_factory.hpp"

namespace iora {
namespace codecs {

AmrWbCodecFactory::AmrWbCodecFactory(CodecInfo info)
  : _info(std::move(info))
{
}

const CodecInfo& AmrWbCodecFactory::codecInfo() const
{
  return _info;
}

bool AmrWbCodecFactory::supports(const CodecInfo& info) const
{
  return _info.name == info.name &&
         _info.clockRate == info.clockRate &&
         _info.channels == info.channels;
}

std::unique_ptr<ICodec> AmrWbCodecFactory::createEncoder(const CodecInfo& params)
{
  if (!supports(params))
  {
    return nullptr;
  }
  std::lock_guard<std::mutex> lock(_mutex);
  return std::make_unique<AmrWbCodec>(_info, AmrWbMode::Encoder,
                                       _defaultBitrateMode, _defaultDtx);
}

std::unique_ptr<ICodec> AmrWbCodecFactory::createDecoder(const CodecInfo& params)
{
  if (!supports(params))
  {
    return nullptr;
  }
  return std::make_unique<AmrWbCodec>(_info, AmrWbMode::Decoder);
}

void AmrWbCodecFactory::setDefaultMode(std::uint32_t bitrateMode)
{
  std::lock_guard<std::mutex> lock(_mutex);
  if (bitrateMode <= 8)
  {
    _defaultBitrateMode = static_cast<AmrWbBitrateMode>(bitrateMode);
  }
}

void AmrWbCodecFactory::setDefaultDtx(bool enable)
{
  std::lock_guard<std::mutex> lock(_mutex);
  _defaultDtx = enable;
}

CodecInfo AmrWbCodecFactory::makeAmrWbInfo()
{
  CodecInfo info;
  info.name = "AMR-WB";
  info.type = CodecType::Audio;
  info.mediaSubtype = "AMR-WB";
  info.clockRate = 16000;
  info.channels = 1;
  info.defaultPayloadType = 0; // dynamic — negotiated via SDP
  info.defaultBitrate = 23850; // mode 8 default
  info.frameSize = std::chrono::microseconds{20000};
  info.features = CodecFeatures::Dtx | CodecFeatures::Vad | CodecFeatures::Plc;
  return info;
}

} // namespace codecs
} // namespace iora
