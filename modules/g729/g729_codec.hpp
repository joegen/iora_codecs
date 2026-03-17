#pragma once

/// @file g729_codec.hpp
/// @brief G.729 Annex A codec implementation wrapping bcg729 encoder/decoder.

#include "iora/codecs/codec/i_codec.hpp"

extern "C" {
#include <bcg729/decoder.h>
#include <bcg729/encoder.h>
}

#include <cstdint>
#include <memory>
#include <string>

namespace iora {
namespace codecs {

/// Operation mode for G729Codec — encoder or decoder.
enum class G729Mode : std::uint8_t
{
  Encoder,
  Decoder
};

/// ICodec implementation wrapping bcg729 (G.729 Annex A).
///
/// G.729 is stateful — the encoder maintains adaptive codebook and LSP
/// history, the decoder maintains synthesis filter memory and PLC state.
/// Separate instances must be created for encoding and decoding via
/// G729CodecFactory.
///
/// The ICodec interface presents 20ms frames (two 10ms G.729 sub-frames)
/// to match RFC 3551 default packetization. Internally, each encode/decode
/// call processes two consecutive 10ms sub-frames of 80 samples each.
///
/// Thread-safety: instances must not be shared across threads.
class G729Codec : public ICodec
{
public:
  /// Construct a G.729 encoder or decoder.
  /// @throws std::runtime_error if bcg729 context creation fails.
  G729Codec(CodecInfo info, G729Mode mode);
  ~G729Codec() override;

  // Non-copyable, non-movable (opaque bcg729 handles).
  G729Codec(const G729Codec&) = delete;
  G729Codec& operator=(const G729Codec&) = delete;
  G729Codec(G729Codec&&) = delete;
  G729Codec& operator=(G729Codec&&) = delete;

  const CodecInfo& info() const override;

  /// Encode S16 PCM (8kHz, 160 samples = 20ms) -> 20 bytes (two 10-byte sub-frames).
  std::shared_ptr<MediaBuffer> encode(const MediaBuffer& input) override;

  /// Decode compressed G.729 frame (20 bytes) -> S16 PCM (8kHz, 160 samples).
  std::shared_ptr<MediaBuffer> decode(const MediaBuffer& input) override;

  /// Packet loss concealment via bcg729 frameErasureFlag=1.
  std::shared_ptr<MediaBuffer> plc(std::size_t frameSamples) override;

  bool setParameter(const std::string& key, std::uint32_t value) override;
  std::uint32_t getParameter(const std::string& key) const override;

private:
  CodecInfo _info;
  G729Mode _mode;
  bcg729EncoderChannelContextStruct* _encoder = nullptr;
  bcg729DecoderChannelContextStruct* _decoder = nullptr;
};

} // namespace codecs
} // namespace iora
