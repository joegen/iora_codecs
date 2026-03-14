#pragma once

/// @file audio_mixer_handler.hpp
/// @brief IMediaHandler for N-way audio conference mixing.

#include "iora/codecs/codec/i_codec.hpp"
#include "iora/codecs/core/media_buffer_pool.hpp"
#include "iora/codecs/dsp/audio_mixer.hpp"
#include "iora/codecs/pipeline/i_media_handler.hpp"

#include <cstdint>
#include <deque>
#include <memory>
#include <unordered_map>

namespace iora {
namespace codecs {

/// IMediaHandler that mixes N audio input streams into per-participant
/// N-1 output streams for conference calling.
///
/// This is a fan-in/fan-out terminal node, NOT a linear chain link.
/// Per-participant mixed output is forwarded by calling
/// outputHandler->incoming(mixedBuffer) directly on each participant's
/// stored output handler. The inherited IMediaHandler::_next chain
/// is unused.
///
/// ## Usage
///
/// Upstream: each participant has a TranscodingHandler that decodes
/// and resamples to a common rate, then feeds into this handler's
/// incoming() method.
///
/// Downstream: each participant has a separate output chain
/// (e.g., TranscodingHandler for re-encoding → RTP) registered via
/// addParticipant().
///
/// ## Mixing cadence
///
/// Timer-driven: the caller invokes mix() at a fixed interval
/// (e.g., 20ms). On each tick, whatever frames are available are
/// mixed. Missing participants get silence (or PLC if decoder
/// provided).
///
/// ## Thread safety
///
/// NOT thread-safe. addParticipant/removeParticipant must not overlap
/// with incoming() or mix() calls.
class AudioMixerHandler : public IMediaHandler
{
public:
  /// Construct a mixer handler.
  /// @param params     Mixing configuration
  /// @param bufferPool Optional shared buffer pool for output
  explicit AudioMixerHandler(const MixParams& params,
                             std::shared_ptr<MediaBufferPool> bufferPool = nullptr);

  /// Register a participant identified by RTP SSRC.
  /// @param ssrc          RTP synchronization source
  /// @param outputHandler Downstream handler chain for this participant
  /// @param decoder       Optional decoder for PLC on missing frames
  void addParticipant(std::uint32_t ssrc,
                      std::shared_ptr<IMediaHandler> outputHandler,
                      ICodec* decoder = nullptr);

  /// Unregister a participant.
  void removeParticipant(std::uint32_t ssrc);

  /// Receive decoded PCM from a participant (identified by SSRC
  /// in the buffer metadata). Buffers the frame for the next mix round.
  void incoming(std::shared_ptr<MediaBuffer> buffer) override;

  // outgoing() NOT overridden — this is a terminal node, _next is unused.

  /// Execute a mix round. For each participant with a registered output
  /// handler, produces an N-1 mix and forwards it. Call this on a
  /// timer (e.g., every 20ms).
  void mix();

  /// Number of active participants.
  std::size_t participantCount() const noexcept;

  /// Frames buffered for a specific participant (diagnostic).
  std::size_t bufferCount(std::uint32_t ssrc) const;

private:
  static constexpr std::size_t kMaxFramesPerParticipant = 3;

  struct ParticipantInfo
  {
    std::shared_ptr<IMediaHandler> outputHandler;
    ICodec* decoder = nullptr;
    std::deque<std::shared_ptr<MediaBuffer>> frameQueue;
  };

  AudioMixer _mixer;
  std::shared_ptr<MediaBufferPool> _bufferPool;
  std::unordered_map<std::uint32_t, ParticipantInfo> _participants;
};

} // namespace codecs
} // namespace iora
