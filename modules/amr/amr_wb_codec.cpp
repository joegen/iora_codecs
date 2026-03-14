#include "amr_wb_codec.hpp"

#include <opencore-amrwb/dec_if.h>
#include <vo-amrwbenc/enc_if.h>

#include <cstring>
#include <stdexcept>

namespace iora {
namespace codecs {

static constexpr int kAmrWbFrameSamples = 320;  // 20ms at 16kHz
static constexpr int kAmrWbMaxFrameBytes = 64;  // mode 8: ~61 bytes + ToC

AmrWbCodec::AmrWbCodec(CodecInfo info, AmrWbMode mode,
                       AmrWbBitrateMode bitrateMode, bool dtx)
  : _info(std::move(info))
  , _mode(mode)
  , _bitrateMode(bitrateMode)
  , _dtx(dtx)
{

  if (_mode == AmrWbMode::Encoder)
  {
    _state = E_IF_init();
    if (_state == nullptr)
    {
      throw std::runtime_error("E_IF_init failed");
    }
  }
  else
  {
    _state = D_IF_init();
    if (_state == nullptr)
    {
      throw std::runtime_error("D_IF_init failed");
    }
  }
}

AmrWbCodec::~AmrWbCodec()
{
  if (_state != nullptr)
  {
    if (_mode == AmrWbMode::Encoder)
    {
      E_IF_exit(_state);
    }
    else
    {
      D_IF_exit(_state);
    }
  }
}

const CodecInfo& AmrWbCodec::info() const
{
  return _info;
}

std::shared_ptr<MediaBuffer> AmrWbCodec::encode(const MediaBuffer& input)
{
  if (_state == nullptr || _mode != AmrWbMode::Encoder)
  {
    return nullptr;
  }

  int samples = static_cast<int>(input.size()) / 2;
  if (samples < kAmrWbFrameSamples)
  {
    return nullptr;
  }

  const auto* pcm = reinterpret_cast<const std::int16_t*>(input.data());

  // Single-frame encode: one 320-sample frame per call (symmetric with decoder).
  auto output = MediaBuffer::create(kAmrWbMaxFrameBytes);
  output->copyMetadataFrom(input);

  int encoded = E_IF_encode(
    _state,
    static_cast<int>(_bitrateMode),
    pcm,
    output->data(),
    _dtx ? 1 : 0);

  if (encoded <= 0)
  {
    return nullptr;
  }

  output->setSize(static_cast<std::size_t>(encoded));
  return output;
}

std::shared_ptr<MediaBuffer> AmrWbCodec::decode(const MediaBuffer& input)
{
  if (_state == nullptr || _mode != AmrWbMode::Decoder)
  {
    return nullptr;
  }

  if (input.size() == 0)
  {
    return nullptr;
  }

  // Output: 320 samples * 2 bytes = 640 bytes per frame
  std::size_t outBytes = kAmrWbFrameSamples * 2;
  auto output = MediaBuffer::create(outBytes);
  output->copyMetadataFrom(input);

  D_IF_decode(
    _state,
    input.data(),
    reinterpret_cast<std::int16_t*>(output->data()),
    0); // bfi=0: good frame

  output->setSize(outBytes);
  return output;
}

std::shared_ptr<MediaBuffer> AmrWbCodec::plc(std::size_t /*frameSamples*/)
{
  if (_state == nullptr || _mode != AmrWbMode::Decoder)
  {
    return nullptr;
  }

  // PLC via Bad Frame Indicator — pass dummy input with bfi=1
  std::size_t outBytes = kAmrWbFrameSamples * 2;
  auto output = MediaBuffer::create(outBytes);

  // Zero-filled dummy frame — decoder reads ToC byte and advances pointer,
  // so we need a buffer at least as large as a full frame to stay in bounds.
  std::uint8_t dummyFrame[kAmrWbMaxFrameBytes] = {};
  D_IF_decode(
    _state,
    dummyFrame,
    reinterpret_cast<std::int16_t*>(output->data()),
    1); // bfi=1: bad frame — triggers PLC

  output->setSize(outBytes);
  return output;
}

bool AmrWbCodec::setParameter(const std::string& key, std::uint32_t value)
{
  if (key == "bitrateMode")
  {
    if (_mode != AmrWbMode::Encoder)
    {
      return false;
    }
    if (value > 8)
    {
      return false;
    }
    _bitrateMode = static_cast<AmrWbBitrateMode>(value);
    return true;
  }
  if (key == "dtx")
  {
    if (_mode != AmrWbMode::Encoder)
    {
      return false;
    }
    _dtx = (value != 0);
    return true;
  }

  return false;
}

std::uint32_t AmrWbCodec::getParameter(const std::string& key) const
{
  if (key == "bitrateMode")
  {
    return static_cast<std::uint32_t>(static_cast<int>(_bitrateMode));
  }
  if (key == "dtx")
  {
    return _dtx ? 1 : 0;
  }
  return 0;
}

} // namespace codecs
} // namespace iora
