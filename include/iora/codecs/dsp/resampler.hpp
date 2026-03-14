#pragma once

#include <cstdint>

struct SpeexResamplerState_;

namespace iora {
namespace codecs {

/// Audio sample rate converter wrapping libspeexdsp's speex_resampler.
/// Provides arbitrary sample rate conversion between codec sample rates
/// (e.g., 48kHz Opus <-> 8kHz G.711). RAII lifecycle with move semantics.
class Resampler
{
public:
  /// Construct a resampler for the given rate conversion.
  /// @param inputRate   Input sample rate in Hz (e.g., 48000)
  /// @param outputRate  Output sample rate in Hz (e.g., 8000)
  /// @param channels    Number of interleaved channels (1 = mono, 2 = stereo)
  /// @param quality     Resampling quality 0-10 (default 3 = VOIP, low latency)
  /// @throws std::runtime_error on initialization failure
  Resampler(std::uint32_t inputRate, std::uint32_t outputRate,
            std::uint32_t channels = 1, int quality = 3);

  ~Resampler();

  Resampler(const Resampler&) = delete;
  Resampler& operator=(const Resampler&) = delete;

  Resampler(Resampler&& other) noexcept;
  Resampler& operator=(Resampler&& other) noexcept;

  /// Resample interleaved S16 audio.
  /// @param in      Input sample buffer (interleaved)
  /// @param inLen   [in] available per-channel samples, [out] consumed per-channel samples
  /// @param out     Output sample buffer (interleaved)
  /// @param outLen  [in] output buffer capacity in per-channel samples, [out] written per-channel samples
  /// @return true on success
  bool process(const std::int16_t* in, std::uint32_t& inLen,
               std::int16_t* out, std::uint32_t& outLen);

  /// Resample interleaved F32 audio.
  /// @param in      Input sample buffer (interleaved)
  /// @param inLen   [in] available per-channel samples, [out] consumed per-channel samples
  /// @param out     Output sample buffer (interleaved)
  /// @param outLen  [in] output buffer capacity in per-channel samples, [out] written per-channel samples
  /// @return true on success
  bool processFloat(const float* in, std::uint32_t& inLen,
                    float* out, std::uint32_t& outLen);

  /// Reset internal state (e.g., on SSRC change).
  void reset();

  /// Set resampling quality (0 = fastest/lowest, 10 = slowest/highest).
  void setQuality(int quality);

  /// Get current resampling quality.
  int getQuality() const;

  /// Change input/output sample rates on the fly.
  /// @return true on success
  bool setRate(std::uint32_t inputRate, std::uint32_t outputRate);

  std::uint32_t inputRate() const noexcept { return _inputRate; }
  std::uint32_t outputRate() const noexcept { return _outputRate; }
  std::uint32_t channels() const noexcept { return _channels; }

  /// Get input latency in samples.
  int inputLatency() const;

  /// Get output latency in samples.
  int outputLatency() const;

  /// Estimate output sample count for buffer pre-allocation (rounds up).
  /// @param inputSamples  Number of per-channel input samples
  /// @param inputRate     Input sample rate in Hz
  /// @param outputRate    Output sample rate in Hz
  /// @return estimated per-channel output samples (may be +-1 due to resampler state)
  static std::uint32_t estimateOutputSamples(std::uint32_t inputSamples,
                                             std::uint32_t inputRate,
                                             std::uint32_t outputRate);

private:
  SpeexResamplerState_* _state = nullptr;
  std::uint32_t _inputRate = 0;
  std::uint32_t _outputRate = 0;
  std::uint32_t _channels = 0;
};

} // namespace codecs
} // namespace iora
