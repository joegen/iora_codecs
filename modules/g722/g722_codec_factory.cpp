#include "g722_codec_factory.hpp"

namespace iora {
namespace codecs {

G722CodecFactory::G722CodecFactory(CodecInfo info)
  : _info(std::move(info))
{
}

const CodecInfo& G722CodecFactory::codecInfo() const
{
  return _info;
}

bool G722CodecFactory::supports(const CodecInfo& info) const
{
  return _info.name == info.name &&
         _info.clockRate == info.clockRate &&
         _info.channels == info.channels;
}

std::unique_ptr<ICodec> G722CodecFactory::createEncoder(const CodecInfo& params)
{
  if (!supports(params))
  {
    return nullptr;
  }
  std::lock_guard<std::mutex> lock(_mutex);
  return std::make_unique<G722Codec>(_info, G722Mode::Encoder, _defaultRate);
}

std::unique_ptr<ICodec> G722CodecFactory::createDecoder(const CodecInfo& params)
{
  if (!supports(params))
  {
    return nullptr;
  }
  std::lock_guard<std::mutex> lock(_mutex);
  return std::make_unique<G722Codec>(_info, G722Mode::Decoder, _defaultRate);
}

void G722CodecFactory::setDefaultMode(std::uint32_t mode)
{
  int rate = 64000;
  if (mode == 1)
  {
    rate = 48000;
  }
  else if (mode == 2)
  {
    rate = 56000;
  }
  else
  {
    rate = 64000;
  }
  std::lock_guard<std::mutex> lock(_mutex);
  _defaultRate = rate;
}

CodecInfo G722CodecFactory::makeG722Info()
{
  CodecInfo info;
  info.name = "G722";
  info.type = CodecType::Audio;
  info.mediaSubtype = "G722";
  info.clockRate = 8000; // RTP clock rate per RFC 3551 (actual sample rate is 16 kHz)
  info.channels = 1;
  info.defaultPayloadType = 9;
  info.defaultBitrate = 64000;
  info.frameSize = std::chrono::microseconds{20000};
  info.features = CodecFeatures::Cbr;
  return info;
}

} // namespace codecs
} // namespace iora
