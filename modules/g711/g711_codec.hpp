#pragma once

/// @file g711_codec.hpp
/// @brief G.711 PCMU (mu-law) and PCMA (A-law) codec implementation.

#include "iora/codecs/codec/i_codec.hpp"
#include "iora/codecs/format/sample_format.hpp"

#include <cstring>
#include <memory>

namespace iora {
namespace codecs {

/// ICodec implementation for G.711 PCMU and PCMA.
///
/// G.711 is stateless — encode/decode are pure lookup table operations
/// with no per-direction state. A single class handles both variants,
/// selected by the CodecInfo passed at construction.
class G711Codec : public ICodec
{
public:
  explicit G711Codec(CodecInfo info)
    : _info(std::move(info))
    , _isMulaw(_info.name == "PCMU")
  {
  }

  const CodecInfo& info() const override
  {
    return _info;
  }

  /// Encode S16 PCM -> compressed G.711 (1 byte per sample).
  /// Input must contain an even number of bytes (S16 samples).
  /// Odd trailing bytes are silently ignored.
  std::shared_ptr<MediaBuffer> encode(const MediaBuffer& input) override
  {
    std::size_t sampleCount = input.size() / 2;
    auto output = MediaBuffer::create(sampleCount);
    output->copyMetadataFrom(input);

    const auto* pcm = reinterpret_cast<const std::int16_t*>(input.data());
    auto* compressed = output->data();

    if (_isMulaw)
    {
      for (std::size_t i = 0; i < sampleCount; ++i)
      {
        compressed[i] = detail::s16ToMulaw(pcm[i]);
      }
    }
    else
    {
      for (std::size_t i = 0; i < sampleCount; ++i)
      {
        compressed[i] = detail::s16ToAlaw(pcm[i]);
      }
    }

    output->setSize(sampleCount);
    return output;
  }

  /// Decode compressed G.711 -> S16 PCM (2 bytes per sample).
  std::shared_ptr<MediaBuffer> decode(const MediaBuffer& input) override
  {
    std::size_t sampleCount = input.size();
    auto output = MediaBuffer::create(sampleCount * 2);
    output->copyMetadataFrom(input);

    const auto* compressed = input.data();
    auto* pcm = reinterpret_cast<std::int16_t*>(output->data());

    if (_isMulaw)
    {
      for (std::size_t i = 0; i < sampleCount; ++i)
      {
        pcm[i] = detail::kMulawToS16[compressed[i]];
      }
    }
    else
    {
      for (std::size_t i = 0; i < sampleCount; ++i)
      {
        pcm[i] = detail::kAlawToS16[compressed[i]];
      }
    }

    output->setSize(sampleCount * 2);
    return output;
  }

  /// PLC: zero-fill (G.711 has no built-in PLC algorithm).
  std::shared_ptr<MediaBuffer> plc(std::size_t frameSamples) override
  {
    std::size_t bytes = frameSamples * 2;
    auto output = MediaBuffer::create(bytes);
    std::memset(output->data(), 0, bytes);
    output->setSize(bytes);
    return output;
  }

  bool setParameter(const std::string& /*key*/, std::uint32_t /*value*/) override
  {
    return false;
  }

  std::uint32_t getParameter(const std::string& /*key*/) const override
  {
    return 0;
  }

private:
  CodecInfo _info;
  const bool _isMulaw;
};

} // namespace codecs
} // namespace iora
