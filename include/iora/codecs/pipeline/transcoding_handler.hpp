#pragma once

/// @file transcoding_handler.hpp
/// @brief Media handler that decodes, optionally resamples, and re-encodes.

#include "iora/codecs/pipeline/i_media_handler.hpp"
#include "iora/codecs/codec/i_codec.hpp"
#include "iora/codecs/dsp/resampler.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace iora {
namespace codecs {

/// Concrete IMediaHandler that transcodes between two codecs.
///
/// Pipeline: decode (input codec) → resample (if sample rates differ) → encode (output codec).
///
/// A Resampler is automatically inserted when the decoder's clockRate differs
/// from the encoder's clockRate (derived from CodecInfo). If the rates match,
/// no resampler overhead is added.
///
/// ## Directionality
///
/// TranscodingHandler only processes the incoming() direction (receive path).
/// outgoing() inherits IMediaHandler's default forwarding behavior. For
/// bidirectional SBC transcoding, chain two TranscodingHandler instances:
/// one with the Opus decoder + G.711 encoder for incoming, another with
/// the G.711 decoder + Opus encoder for outgoing.
///
/// ## Thread safety
///
/// incoming() and outgoing() are NOT thread-safe with respect to each other
/// or to swapCodecs(). The caller must ensure no concurrent calls. This is
/// consistent with the synchronous pipeline execution model.
///
/// @note swapCodecs() is NOT thread-safe — the caller must ensure no
/// concurrent incoming()/outgoing() calls during a swap. Typical pattern:
/// signaling thread detects codec change, pauses media flow, calls
/// swapCodecs(), resumes flow.
class TranscodingHandler : public IMediaHandler
{
public:
  /// Construct a transcoding handler.
  /// @param decoder  Codec instance for decoding incoming frames
  /// @param encoder  Codec instance for encoding outgoing frames
  /// @param channels Number of audio channels (1 = mono, 2 = stereo)
  /// @throws std::invalid_argument if decoder or encoder is null
  TranscodingHandler(std::unique_ptr<ICodec> decoder,
                     std::unique_ptr<ICodec> encoder,
                     std::uint32_t channels = 1);

  /// Decode → resample (if needed) → encode → forward to next handler.
  void incoming(std::shared_ptr<MediaBuffer> buffer) override;

  // outgoing() is NOT overridden — uses IMediaHandler default (forward).

  /// Query the decoder's codec identity.
  const CodecInfo& decoderInfo() const { return _decoder->info(); }

  /// Query the encoder's codec identity.
  const CodecInfo& encoderInfo() const { return _encoder->info(); }

  /// Replace both codecs. Recreates the Resampler if the new pair has
  /// different clock rates. Reallocates intermediate buffers as needed.
  ///
  /// @note NOT thread-safe — caller must ensure no concurrent
  /// incoming()/outgoing() calls during a swap.
  /// @throws std::invalid_argument if decoder or encoder is null
  void swapCodecs(std::unique_ptr<ICodec> decoder,
                  std::unique_ptr<ICodec> encoder);

  /// True if a Resampler is active (decoder and encoder have different rates).
  bool hasResampler() const noexcept { return _resampler.has_value(); }

private:
  void initPipeline();

  std::unique_ptr<ICodec> _decoder;
  std::unique_ptr<ICodec> _encoder;
  std::optional<Resampler> _resampler;
  std::uint32_t _channels;

  // Pre-allocated intermediate buffers (avoid per-frame heap allocations).
  std::vector<std::int16_t> _resampleBuf;
  std::shared_ptr<MediaBuffer> _resampleMediaBuf;
};

} // namespace codecs
} // namespace iora
