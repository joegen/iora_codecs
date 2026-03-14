#pragma once

/// @file ilbc_codec.hpp
/// @brief iLBC codec implementation wrapping libilbc encoder/decoder.

#include "iora/codecs/codec/i_codec.hpp"

#include <cstdint>
#include <memory>
#include <string>

struct iLBC_encinst_t_;
struct iLBC_decinst_t_;

namespace iora {
namespace codecs {

/// Operation mode for IlbcCodec — encoder or decoder.
enum class IlbcMode : std::uint8_t
{
  Encoder,
  Decoder
};

/// ICodec implementation wrapping libilbc.
///
/// iLBC is stateful — encoder and decoder maintain internal prediction
/// state. Separate instances must be created for encoding and decoding
/// via IlbcCodecFactory.
///
/// Supports 20ms mode (160 samples, 38 bytes, 15.2 kbps) and
/// 30ms mode (240 samples, 50 bytes, 13.33 kbps) at 8 kHz mono.
/// iLBC has built-in packet loss concealment.
class IlbcCodec : public ICodec
{
public:
  /// Construct an iLBC encoder or decoder.
  /// @param info CodecInfo describing this codec instance.
  /// @param mode Encoder or Decoder.
  /// @param frameLenMs Frame duration: 20 or 30 ms (default 30).
  IlbcCodec(CodecInfo info, IlbcMode mode, int frameLenMs = 30);
  ~IlbcCodec() override;

  // Non-copyable, non-movable (opaque libilbc handles).
  IlbcCodec(const IlbcCodec&) = delete;
  IlbcCodec& operator=(const IlbcCodec&) = delete;
  IlbcCodec(IlbcCodec&&) = delete;
  IlbcCodec& operator=(IlbcCodec&&) = delete;

  const CodecInfo& info() const override;

  /// Encode S16 PCM (8kHz) -> compressed iLBC frame.
  std::shared_ptr<MediaBuffer> encode(const MediaBuffer& input) override;

  /// Decode compressed iLBC frame -> S16 PCM (8kHz).
  std::shared_ptr<MediaBuffer> decode(const MediaBuffer& input) override;

  /// Packet loss concealment via WebRtcIlbcfix_DecodePlc.
  std::shared_ptr<MediaBuffer> plc(std::size_t frameSamples) override;

  bool setParameter(const std::string& key, std::uint32_t value) override;
  std::uint32_t getParameter(const std::string& key) const override;

private:
  CodecInfo _info;
  IlbcMode _mode;
  iLBC_encinst_t_* _encoder = nullptr;
  iLBC_decinst_t_* _decoder = nullptr;
  int _frameLenMs;
  int _frameSamples;
};

} // namespace codecs
} // namespace iora
