#pragma once

/// @file opus_codec.hpp
/// @brief Opus codec implementation wrapping libopus encoder/decoder.

#include "iora/codecs/codec/i_codec.hpp"

#include <cstdint>
#include <memory>
#include <string>

struct OpusEncoder;
struct OpusDecoder;

namespace iora {
namespace codecs {

/// Operation mode for OpusCodec — encoder or decoder.
enum class OpusMode : std::uint8_t
{
  Encoder,
  Decoder
};

/// ICodec implementation wrapping libopus.
///
/// Opus is stateful — the encoder tracks signal history and the decoder
/// tracks PLC state. Separate instances must be created for encoding
/// and decoding via OpusCodecFactory.
class OpusCodec : public ICodec
{
public:
  /// Construct an Opus encoder or decoder.
  /// @throws std::runtime_error if opus_encoder_create/opus_decoder_create fails.
  OpusCodec(CodecInfo info, OpusMode mode);
  ~OpusCodec() override;

  // Non-copyable, non-movable (opaque libopus handles).
  OpusCodec(const OpusCodec&) = delete;
  OpusCodec& operator=(const OpusCodec&) = delete;
  OpusCodec(OpusCodec&&) = delete;
  OpusCodec& operator=(OpusCodec&&) = delete;

  const CodecInfo& info() const override;

  /// Encode S16 PCM (48kHz) -> compressed Opus frame.
  std::shared_ptr<MediaBuffer> encode(const MediaBuffer& input) override;

  /// Decode compressed Opus frame -> S16 PCM (48kHz).
  std::shared_ptr<MediaBuffer> decode(const MediaBuffer& input) override;

  /// Packet loss concealment via opus_decode(NULL, 0).
  std::shared_ptr<MediaBuffer> plc(std::size_t frameSamples) override;

  bool setParameter(const std::string& key, std::uint32_t value) override;
  std::uint32_t getParameter(const std::string& key) const override;

private:
  CodecInfo _info;
  OpusMode _mode;
  ::OpusEncoder* _encoder = nullptr;
  ::OpusDecoder* _decoder = nullptr;
  int _channels;
  int _frameSamples; ///< Samples per channel per frame (960 for 20ms at 48kHz)
};

} // namespace codecs
} // namespace iora
