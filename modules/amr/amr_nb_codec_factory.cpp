#include "amr_nb_codec_factory.hpp"

namespace iora {
namespace codecs {

AmrNbCodecFactory::AmrNbCodecFactory(CodecInfo info)
  : _info(std::move(info))
{
}

const CodecInfo& AmrNbCodecFactory::codecInfo() const
{
  return _info;
}

bool AmrNbCodecFactory::supports(const CodecInfo& info) const
{
  return _info.name == info.name &&
         _info.clockRate == info.clockRate &&
         _info.channels == info.channels;
}

std::unique_ptr<ICodec> AmrNbCodecFactory::createEncoder(const CodecInfo& params)
{
  if (!supports(params))
  {
    return nullptr;
  }
  std::lock_guard<std::mutex> lock(_mutex);
  return std::make_unique<AmrNbCodec>(_info, AmrNbMode::Encoder,
                                       _defaultBitrateMode, _defaultDtx);
}

std::unique_ptr<ICodec> AmrNbCodecFactory::createDecoder(const CodecInfo& params)
{
  if (!supports(params))
  {
    return nullptr;
  }
  return std::make_unique<AmrNbCodec>(_info, AmrNbMode::Decoder);
}

void AmrNbCodecFactory::setDefaultMode(std::uint32_t bitrateMode)
{
  std::lock_guard<std::mutex> lock(_mutex);
  if (bitrateMode <= 7)
  {
    _defaultBitrateMode = static_cast<AmrNbBitrateMode>(bitrateMode);
  }
}

void AmrNbCodecFactory::setDefaultDtx(bool enable)
{
  std::lock_guard<std::mutex> lock(_mutex);
  _defaultDtx = enable;
}

CodecInfo AmrNbCodecFactory::makeAmrNbInfo()
{
  CodecInfo info;
  info.name = "AMR";
  info.type = CodecType::Audio;
  info.mediaSubtype = "AMR";
  info.clockRate = 8000;
  info.channels = 1;
  info.defaultPayloadType = 0; // dynamic — negotiated via SDP
  info.defaultBitrate = 12200; // MR122 default
  info.frameSize = std::chrono::microseconds{20000};
  info.features = CodecFeatures::Dtx | CodecFeatures::Vad | CodecFeatures::Plc;
  return info;
}

} // namespace codecs
} // namespace iora
