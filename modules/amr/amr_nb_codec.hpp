#pragma once

/// @file amr_nb_codec.hpp
/// @brief AMR-NB codec implementation wrapping opencore-amr encoder/decoder.

#include "iora/codecs/codec/i_codec.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace iora {
namespace codecs {

/// Operation mode for AmrNbCodec — encoder or decoder.
enum class AmrNbMode : std::uint8_t
{
  Encoder,
  Decoder
};

/// AMR-NB bitrate modes (maps to opencore-amr enum Mode).
enum class AmrNbBitrateMode : std::uint8_t
{
  MR475 = 0,  // 4.75 kbps
  MR515 = 1,  // 5.15 kbps
  MR59  = 2,  // 5.90 kbps
  MR67  = 3,  // 6.70 kbps
  MR74  = 4,  // 7.40 kbps
  MR795 = 5,  // 7.95 kbps
  MR102 = 6,  // 10.2 kbps
  MR122 = 7   // 12.2 kbps
};

/// ICodec implementation wrapping opencore-amr for AMR-NB.
///
/// AMR-NB is a stateful narrowband speech codec at 8 kHz.
/// Supports 8 bitrate modes from 4.75 to 12.2 kbps.
/// Separate instances must be created for encoding and decoding.
class AmrNbCodec : public ICodec
{
public:
  /// Construct an AMR-NB encoder or decoder.
  /// @param info CodecInfo describing this codec instance.
  /// @param mode Encoder or Decoder.
  /// @param bitrateMode Bitrate mode (default MR122 = 12.2 kbps).
  /// @param dtx Enable discontinuous transmission (default false).
  AmrNbCodec(CodecInfo info, AmrNbMode mode,
             AmrNbBitrateMode bitrateMode = AmrNbBitrateMode::MR122,
             bool dtx = false);
  ~AmrNbCodec() override;

  // Non-copyable, non-movable (opaque handles).
  AmrNbCodec(const AmrNbCodec&) = delete;
  AmrNbCodec& operator=(const AmrNbCodec&) = delete;
  AmrNbCodec(AmrNbCodec&&) = delete;
  AmrNbCodec& operator=(AmrNbCodec&&) = delete;

  const CodecInfo& info() const override;

  /// Encode S16 PCM (8kHz) -> compressed AMR-NB frame.
  std::shared_ptr<MediaBuffer> encode(const MediaBuffer& input) override;

  /// Decode compressed AMR-NB frame -> S16 PCM (8kHz).
  std::shared_ptr<MediaBuffer> decode(const MediaBuffer& input) override;

  /// Packet loss concealment via decoder BFI flag.
  std::shared_ptr<MediaBuffer> plc(std::size_t frameSamples) override;

  bool setParameter(const std::string& key, std::uint32_t value) override;
  std::uint32_t getParameter(const std::string& key) const override;

private:
  CodecInfo _info;
  AmrNbMode _mode;
  void* _state = nullptr;
  AmrNbBitrateMode _bitrateMode;
  bool _dtx;
};

} // namespace codecs
} // namespace iora
