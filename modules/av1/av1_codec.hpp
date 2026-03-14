#pragma once

/// @file av1_codec.hpp
/// @brief AV1 codec implementation using libaom (encoder) and dav1d (decoder).

#include "iora/codecs/codec/i_codec.hpp"

#include <cstdint>
#include <memory>
#include <string>

// libaom encoder headers
#include <aom/aom_codec.h>
#include <aom/aom_encoder.h>
#include <aom/aom_image.h>

// Forward-declare dav1d types to avoid header pollution
struct Dav1dContext;
struct Dav1dSettings;

namespace iora {
namespace codecs {

/// Operation mode for Av1Codec — encoder or decoder.
enum class Av1Mode : std::uint8_t
{
  Encoder,
  Decoder
};

/// ICodec implementation using libaom for AV1 encoding and dav1d for decoding.
///
/// AV1 is a stateful video codec. The encoder accepts I420 input and produces
/// an AV1 bitstream using libaom. The decoder accepts an AV1 bitstream and
/// outputs I420 frames using dav1d for high-performance decoding. Separate
/// instances must be created for encoding and decoding.
class Av1Codec : public ICodec
{
public:
  Av1Codec(CodecInfo info, Av1Mode mode,
           std::uint32_t width = 640, std::uint32_t height = 480,
           std::uint32_t bitrate = 300000, float framerate = 30.0f,
           std::uint32_t speed = 8);
  ~Av1Codec() override;

  // Non-copyable, non-movable (opaque codec context).
  Av1Codec(const Av1Codec&) = delete;
  Av1Codec& operator=(const Av1Codec&) = delete;
  Av1Codec(Av1Codec&&) = delete;
  Av1Codec& operator=(Av1Codec&&) = delete;

  const CodecInfo& info() const override;

  /// Encode I420 frame -> AV1 bitstream (libaom).
  std::shared_ptr<MediaBuffer> encode(const MediaBuffer& input) override;

  /// Decode AV1 bitstream -> I420 frame (dav1d).
  std::shared_ptr<MediaBuffer> decode(const MediaBuffer& input) override;

  /// Not applicable for video codecs — always returns nullptr.
  std::shared_ptr<MediaBuffer> plc(std::size_t frameSamples) override;

  /// Supported parameters:
  ///   "bitrate" — target bitrate in bps (encoder only)
  ///   "speed" — CPU usage / speed preset (encoder only)
  ///   "framerate" — max frame rate in fps (encoder only)
  ///   "requestKeyFrame" — force keyframe on next encode (encoder only)
  bool setParameter(const std::string& key, std::uint32_t value) override;
  std::uint32_t getParameter(const std::string& key) const override;

private:
  CodecInfo _info;
  Av1Mode _mode;

  // Encoder state (libaom)
  aom_codec_ctx_t _encCtx;
  aom_codec_enc_cfg_t _cfg;
  aom_image_t* _img = nullptr;
  bool _encoderInitialized = false;

  // Decoder state (dav1d)
  Dav1dContext* _dav1dCtx = nullptr;

  // Shared state
  std::uint32_t _width;
  std::uint32_t _height;
  std::uint32_t _bitrate;
  float _framerate;
  std::uint32_t _speed;
  bool _forceKeyFrame = false;
};

} // namespace codecs
} // namespace iora
