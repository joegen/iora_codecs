#include "g729_codec_factory.hpp"

namespace iora {
namespace codecs {

G729CodecFactory::G729CodecFactory(CodecInfo info)
  : _info(std::move(info))
{
}

const CodecInfo& G729CodecFactory::codecInfo() const
{
  return _info;
}

bool G729CodecFactory::supports(const CodecInfo& info) const
{
  return _info.name == info.name &&
         _info.clockRate == info.clockRate &&
         _info.channels == info.channels;
}

std::unique_ptr<ICodec> G729CodecFactory::createEncoder(const CodecInfo& params)
{
  if (!supports(params))
  {
    return nullptr;
  }
  return std::make_unique<G729Codec>(_info, G729Mode::Encoder);
}

std::unique_ptr<ICodec> G729CodecFactory::createDecoder(const CodecInfo& params)
{
  if (!supports(params))
  {
    return nullptr;
  }
  return std::make_unique<G729Codec>(_info, G729Mode::Decoder);
}

CodecInfo G729CodecFactory::makeG729Info()
{
  CodecInfo info;
  info.name = "G729";
  info.type = CodecType::Audio;
  info.mediaSubtype = "G729";
  info.clockRate = 8000;
  info.channels = 1;
  info.defaultPayloadType = 18;
  info.defaultBitrate = 8000;
  info.frameSize = std::chrono::microseconds{20000};
  info.features = CodecFeatures::Plc | CodecFeatures::Cbr;
  return info;
}

} // namespace codecs
} // namespace iora
