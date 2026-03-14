#include "opus_codec.hpp"

#include <opus/opus.h>

#include <cstring>
#include <stdexcept>

namespace iora {
namespace codecs {

OpusCodec::OpusCodec(CodecInfo info, OpusMode mode)
  : _info(std::move(info))
  , _mode(mode)
  , _channels(static_cast<int>(_info.channels))
  , _frameSamples(960) // 20ms at 48kHz
{
  int error = 0;

  if (_mode == OpusMode::Encoder)
  {
    _encoder = opus_encoder_create(48000, _channels, OPUS_APPLICATION_VOIP, &error);
    if (error != OPUS_OK || _encoder == nullptr)
    {
      throw std::runtime_error(
        std::string("opus_encoder_create failed: ") + opus_strerror(error));
    }

    // Defaults: VBR on, complexity 5, FEC off, DTX off
    int defaultBitrate = (_channels == 1) ? 32000 : 64000;
    opus_encoder_ctl(_encoder, OPUS_SET_BITRATE(defaultBitrate));
    opus_encoder_ctl(_encoder, OPUS_SET_COMPLEXITY(5));
    opus_encoder_ctl(_encoder, OPUS_SET_VBR(1));
    opus_encoder_ctl(_encoder, OPUS_SET_INBAND_FEC(0));
    opus_encoder_ctl(_encoder, OPUS_SET_DTX(0));
  }
  else
  {
    _decoder = opus_decoder_create(48000, _channels, &error);
    if (error != OPUS_OK || _decoder == nullptr)
    {
      throw std::runtime_error(
        std::string("opus_decoder_create failed: ") + opus_strerror(error));
    }
  }
}

OpusCodec::~OpusCodec()
{
  if (_encoder != nullptr)
  {
    opus_encoder_destroy(_encoder);
  }
  if (_decoder != nullptr)
  {
    opus_decoder_destroy(_decoder);
  }
}

const CodecInfo& OpusCodec::info() const
{
  return _info;
}

std::shared_ptr<MediaBuffer> OpusCodec::encode(const MediaBuffer& input)
{
  if (_encoder == nullptr)
  {
    return nullptr;
  }

  int samples = static_cast<int>(input.size()) / (2 * _channels);
  const auto* pcm = reinterpret_cast<const opus_int16*>(input.data());

  // Max Opus frame: 4000 bytes is generous for any bitrate
  constexpr int kMaxPacket = 4000;
  auto output = MediaBuffer::create(kMaxPacket);
  output->copyMetadataFrom(input);

  int encoded = opus_encode(_encoder, pcm, samples, output->data(), kMaxPacket);
  if (encoded < 0)
  {
    return nullptr;
  }

  output->setSize(static_cast<std::size_t>(encoded));
  return output;
}

std::shared_ptr<MediaBuffer> OpusCodec::decode(const MediaBuffer& input)
{
  if (_decoder == nullptr)
  {
    return nullptr;
  }

  std::size_t maxSamples = static_cast<std::size_t>(_frameSamples) * static_cast<std::size_t>(_channels);
  std::size_t outBytes = maxSamples * 2;
  auto output = MediaBuffer::create(outBytes);
  output->copyMetadataFrom(input);

  int decoded = opus_decode(
    _decoder,
    input.data(),
    static_cast<opus_int32>(input.size()),
    reinterpret_cast<opus_int16*>(output->data()),
    _frameSamples,
    0 // no FEC
  );

  if (decoded < 0)
  {
    return nullptr;
  }

  output->setSize(static_cast<std::size_t>(decoded) * static_cast<std::size_t>(_channels) * 2);
  return output;
}

std::shared_ptr<MediaBuffer> OpusCodec::plc(std::size_t frameSamples)
{
  if (_decoder == nullptr)
  {
    return nullptr;
  }

  int samples = static_cast<int>(frameSamples);
  std::size_t outBytes = frameSamples * static_cast<std::size_t>(_channels) * 2;
  auto output = MediaBuffer::create(outBytes);

  int decoded = opus_decode(
    _decoder,
    nullptr,
    0,
    reinterpret_cast<opus_int16*>(output->data()),
    samples,
    0
  );

  if (decoded < 0)
  {
    std::memset(output->data(), 0, outBytes);
    output->setSize(outBytes);
    return output;
  }

  output->setSize(static_cast<std::size_t>(decoded) * static_cast<std::size_t>(_channels) * 2);
  return output;
}

bool OpusCodec::setParameter(const std::string& key, std::uint32_t value)
{
  if (_encoder == nullptr)
  {
    return false;
  }

  int v = static_cast<int>(value);
  int err = OPUS_OK;

  if (key == "bitrate")
  {
    err = opus_encoder_ctl(_encoder, OPUS_SET_BITRATE(v));
  }
  else if (key == "complexity")
  {
    err = opus_encoder_ctl(_encoder, OPUS_SET_COMPLEXITY(v));
  }
  else if (key == "fec")
  {
    err = opus_encoder_ctl(_encoder, OPUS_SET_INBAND_FEC(v));
  }
  else if (key == "dtx")
  {
    err = opus_encoder_ctl(_encoder, OPUS_SET_DTX(v));
  }
  else if (key == "vbr")
  {
    err = opus_encoder_ctl(_encoder, OPUS_SET_VBR(v));
  }
  else if (key == "vbr_constraint")
  {
    err = opus_encoder_ctl(_encoder, OPUS_SET_VBR_CONSTRAINT(v));
  }
  else if (key == "signal")
  {
    err = opus_encoder_ctl(_encoder, OPUS_SET_SIGNAL(v));
  }
  else if (key == "packet_loss_pct")
  {
    err = opus_encoder_ctl(_encoder, OPUS_SET_PACKET_LOSS_PERC(v));
  }
  else
  {
    return false;
  }

  return err == OPUS_OK;
}

std::uint32_t OpusCodec::getParameter(const std::string& key) const
{
  opus_int32 val = 0;

  if (_encoder != nullptr)
  {
    if (key == "bitrate")
    {
      opus_encoder_ctl(_encoder, OPUS_GET_BITRATE(&val));
    }
    else if (key == "complexity")
    {
      opus_encoder_ctl(_encoder, OPUS_GET_COMPLEXITY(&val));
    }
    else if (key == "fec")
    {
      opus_encoder_ctl(_encoder, OPUS_GET_INBAND_FEC(&val));
    }
    else if (key == "dtx")
    {
      opus_encoder_ctl(_encoder, OPUS_GET_DTX(&val));
    }
    else if (key == "vbr")
    {
      opus_encoder_ctl(_encoder, OPUS_GET_VBR(&val));
    }
    else if (key == "vbr_constraint")
    {
      opus_encoder_ctl(_encoder, OPUS_GET_VBR_CONSTRAINT(&val));
    }
    else if (key == "signal")
    {
      opus_encoder_ctl(_encoder, OPUS_GET_SIGNAL(&val));
    }
    else if (key == "packet_loss_pct")
    {
      opus_encoder_ctl(_encoder, OPUS_GET_PACKET_LOSS_PERC(&val));
    }
  }
  else if (_decoder != nullptr)
  {
    if (key == "sampleRate")
    {
      val = 48000;
    }
    else if (key == "channels")
    {
      val = _channels;
    }
    else if (key == "gain")
    {
      opus_decoder_ctl(_decoder, OPUS_GET_GAIN(&val));
    }
    else if (key == "lastPacketDuration")
    {
      opus_decoder_ctl(_decoder, OPUS_GET_LAST_PACKET_DURATION(&val));
    }
    else if (key == "bandwidth")
    {
      opus_decoder_ctl(_decoder, OPUS_GET_BANDWIDTH(&val));
    }
  }

  return static_cast<std::uint32_t>(val);
}

} // namespace codecs
} // namespace iora
