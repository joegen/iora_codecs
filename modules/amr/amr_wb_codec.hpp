#pragma once

/// @file amr_wb_codec.hpp
/// @brief AMR-WB codec implementation wrapping opencore-amr (decode) and vo-amrwbenc (encode).

#include "iora/codecs/codec/i_codec.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace iora {
namespace codecs {

/// Operation mode for AmrWbCodec — encoder or decoder.
enum class AmrWbMode : std::uint8_t
{
  Encoder,
  Decoder
};

/// AMR-WB bitrate modes (maps to vo-amrwbenc VOAMRWB_FRAMETYPE enum).
enum class AmrWbBitrateMode : std::uint8_t
{
  MD66  = 0,  // 6.60 kbps
  MD885 = 1,  // 8.85 kbps
  MD1265 = 2, // 12.65 kbps
  MD1425 = 3, // 14.25 kbps
  MD1585 = 4, // 15.85 kbps
  MD1825 = 5, // 18.25 kbps
  MD1985 = 6, // 19.85 kbps
  MD2305 = 7, // 23.05 kbps
  MD2385 = 8  // 23.85 kbps
};

/// ICodec implementation wrapping opencore-amr and vo-amrwbenc for AMR-WB.
///
/// AMR-WB is a stateful wideband speech codec at 16 kHz.
/// Supports 9 bitrate modes from 6.6 to 23.85 kbps.
/// Encoder uses vo-amrwbenc, decoder uses opencore-amr.
/// Separate instances must be created for encoding and decoding.
class AmrWbCodec : public ICodec
{
public:
  /// Construct an AMR-WB encoder or decoder.
  /// @param info CodecInfo describing this codec instance.
  /// @param mode Encoder or Decoder.
  /// @param bitrateMode Bitrate mode (default MD2385 = 23.85 kbps).
  /// @param dtx Enable discontinuous transmission (default false).
  AmrWbCodec(CodecInfo info, AmrWbMode mode,
             AmrWbBitrateMode bitrateMode = AmrWbBitrateMode::MD2385,
             bool dtx = false);
  ~AmrWbCodec() override;

  // Non-copyable, non-movable (opaque handles).
  AmrWbCodec(const AmrWbCodec&) = delete;
  AmrWbCodec& operator=(const AmrWbCodec&) = delete;
  AmrWbCodec(AmrWbCodec&&) = delete;
  AmrWbCodec& operator=(AmrWbCodec&&) = delete;

  const CodecInfo& info() const override;

  /// Encode S16 PCM (16kHz) -> compressed AMR-WB frame.
  std::shared_ptr<MediaBuffer> encode(const MediaBuffer& input) override;

  /// Decode compressed AMR-WB frame -> S16 PCM (16kHz).
  std::shared_ptr<MediaBuffer> decode(const MediaBuffer& input) override;

  /// Packet loss concealment via decoder BFI flag.
  std::shared_ptr<MediaBuffer> plc(std::size_t frameSamples) override;

  bool setParameter(const std::string& key, std::uint32_t value) override;
  std::uint32_t getParameter(const std::string& key) const override;

private:
  CodecInfo _info;
  AmrWbMode _mode;
  void* _state = nullptr;
  AmrWbBitrateMode _bitrateMode;
  bool _dtx;
};

} // namespace codecs
} // namespace iora
