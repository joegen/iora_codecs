#include "ilbc_codec.hpp"

#include <ilbc.h>

#include <cstring>
#include <stdexcept>

namespace iora {
namespace codecs {

IlbcCodec::IlbcCodec(CodecInfo info, IlbcMode mode, int frameLenMs)
  : _info(std::move(info))
  , _mode(mode)
  , _frameLenMs(frameLenMs)
  , _frameSamples(frameLenMs == 20 ? 160 : 240)
{
  if (_mode == IlbcMode::Encoder)
  {
    if (WebRtcIlbcfix_EncoderCreate(&_encoder) != 0 || _encoder == nullptr)
    {
      throw std::runtime_error("WebRtcIlbcfix_EncoderCreate failed");
    }
    if (WebRtcIlbcfix_EncoderInit(_encoder, static_cast<std::int16_t>(_frameLenMs)) != 0)
    {
      WebRtcIlbcfix_EncoderFree(_encoder);
      _encoder = nullptr;
      throw std::runtime_error("WebRtcIlbcfix_EncoderInit failed");
    }
  }
  else
  {
    if (WebRtcIlbcfix_DecoderCreate(&_decoder) != 0 || _decoder == nullptr)
    {
      throw std::runtime_error("WebRtcIlbcfix_DecoderCreate failed");
    }
    if (WebRtcIlbcfix_DecoderInit(_decoder, static_cast<std::int16_t>(_frameLenMs)) != 0)
    {
      WebRtcIlbcfix_DecoderFree(_decoder);
      _decoder = nullptr;
      throw std::runtime_error("WebRtcIlbcfix_DecoderInit failed");
    }
  }
}

IlbcCodec::~IlbcCodec()
{
  if (_encoder != nullptr)
  {
    WebRtcIlbcfix_EncoderFree(_encoder);
  }
  if (_decoder != nullptr)
  {
    WebRtcIlbcfix_DecoderFree(_decoder);
  }
}

const CodecInfo& IlbcCodec::info() const
{
  return _info;
}

std::shared_ptr<MediaBuffer> IlbcCodec::encode(const MediaBuffer& input)
{
  if (_encoder == nullptr)
  {
    return nullptr;
  }

  int samples = static_cast<int>(input.size()) / 2;
  if (samples <= 0)
  {
    return nullptr;
  }

  const auto* pcm = reinterpret_cast<const std::int16_t*>(input.data());

  // Max output: 38 bytes per 20ms frame or 50 bytes per 30ms frame
  // Input may contain multiple frames
  std::size_t maxOutput = static_cast<std::size_t>(samples);
  auto output = MediaBuffer::create(maxOutput);
  output->copyMetadataFrom(input);

  int encoded = WebRtcIlbcfix_Encode(
    _encoder,
    pcm,
    static_cast<std::size_t>(samples),
    output->data());

  if (encoded <= 0)
  {
    return nullptr;
  }

  output->setSize(static_cast<std::size_t>(encoded));
  return output;
}

std::shared_ptr<MediaBuffer> IlbcCodec::decode(const MediaBuffer& input)
{
  if (_decoder == nullptr)
  {
    return nullptr;
  }

  // iLBC decoder supports up to 3 frames per packet — size output accordingly.
  // Compressed frame size: 38 bytes (20ms) or 50 bytes (30ms).
  std::size_t bytesPerFrame = (_frameLenMs == 20) ? 38 : 50;
  std::size_t numFrames = (bytesPerFrame > 0 && input.size() >= bytesPerFrame)
    ? (input.size() / bytesPerFrame) : 1;
  if (numFrames > 3)
  {
    numFrames = 3;
  }
  std::size_t maxSamples = static_cast<std::size_t>(_frameSamples) * numFrames;
  std::size_t maxBytes = maxSamples * 2;
  auto output = MediaBuffer::create(maxBytes);
  output->copyMetadataFrom(input);

  std::int16_t speechType = 0;
  int decoded = WebRtcIlbcfix_Decode(
    _decoder,
    input.data(),
    input.size(),
    reinterpret_cast<std::int16_t*>(output->data()),
    &speechType);

  if (decoded <= 0)
  {
    return nullptr;
  }

  output->setSize(static_cast<std::size_t>(decoded) * 2);
  return output;
}

std::shared_ptr<MediaBuffer> IlbcCodec::plc(std::size_t frameSamples)
{
  if (_decoder == nullptr)
  {
    return nullptr;
  }

  // Always allocate based on decoder's actual frame size, not caller's value,
  // because WebRtcIlbcfix_DecodePlc writes _frameSamples regardless.
  std::size_t decoderSamples = static_cast<std::size_t>(_frameSamples);
  std::size_t outBytes = decoderSamples * 2;
  auto output = MediaBuffer::create(outBytes);

  std::size_t generated = WebRtcIlbcfix_DecodePlc(
    _decoder,
    reinterpret_cast<std::int16_t*>(output->data()),
    1);

  if (generated == 0)
  {
    std::memset(output->data(), 0, outBytes);
    output->setSize(outBytes);
    return output;
  }

  output->setSize(generated * 2);
  return output;
}

bool IlbcCodec::setParameter(const std::string& key, std::uint32_t value)
{
  if (key == "frameMode")
  {
    int newLen = static_cast<int>(value);
    if (newLen != 20 && newLen != 30)
    {
      return false;
    }

    if (_encoder != nullptr)
    {
      if (WebRtcIlbcfix_EncoderInit(_encoder, static_cast<std::int16_t>(newLen)) != 0)
      {
        return false;
      }
    }
    else if (_decoder != nullptr)
    {
      if (WebRtcIlbcfix_DecoderInit(_decoder, static_cast<std::int16_t>(newLen)) != 0)
      {
        return false;
      }
    }
    else
    {
      return false;
    }

    _frameLenMs = newLen;
    _frameSamples = (newLen == 20) ? 160 : 240;
    return true;
  }

  return false;
}

std::uint32_t IlbcCodec::getParameter(const std::string& key) const
{
  if (key == "frameMode")
  {
    return static_cast<std::uint32_t>(_frameLenMs);
  }
  return 0;
}

} // namespace codecs
} // namespace iora
