#include "g729_codec.hpp"

#include <cstring>
#include <stdexcept>

namespace iora {
namespace codecs {

/// Samples per 10ms sub-frame at 8 kHz.
static constexpr int kSubFrameSamples = 80;
/// Bytes per compressed 10ms sub-frame.
static constexpr int kSubFrameBytes = 10;
/// Number of 10ms sub-frames in one 20ms codec frame.
static constexpr int kSubFramesPerFrame = 2;
/// Total samples per 20ms frame.
static constexpr int kFrameSamples = kSubFrameSamples * kSubFramesPerFrame;
/// Total compressed bytes per 20ms frame.
static constexpr int kFrameBytes = kSubFrameBytes * kSubFramesPerFrame;

G729Codec::G729Codec(CodecInfo info, G729Mode mode)
  : _info(std::move(info))
  , _mode(mode)
{
  if (_mode == G729Mode::Encoder)
  {
    _encoder = initBcg729EncoderChannel(0); // VAD disabled — fixed 10-byte output
    if (_encoder == nullptr)
    {
      throw std::runtime_error("initBcg729EncoderChannel failed");
    }
  }
  else
  {
    _decoder = initBcg729DecoderChannel();
    if (_decoder == nullptr)
    {
      throw std::runtime_error("initBcg729DecoderChannel failed");
    }
  }
}

G729Codec::~G729Codec()
{
  if (_encoder != nullptr)
  {
    closeBcg729EncoderChannel(_encoder);
  }
  if (_decoder != nullptr)
  {
    closeBcg729DecoderChannel(_decoder);
  }
}

const CodecInfo& G729Codec::info() const
{
  return _info;
}

std::shared_ptr<MediaBuffer> G729Codec::encode(const MediaBuffer& input)
{
  if (_encoder == nullptr)
  {
    return nullptr;
  }

  // Expect 160 S16 samples (20ms at 8kHz) = 320 bytes
  if (input.size() < static_cast<std::size_t>(kFrameSamples) * 2)
  {
    return nullptr;
  }

  auto output = MediaBuffer::create(kFrameBytes);
  output->copyMetadataFrom(input);

  const auto* pcm = reinterpret_cast<const int16_t*>(input.data());

  // Encode two 10ms sub-frames
  for (int i = 0; i < kSubFramesPerFrame; ++i)
  {
    uint8_t bitStreamLength = 0;
    bcg729Encoder(
      _encoder,
      pcm + (i * kSubFrameSamples),
      output->data() + (i * kSubFrameBytes),
      &bitStreamLength);

    if (bitStreamLength != kSubFrameBytes)
    {
      return nullptr;
    }
  }

  output->setSize(kFrameBytes);
  return output;
}

std::shared_ptr<MediaBuffer> G729Codec::decode(const MediaBuffer& input)
{
  if (_decoder == nullptr)
  {
    return nullptr;
  }

  // Expect 20 bytes (two 10-byte sub-frames)
  if (input.size() < static_cast<std::size_t>(kFrameBytes))
  {
    return nullptr;
  }

  std::size_t outBytes = static_cast<std::size_t>(kFrameSamples) * 2;
  auto output = MediaBuffer::create(outBytes);
  output->copyMetadataFrom(input);

  auto* pcm = reinterpret_cast<int16_t*>(output->data());

  // Decode two 10ms sub-frames
  for (int i = 0; i < kSubFramesPerFrame; ++i)
  {
    bcg729Decoder(
      _decoder,
      input.data() + (i * kSubFrameBytes),
      kSubFrameBytes,
      0, // frameErasureFlag = 0 (normal decode)
      0, // SIDFrameFlag = 0
      0, // rfc3389PayloadFlag = 0
      pcm + (i * kSubFrameSamples));
  }

  output->setSize(outBytes);
  return output;
}

std::shared_ptr<MediaBuffer> G729Codec::plc(std::size_t frameSamples)
{
  if (_decoder == nullptr)
  {
    return nullptr;
  }

  int subFrames = (frameSamples > 0)
    ? static_cast<int>(frameSamples) / kSubFrameSamples
    : kSubFramesPerFrame;
  if (subFrames < 1)
  {
    subFrames = 1;
  }
  if (subFrames > kSubFramesPerFrame)
  {
    subFrames = kSubFramesPerFrame;
  }

  std::size_t totalSamples = static_cast<std::size_t>(subFrames) * kSubFrameSamples;
  std::size_t outBytes = totalSamples * 2;
  auto output = MediaBuffer::create(outBytes);

  auto* pcm = reinterpret_cast<int16_t*>(output->data());

  for (int i = 0; i < subFrames; ++i)
  {
    bcg729Decoder(
      _decoder,
      nullptr,
      0,
      1, // frameErasureFlag = 1 (PLC)
      0, // SIDFrameFlag = 0
      0, // rfc3389PayloadFlag = 0
      pcm + (i * kSubFrameSamples));
  }

  output->setSize(outBytes);
  return output;
}

bool G729Codec::setParameter(const std::string& /*key*/, std::uint32_t /*value*/)
{
  // G.729A has no runtime-configurable parameters (fixed 8 kbps)
  return false;
}

std::uint32_t G729Codec::getParameter(const std::string& /*key*/) const
{
  return 0;
}

} // namespace codecs
} // namespace iora
