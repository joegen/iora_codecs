#pragma once

/// @file audio_mixer.hpp
/// @brief Audio mixing engine for N-way conference support.

#include "iora/codecs/core/media_buffer.hpp"
#include "iora/codecs/core/media_buffer_pool.hpp"
#include "iora/codecs/core/media_clock.hpp"
#include "iora/codecs/dsp/resampler.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

namespace iora {
namespace codecs {

/// Mixing algorithm selection.
enum class MixAlgorithm
{
  /// Divide summed samples by (N-1) source count. Prevents clipping
  /// but output volume decreases with many participants.
  SampleAverage,

  /// Sum samples with int16_t saturation clamping. Louder but clips
  /// with many loud participants.
  SaturatingAdd,

  /// Automatic gain control — track peak level over a sliding window
  /// and apply gain to maintain consistent output level.
  AgcNormalized
};

/// Configuration for AudioMixer.
struct MixParams
{
  std::uint32_t targetSampleRate = 16000;
  std::uint32_t channels = 1;
  std::uint32_t maxParticipants = 32;
  MixAlgorithm algorithm = MixAlgorithm::SampleAverage;

  /// AGC target peak level (fraction of int16_t max). Only used
  /// when algorithm == AgcNormalized.
  float agcTargetLevel = 0.7f;

  /// AGC smoothing window in frames. Higher = smoother gain changes.
  std::uint32_t agcWindowFrames = 25;

  /// Clock drift threshold in PPM before sample insertion/dropping.
  double driftThresholdPpm = 50.0;

  /// Enable VAD-based silence exclusion.
  bool enableVad = false;

  /// VAD energy threshold (RMS below this = silence).
  float vadSilenceThreshold = 100.0f;

  /// Maximum active speakers to mix (0 = no limit).
  /// When > 0, only the top N loudest participants are mixed.
  std::uint32_t maxActiveSpeakers = 0;
};

/// Audio mixing engine — takes N decoded PCM input streams and produces
/// per-participant output where each hears all OTHER participants.
///
/// N-1 mix semantics: for participant P, output = mix of all participants
/// except P. Uses efficient subtract-from-total algorithm: compute
/// total_sum once (O(N)), then output_P = total_sum - P for each P.
///
/// NOT thread-safe — caller must serialize pushAudio() and mixFor().
/// Typical pattern: all pushAudio() calls complete within one timer
/// interval, then all mixFor() calls, before the next interval.
class AudioMixer
{
public:
  explicit AudioMixer(const MixParams& params);

  /// Register a participant for mixing. Pre-allocates buffers.
  void addParticipant(std::uint32_t participantId);

  /// Optionally register with a specific input sample rate that
  /// differs from targetSampleRate. A Resampler is created for
  /// this participant.
  void addParticipant(std::uint32_t participantId, std::uint32_t inputSampleRate);

  /// Unregister a participant and free their buffers.
  void removeParticipant(std::uint32_t participantId);

  /// Submit a decoded S16 PCM frame from one participant.
  /// Buffer data is copied internally. If the participant has a
  /// Resampler, the audio is resampled to targetSampleRate first.
  void pushAudio(std::uint32_t participantId,
                 const std::shared_ptr<MediaBuffer>& buffer);

  /// Returns N-1 mix for the given participant (all others mixed).
  /// Returns nullptr if participant not found, no audio available
  /// from other participants, or only one participant active.
  std::shared_ptr<MediaBuffer> mixFor(std::uint32_t participantId);

  /// Clear all buffered audio (call after a mix round).
  void clearBuffers();

  /// Number of active participants.
  std::size_t participantCount() const noexcept;

  /// Whether a specific participant has buffered audio.
  bool hasAudio(std::uint32_t participantId) const;

  const MixParams& params() const noexcept { return _params; }

  // -- Clock drift compensation (Phase 3) --

  /// Set the MediaClock for a participant (enables drift detection).
  void setParticipantClock(std::uint32_t participantId,
                           std::unique_ptr<MediaClock> clock);

  /// Query drift in PPM between a participant's clock and the mixer
  /// reference clock. Returns 0.0 if no clocks set.
  double driftPpm(std::uint32_t participantId) const;

  // -- VAD and dominant speaker (Phase 4) --

  /// Returns the participant ID of the current dominant speaker.
  /// Returns 0 if no participants have audio.
  std::uint32_t dominantSpeaker() const noexcept;

  /// Returns true if the participant is currently classified as speaking.
  bool isSpeaking(std::uint32_t participantId) const;

  /// Optional callback fired on speech↔silence transitions.
  using VadCallback = std::function<void(std::uint32_t participantId, bool speaking)>;
  void setVadCallback(VadCallback cb);

private:
  struct ParticipantState
  {
    std::vector<std::int16_t> pcmBuffer;
    std::size_t sampleCount = 0;
    bool hasData = false;
    std::optional<Resampler> resampler;
    std::vector<std::int16_t> resampleBuf;

    // Clock drift
    std::unique_ptr<MediaClock> clock;
    double accumulatedDriftSamples = 0.0;

    // VAD state
    float rmsEnergy = 0.0f;
    bool speaking = false;
  };

  void computeTotalSum();
  std::shared_ptr<MediaBuffer> applyAlgorithm(std::uint32_t excludeId);

  static std::int16_t clampToInt16(std::int32_t value) noexcept
  {
    return static_cast<std::int16_t>(
      std::clamp(value, static_cast<std::int32_t>(-32768),
                        static_cast<std::int32_t>(32767)));
  }

  MixParams _params;
  std::unordered_map<std::uint32_t, ParticipantState> _participants;

  // Pre-allocated total sum buffer (int32_t to prevent overflow).
  std::vector<std::int32_t> _totalSum;
  std::size_t _maxSamplesPerFrame = 0;
  std::uint32_t _contributorCount = 0;

  // AGC state
  float _agcGain = 1.0f;
  float _agcPeakHistory = 0.0f;
  std::uint32_t _agcFrameCount = 0;

  // Reference clock for drift detection
  std::unique_ptr<MediaClock> _referenceClock;

  // Dominant speaker tracking
  std::uint32_t _dominantSpeakerId = 0;
  float _dominantSpeakerEnergy = 0.0f;

  // VAD callback
  VadCallback _vadCallback;

  void applyDriftCompensation(ParticipantState& state);
  void updateVadState(std::uint32_t participantId, ParticipantState& state);
  void updateDominantSpeaker();
  bool shouldIncludeInMix(std::uint32_t participantId,
                          const ParticipantState& state) const;
};

} // namespace codecs
} // namespace iora
