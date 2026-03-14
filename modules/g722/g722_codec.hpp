#pragma once

/// @file g722_codec.hpp
/// @brief G.722 wideband codec implementation wrapping libg722 encoder/decoder.

#include "iora/codecs/codec/i_codec.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace iora {
namespace codecs {

/// Operation mode for G722Codec — encoder or decoder.
enum class G722Mode : std::uint8_t
{
  Encoder,
  Decoder
};

/// ICodec implementation wrapping libg722.
///
/// G.722 is stateful — the encoder and decoder maintain sub-band ADPCM
/// state. Separate instances must be created for encoding and decoding
/// via G722CodecFactory.
///
/// NOTE: RTP clock rate is 8000 Hz per RFC 3551 (historical anomaly),
/// but the actual audio sample rate is 16000 Hz.
class G722Codec : public ICodec
{
public:
  /// Construct a G.722 encoder or decoder.
  /// @param info CodecInfo describing this codec instance.
  /// @param mode Encoder or Decoder.
  /// @param rate Bitrate: 64000, 56000, or 48000 bps (default 64000).
  G722Codec(CodecInfo info, G722Mode mode, int rate = 64000);
  ~G722Codec() override;

  // Non-copyable, non-movable (opaque libg722 handles).
  G722Codec(const G722Codec&) = delete;
  G722Codec& operator=(const G722Codec&) = delete;
  G722Codec(G722Codec&&) = delete;
  G722Codec& operator=(G722Codec&&) = delete;

  const CodecInfo& info() const override;

  /// Encode S16 PCM (16kHz) -> compressed G.722 frame.
  std::shared_ptr<MediaBuffer> encode(const MediaBuffer& input) override;

  /// Decode compressed G.722 frame -> S16 PCM (16kHz).
  std::shared_ptr<MediaBuffer> decode(const MediaBuffer& input) override;

  /// Packet loss concealment — zero-fill (G.722 has no built-in PLC).
  std::shared_ptr<MediaBuffer> plc(std::size_t frameSamples) override;

  bool setParameter(const std::string& key, std::uint32_t value) override;
  std::uint32_t getParameter(const std::string& key) const override;

private:
  CodecInfo _info;
  G722Mode _mode;
  void* _encoder = nullptr;
  void* _decoder = nullptr;
  int _rate;
};

} // namespace codecs
} // namespace iora
