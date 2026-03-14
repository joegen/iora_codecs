#pragma once

/// @file goertzel_detector.hpp
/// @brief DTMF tone detection using the Goertzel algorithm.

#include "iora/codecs/core/media_buffer.hpp"
#include "iora/codecs/pipeline/i_media_handler.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>

namespace iora {
namespace codecs {

/// Configuration for Goertzel-based DTMF detection.
struct GoertzelParams
{
  float energyThreshold = 1000.0f;
  float normalTwistDb = 4.0f;   ///< Max dB when high > low (ITU-T Q.24)
  float reverseTwistDb = 8.0f;  ///< Max dB when low > high (ITU-T Q.24)
  std::uint32_t minDurationMs = 40;
};

/// Result of DTMF detection on one audio frame.
struct DtmfResult
{
  bool detected = false;
  char digit = '\0';
  float lowFreqMagnitude = 0.0f;
  float highFreqMagnitude = 0.0f;
};

/// Detects DTMF tones in S16 PCM audio using the Goertzel algorithm.
///
/// Efficiently computes DFT magnitude at 8 specific DTMF frequencies
/// (O(N) per frequency). Includes twist limit checking per ITU-T Q.24
/// and minimum duration enforcement.
class GoertzelDetector
{
public:
  explicit GoertzelDetector(std::uint32_t sampleRate = 8000,
                            GoertzelParams params = {})
    : _sampleRate(sampleRate)
    , _params(params)
    , _currentDigit('\0')
    , _consecutiveMs(0)
    , _lastReportedDigit('\0')
  {
  }

  /// Detect DTMF in one frame of S16 PCM samples.
  DtmfResult detect(const std::int16_t* samples, std::size_t sampleCount)
  {
    DtmfResult result;

    if (sampleCount == 0 || samples == nullptr)
    {
      updateDuration('\0', 0);
      return result;
    }

    std::uint32_t frameMs = static_cast<std::uint32_t>(
      sampleCount * 1000 / _sampleRate);

    // Compute Goertzel magnitude for all 8 DTMF frequencies.
    static constexpr double kLowFreqs[4] = {697.0, 770.0, 852.0, 941.0};
    static constexpr double kHighFreqs[4] = {1209.0, 1336.0, 1477.0, 1633.0};

    double lowMags[4];
    double highMags[4];

    for (int i = 0; i < 4; ++i)
    {
      lowMags[i] = computeMagnitude(samples, sampleCount, kLowFreqs[i]);
      highMags[i] = computeMagnitude(samples, sampleCount, kHighFreqs[i]);
    }

    // Find strongest low and high.
    int bestLow = 0;
    int bestHigh = 0;
    for (int i = 1; i < 4; ++i)
    {
      if (lowMags[i] > lowMags[bestLow])
      {
        bestLow = i;
      }
      if (highMags[i] > highMags[bestHigh])
      {
        bestHigh = i;
      }
    }

    double lowMag = lowMags[bestLow];
    double highMag = highMags[bestHigh];

    result.lowFreqMagnitude = static_cast<float>(lowMag);
    result.highFreqMagnitude = static_cast<float>(highMag);

    // Both must exceed threshold.
    if (lowMag < _params.energyThreshold || highMag < _params.energyThreshold)
    {
      updateDuration('\0', frameMs);
      return result;
    }

    // Twist check (asymmetric per ITU-T Q.24).
    if (highMag > lowMag)
    {
      double twistDb = 20.0 * std::log10(highMag / lowMag);
      if (twistDb > _params.normalTwistDb)
      {
        updateDuration('\0', frameMs);
        return result;
      }
    }
    else
    {
      double reverseTwistDb = 20.0 * std::log10(lowMag / highMag);
      if (reverseTwistDb > _params.reverseTwistDb)
      {
        updateDuration('\0', frameMs);
        return result;
      }
    }

    // Map to digit.
    static constexpr char kDigitMap[4][4] = {
      {'1', '2', '3', 'A'},
      {'4', '5', '6', 'B'},
      {'7', '8', '9', 'C'},
      {'*', '0', '#', 'D'}
    };
    char detectedDigit = kDigitMap[bestLow][bestHigh];

    updateDuration(detectedDigit, frameMs);

    // Only report after minimum duration.
    if (_currentDigit == detectedDigit && _consecutiveMs >= _params.minDurationMs)
    {
      result.detected = true;
      result.digit = detectedDigit;
      _lastReportedDigit = detectedDigit;
    }

    return result;
  }

  /// Convenience overload for MediaBuffer.
  DtmfResult detect(const MediaBuffer& buffer)
  {
    if (buffer.size() % sizeof(std::int16_t) != 0)
    {
      return {};
    }
    auto* samples = reinterpret_cast<const std::int16_t*>(buffer.data());
    std::size_t sampleCount = buffer.size() / sizeof(std::int16_t);
    return detect(samples, sampleCount);
  }

  void reset()
  {
    _currentDigit = '\0';
    _consecutiveMs = 0;
    _lastReportedDigit = '\0';
  }

  std::uint32_t sampleRate() const noexcept { return _sampleRate; }
  const GoertzelParams& params() const noexcept { return _params; }

private:
  static constexpr double kPi = 3.14159265358979323846;

  double computeMagnitude(const std::int16_t* samples, std::size_t N,
                          double freq) const
  {
    double coeff = 2.0 * std::cos(2.0 * kPi * freq / _sampleRate);
    double q1 = 0.0;
    double q2 = 0.0;
    for (std::size_t i = 0; i < N; ++i)
    {
      double q0 = coeff * q1 - q2 + static_cast<double>(samples[i]);
      q2 = q1;
      q1 = q0;
    }
    return std::max(0.0, q1 * q1 + q2 * q2 - coeff * q1 * q2);
  }

  void updateDuration(char digit, std::uint32_t frameMs)
  {
    if (digit == _currentDigit && digit != '\0')
    {
      _consecutiveMs += frameMs;
    }
    else
    {
      _currentDigit = digit;
      _consecutiveMs = (digit != '\0') ? frameMs : 0;
    }
  }

  std::uint32_t _sampleRate;
  GoertzelParams _params;
  char _currentDigit;
  std::uint32_t _consecutiveMs;
  char _lastReportedDigit;
};

/// IMediaHandler that monitors PCM for DTMF tones in both directions.
///
/// Always forwards the audio buffer unchanged. Invokes a callback
/// when a tone is detected.
class GoertzelHandler : public IMediaHandler
{
public:
  using DtmfCallback = std::function<void(char digit, std::uint32_t durationMs)>;

  explicit GoertzelHandler(std::uint32_t sampleRate = 8000,
                           GoertzelParams params = {},
                           DtmfCallback callback = nullptr)
    : _detectorIn(sampleRate, params)
    , _detectorOut(sampleRate, params)
    , _callback(std::move(callback))
  {
  }

  void incoming(std::shared_ptr<MediaBuffer> buffer) override
  {
    if (buffer)
    {
      auto result = _detectorIn.detect(*buffer);
      if (result.detected && _callback)
      {
        _callback(result.digit, 0);
      }
    }
    forwardIncoming(std::move(buffer));
  }

  void outgoing(std::shared_ptr<MediaBuffer> buffer) override
  {
    if (buffer)
    {
      auto result = _detectorOut.detect(*buffer);
      if (result.detected && _callback)
      {
        _callback(result.digit, 0);
      }
    }
    forwardOutgoing(std::move(buffer));
  }

  GoertzelDetector& detectorIncoming() noexcept { return _detectorIn; }
  GoertzelDetector& detectorOutgoing() noexcept { return _detectorOut; }

  void setCallback(DtmfCallback cb) { _callback = std::move(cb); }

private:
  GoertzelDetector _detectorIn;
  GoertzelDetector _detectorOut;
  DtmfCallback _callback;
};

} // namespace codecs
} // namespace iora
