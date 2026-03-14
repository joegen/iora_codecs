#include "g722_codec.hpp"

#include <g722_encoder.h>
#include <g722_decoder.h>

#include <cstring>
#include <stdexcept>

namespace iora {
namespace codecs {

G722Codec::G722Codec(CodecInfo info, G722Mode mode, int rate)
  : _info(std::move(info))
  , _mode(mode)
  , _rate(rate)
{
  if (_mode == G722Mode::Encoder)
  {
    _encoder = g722_encoder_new(_rate, G722_DEFAULT);
    if (_encoder == nullptr)
    {
      throw std::runtime_error("g722_encoder_new failed");
    }
  }
  else
  {
    _decoder = g722_decoder_new(_rate, G722_DEFAULT);
    if (_decoder == nullptr)
    {
      throw std::runtime_error("g722_decoder_new failed");
    }
  }
}

G722Codec::~G722Codec()
{
  if (_encoder != nullptr)
  {
    g722_encoder_destroy(static_cast<G722_ENC_CTX*>(_encoder));
  }
  if (_decoder != nullptr)
  {
    g722_decoder_destroy(static_cast<G722_DEC_CTX*>(_decoder));
  }
}

const CodecInfo& G722Codec::info() const
{
  return _info;
}

std::shared_ptr<MediaBuffer> G722Codec::encode(const MediaBuffer& input)
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

  // Max output: at 64kbps, 320 samples (20ms) -> 160 bytes
  std::size_t maxOutput = static_cast<std::size_t>(samples);
  auto output = MediaBuffer::create(maxOutput);
  output->copyMetadataFrom(input);

  int encoded = g722_encode(
    static_cast<G722_ENC_CTX*>(_encoder),
    pcm,
    samples,
    output->data());

  if (encoded <= 0)
  {
    return nullptr;
  }

  output->setSize(static_cast<std::size_t>(encoded));
  return output;
}

std::shared_ptr<MediaBuffer> G722Codec::decode(const MediaBuffer& input)
{
  if (_decoder == nullptr)
  {
    return nullptr;
  }

  int compressedBytes = static_cast<int>(input.size());

  // Max output: each compressed byte decodes to ~2 samples (at 64kbps)
  std::size_t maxSamples = static_cast<std::size_t>(compressedBytes) * 2;
  std::size_t maxBytes = maxSamples * 2;
  auto output = MediaBuffer::create(maxBytes);
  output->copyMetadataFrom(input);

  int decoded = g722_decode(
    static_cast<G722_DEC_CTX*>(_decoder),
    input.data(),
    compressedBytes,
    reinterpret_cast<std::int16_t*>(output->data()));

  if (decoded <= 0)
  {
    return nullptr;
  }

  output->setSize(static_cast<std::size_t>(decoded) * 2);
  return output;
}

std::shared_ptr<MediaBuffer> G722Codec::plc(std::size_t frameSamples)
{
  if (_decoder == nullptr)
  {
    return nullptr;
  }

  std::size_t outBytes = frameSamples * 2;
  auto output = MediaBuffer::create(outBytes);
  std::memset(output->data(), 0, outBytes);
  output->setSize(outBytes);
  return output;
}

bool G722Codec::setParameter(const std::string& key, std::uint32_t value)
{
  if (_encoder == nullptr)
  {
    return false;
  }

  if (key == "mode")
  {
    int newRate = static_cast<int>(value);
    if (newRate != 48000 && newRate != 56000 && newRate != 64000)
    {
      return false;
    }

    // Create new encoder before destroying old one (safe rollback on failure)
    auto* newEncoder = g722_encoder_new(newRate, G722_DEFAULT);
    if (newEncoder == nullptr)
    {
      return false;
    }
    g722_encoder_destroy(static_cast<G722_ENC_CTX*>(_encoder));
    _encoder = newEncoder;
    _rate = newRate;
    return true;
  }

  return false;
}

std::uint32_t G722Codec::getParameter(const std::string& key) const
{
  if (key == "mode")
  {
    return static_cast<std::uint32_t>(_rate);
  }
  return 0;
}

} // namespace codecs
} // namespace iora
