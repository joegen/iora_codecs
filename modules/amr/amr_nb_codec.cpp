#include "amr_nb_codec.hpp"

#include <opencore-amrnb/interf_enc.h>
#include <opencore-amrnb/interf_dec.h>

#include <cstring>
#include <stdexcept>

namespace iora {
namespace codecs {

static constexpr int kAmrNbFrameSamples = 160;  // 20ms at 8kHz
static constexpr int kAmrNbMaxFrameBytes = 33;  // MR122: 32 bytes + 1 ToC byte

AmrNbCodec::AmrNbCodec(CodecInfo info, AmrNbMode mode,
                       AmrNbBitrateMode bitrateMode, bool dtx)
  : _info(std::move(info))
  , _mode(mode)
  , _bitrateMode(bitrateMode)
  , _dtx(dtx)
{
  if (static_cast<int>(_bitrateMode) > 7)
  {
    throw std::runtime_error("Invalid AMR-NB bitrate mode");
  }

  if (_mode == AmrNbMode::Encoder)
  {
    _state = Encoder_Interface_init(dtx ? 1 : 0);
    if (_state == nullptr)
    {
      throw std::runtime_error("Encoder_Interface_init failed");
    }
  }
  else
  {
    _state = Decoder_Interface_init();
    if (_state == nullptr)
    {
      throw std::runtime_error("Decoder_Interface_init failed");
    }
  }
}

AmrNbCodec::~AmrNbCodec()
{
  if (_state != nullptr)
  {
    if (_mode == AmrNbMode::Encoder)
    {
      Encoder_Interface_exit(_state);
    }
    else
    {
      Decoder_Interface_exit(_state);
    }
  }
}

const CodecInfo& AmrNbCodec::info() const
{
  return _info;
}

std::shared_ptr<MediaBuffer> AmrNbCodec::encode(const MediaBuffer& input)
{
  if (_state == nullptr || _mode != AmrNbMode::Encoder)
  {
    return nullptr;
  }

  int samples = static_cast<int>(input.size()) / 2;
  if (samples < kAmrNbFrameSamples)
  {
    return nullptr;
  }

  const auto* pcm = reinterpret_cast<const std::int16_t*>(input.data());

  // Single-frame encode: one 160-sample frame per call (symmetric with decoder).
  auto output = MediaBuffer::create(kAmrNbMaxFrameBytes);
  output->copyMetadataFrom(input);

  int encoded = Encoder_Interface_Encode(
    _state,
    static_cast<Mode>(static_cast<int>(_bitrateMode)),
    pcm,
    output->data(),
    0);

  if (encoded <= 0)
  {
    return nullptr;
  }

  output->setSize(static_cast<std::size_t>(encoded));
  return output;
}

std::shared_ptr<MediaBuffer> AmrNbCodec::decode(const MediaBuffer& input)
{
  if (_state == nullptr || _mode != AmrNbMode::Decoder)
  {
    return nullptr;
  }

  if (input.size() == 0)
  {
    return nullptr;
  }

  // Output: 160 samples * 2 bytes = 320 bytes per frame
  // AMR-NB is one frame per packet in typical RTP usage
  std::size_t outBytes = kAmrNbFrameSamples * 2;
  auto output = MediaBuffer::create(outBytes);
  output->copyMetadataFrom(input);

  Decoder_Interface_Decode(
    _state,
    input.data(),
    reinterpret_cast<std::int16_t*>(output->data()),
    0); // bfi=0: good frame

  output->setSize(outBytes);
  return output;
}

std::shared_ptr<MediaBuffer> AmrNbCodec::plc(std::size_t /*frameSamples*/)
{
  if (_state == nullptr || _mode != AmrNbMode::Decoder)
  {
    return nullptr;
  }

  // PLC via Bad Frame Indicator — pass null input with bfi=1
  std::size_t outBytes = kAmrNbFrameSamples * 2;
  auto output = MediaBuffer::create(outBytes);

  // Zero-filled dummy frame — decoder reads ToC byte and advances pointer,
  // so we need a buffer at least as large as a full frame to stay in bounds.
  std::uint8_t dummyFrame[kAmrNbMaxFrameBytes] = {};
  Decoder_Interface_Decode(
    _state,
    dummyFrame,
    reinterpret_cast<std::int16_t*>(output->data()),
    1); // bfi=1: bad frame — triggers PLC

  output->setSize(outBytes);
  return output;
}

bool AmrNbCodec::setParameter(const std::string& key, std::uint32_t value)
{
  if (key == "bitrateMode")
  {
    if (_mode != AmrNbMode::Encoder)
    {
      return false;
    }
    if (value > 7)
    {
      return false;
    }
    _bitrateMode = static_cast<AmrNbBitrateMode>(value);
    return true;
  }
  if (key == "dtx")
  {
    if (_mode != AmrNbMode::Encoder)
    {
      return false;
    }
    bool newDtx = (value != 0);
    if (newDtx == _dtx)
    {
      return true;
    }
    // DTX is set at init time for AMR-NB — re-create encoder state.
    if (_state != nullptr)
    {
      void* newState = Encoder_Interface_init(newDtx ? 1 : 0);
      if (newState == nullptr)
      {
        return false;
      }
      Encoder_Interface_exit(_state);
      _state = newState;
    }
    _dtx = newDtx;
    return true;
  }

  return false;
}

std::uint32_t AmrNbCodec::getParameter(const std::string& key) const
{
  if (key == "bitrateMode")
  {
    return static_cast<std::uint32_t>(_bitrateMode);
  }
  if (key == "dtx")
  {
    return _dtx ? 1 : 0;
  }
  return 0;
}

} // namespace codecs
} // namespace iora
