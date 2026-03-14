#include "iora/codecs/dsp/audio_mixer.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace iora {
namespace codecs {

AudioMixer::AudioMixer(const MixParams& params)
  : _params(params)
{
  // Pre-allocate total sum buffer for 20ms at target rate.
  _maxSamplesPerFrame =
    static_cast<std::size_t>(_params.targetSampleRate * 20 / 1000) * _params.channels;
  _totalSum.resize(_maxSamplesPerFrame, 0);
}

void AudioMixer::addParticipant(std::uint32_t participantId)
{
  addParticipant(participantId, _params.targetSampleRate);
}

void AudioMixer::addParticipant(std::uint32_t participantId,
                                std::uint32_t inputSampleRate)
{
  if (_participants.count(participantId))
  {
    return;
  }

  ParticipantState state;
  state.pcmBuffer.resize(_maxSamplesPerFrame, 0);
  state.sampleCount = 0;
  state.hasData = false;

  if (inputSampleRate != _params.targetSampleRate)
  {
    state.resampler.emplace(inputSampleRate, _params.targetSampleRate,
                            _params.channels);
    // Pre-allocate resample output buffer.
    auto inputFrameSamples =
      static_cast<std::uint32_t>(inputSampleRate * 20 / 1000);
    auto estimatedOutput = Resampler::estimateOutputSamples(
      inputFrameSamples, inputSampleRate, _params.targetSampleRate);
    state.resampleBuf.resize((estimatedOutput + 16) * _params.channels, 0);
  }

  _participants.emplace(participantId, std::move(state));
}

void AudioMixer::removeParticipant(std::uint32_t participantId)
{
  _participants.erase(participantId);
}

void AudioMixer::pushAudio(std::uint32_t participantId,
                           const std::shared_ptr<MediaBuffer>& buffer)
{
  auto it = _participants.find(participantId);
  if (it == _participants.end() || !buffer || buffer->size() == 0)
  {
    return;
  }

  auto& state = it->second;
  const auto* inputPtr = reinterpret_cast<const std::int16_t*>(buffer->data());
  auto inputSamples = buffer->size() / sizeof(std::int16_t);

  if (state.resampler)
  {
    auto inLen = static_cast<std::uint32_t>(inputSamples / _params.channels);
    auto outLen = static_cast<std::uint32_t>(state.resampleBuf.size() / _params.channels);

    state.resampler->process(inputPtr, inLen, state.resampleBuf.data(), outLen);

    auto totalSamples = static_cast<std::size_t>(outLen) * _params.channels;
    auto copyCount = std::min(totalSamples, _maxSamplesPerFrame);
    std::memcpy(state.pcmBuffer.data(), state.resampleBuf.data(),
                copyCount * sizeof(std::int16_t));
    state.sampleCount = copyCount;
  }
  else
  {
    auto copyCount = std::min(inputSamples, _maxSamplesPerFrame);
    std::memcpy(state.pcmBuffer.data(), inputPtr,
                copyCount * sizeof(std::int16_t));
    state.sampleCount = copyCount;
  }

  state.hasData = true;

  // Apply drift compensation if clock is set.
  applyDriftCompensation(state);

  // Update VAD state.
  updateVadState(participantId, state);
}

std::shared_ptr<MediaBuffer> AudioMixer::mixFor(std::uint32_t participantId)
{
  auto it = _participants.find(participantId);
  if (it == _participants.end())
  {
    return nullptr;
  }

  // Update dominant speaker before filtering.
  updateDominantSpeaker();

  // Count contributors (participants with data, excluding the target,
  // filtered by VAD and maxActiveSpeakers).
  std::uint32_t sourceCount = 0;
  for (const auto& [id, state] : _participants)
  {
    if (id != participantId && state.hasData && shouldIncludeInMix(id, state))
    {
      ++sourceCount;
    }
  }

  if (sourceCount == 0)
  {
    return nullptr;
  }

  // Compute total sum of all contributors (excluding target participant).
  // Using int32_t accumulator to prevent overflow.
  std::fill(_totalSum.begin(), _totalSum.end(), 0);
  std::size_t maxSamples = 0;

  for (const auto& [id, state] : _participants)
  {
    if (id != participantId && state.hasData && shouldIncludeInMix(id, state))
    {
      maxSamples = std::max(maxSamples, state.sampleCount);
      for (std::size_t i = 0; i < state.sampleCount; ++i)
      {
        _totalSum[i] += static_cast<std::int32_t>(state.pcmBuffer[i]);
      }
    }
  }

  return applyAlgorithm(sourceCount);
}

void AudioMixer::clearBuffers()
{
  for (auto& [id, state] : _participants)
  {
    state.hasData = false;
    state.sampleCount = 0;
  }
}

std::size_t AudioMixer::participantCount() const noexcept
{
  return _participants.size();
}

bool AudioMixer::hasAudio(std::uint32_t participantId) const
{
  auto it = _participants.find(participantId);
  return it != _participants.end() && it->second.hasData;
}

std::shared_ptr<MediaBuffer> AudioMixer::applyAlgorithm(std::uint32_t sourceCount)
{
  // Find actual sample count from totalSum (max of all contributors).
  std::size_t sampleCount = 0;
  for (const auto& [id, state] : _participants)
  {
    if (state.hasData)
    {
      sampleCount = std::max(sampleCount, state.sampleCount);
    }
  }

  if (sampleCount == 0)
  {
    return nullptr;
  }

  auto output = MediaBuffer::create(sampleCount * sizeof(std::int16_t));
  auto* outPtr = reinterpret_cast<std::int16_t*>(output->data());

  switch (_params.algorithm)
  {
    case MixAlgorithm::SampleAverage:
    {
      for (std::size_t i = 0; i < sampleCount; ++i)
      {
        outPtr[i] = clampToInt16(_totalSum[i] / static_cast<std::int32_t>(sourceCount));
      }
      break;
    }

    case MixAlgorithm::SaturatingAdd:
    {
      for (std::size_t i = 0; i < sampleCount; ++i)
      {
        outPtr[i] = clampToInt16(_totalSum[i]);
      }
      break;
    }

    case MixAlgorithm::AgcNormalized:
    {
      // Find peak in current frame.
      std::int32_t framePeak = 0;
      for (std::size_t i = 0; i < sampleCount; ++i)
      {
        framePeak = std::max(framePeak, std::abs(_totalSum[i]));
      }

      // Update smoothed peak (exponential moving average).
      float currentPeak = static_cast<float>(framePeak);
      if (_agcFrameCount == 0)
      {
        _agcPeakHistory = currentPeak;
      }
      else
      {
        float alpha = 2.0f / static_cast<float>(_params.agcWindowFrames + 1);
        _agcPeakHistory = alpha * currentPeak + (1.0f - alpha) * _agcPeakHistory;
      }
      ++_agcFrameCount;

      // Compute gain to reach target level.
      float targetPeak = _params.agcTargetLevel * 32767.0f;
      if (_agcPeakHistory > 1.0f)
      {
        _agcGain = targetPeak / _agcPeakHistory;
        // Limit gain to prevent amplifying silence.
        _agcGain = std::min(_agcGain, 10.0f);
      }

      for (std::size_t i = 0; i < sampleCount; ++i)
      {
        auto scaled = static_cast<std::int32_t>(
          static_cast<float>(_totalSum[i]) * _agcGain);
        outPtr[i] = clampToInt16(scaled);
      }
      break;
    }
  }

  output->setSize(sampleCount * sizeof(std::int16_t));
  return output;
}

// -- Clock drift compensation --

void AudioMixer::setParticipantClock(std::uint32_t participantId,
                                     std::unique_ptr<MediaClock> clock)
{
  auto it = _participants.find(participantId);
  if (it == _participants.end())
  {
    return;
  }
  it->second.clock = std::move(clock);

  // Create reference clock on first assignment.
  if (!_referenceClock)
  {
    _referenceClock = std::make_unique<MediaClock>(_params.targetSampleRate);
  }
}

double AudioMixer::driftPpm(std::uint32_t participantId) const
{
  auto it = _participants.find(participantId);
  if (it == _participants.end() || !it->second.clock || !_referenceClock)
  {
    return 0.0;
  }
  return it->second.clock->driftPpm(*_referenceClock);
}

void AudioMixer::applyDriftCompensation(ParticipantState& state)
{
  if (!state.clock || !_referenceClock || state.sampleCount == 0)
  {
    return;
  }

  double drift = state.clock->driftPpm(*_referenceClock);
  if (std::abs(drift) < _params.driftThresholdPpm)
  {
    return;
  }

  // Accumulate fractional sample drift.
  // drift in PPM → samples per frame:
  // driftSamples = frameSamples * drift / 1e6
  double frameSamples = static_cast<double>(state.sampleCount);
  state.accumulatedDriftSamples += frameSamples * drift / 1'000'000.0;

  if (state.accumulatedDriftSamples >= 1.0)
  {
    // Clock running fast → drop a sample to compress.
    if (state.sampleCount > 1)
    {
      // Remove one sample from the middle.
      auto mid = state.sampleCount / 2;
      for (auto i = mid; i < state.sampleCount - 1; ++i)
      {
        state.pcmBuffer[i] = state.pcmBuffer[i + 1];
      }
      --state.sampleCount;
    }
    state.accumulatedDriftSamples -= 1.0;
  }
  else if (state.accumulatedDriftSamples <= -1.0)
  {
    // Clock running slow → insert a sample to stretch.
    if (state.sampleCount < _maxSamplesPerFrame)
    {
      auto mid = state.sampleCount / 2;
      // Shift samples right.
      for (auto i = state.sampleCount; i > mid; --i)
      {
        state.pcmBuffer[i] = state.pcmBuffer[i - 1];
      }
      // Duplicate the sample at mid.
      ++state.sampleCount;
    }
    state.accumulatedDriftSamples += 1.0;
  }
}

// -- VAD and dominant speaker --

void AudioMixer::updateVadState(std::uint32_t participantId,
                                ParticipantState& state)
{
  if (state.sampleCount == 0)
  {
    state.rmsEnergy = 0.0f;
    return;
  }

  // Compute RMS energy.
  double sum = 0.0;
  for (std::size_t i = 0; i < state.sampleCount; ++i)
  {
    auto s = static_cast<double>(state.pcmBuffer[i]);
    sum += s * s;
  }
  state.rmsEnergy = static_cast<float>(
    std::sqrt(sum / static_cast<double>(state.sampleCount)));

  if (_params.enableVad)
  {
    bool wasSpeaking = state.speaking;
    state.speaking = state.rmsEnergy >= _params.vadSilenceThreshold;

    if (_vadCallback && wasSpeaking != state.speaking)
    {
      _vadCallback(participantId, state.speaking);
    }
  }
  else
  {
    state.speaking = true; // All participants considered speaking if VAD disabled.
  }
}

void AudioMixer::updateDominantSpeaker()
{
  _dominantSpeakerId = 0;
  _dominantSpeakerEnergy = 0.0f;

  for (const auto& [id, state] : _participants)
  {
    if (state.hasData && state.rmsEnergy > _dominantSpeakerEnergy)
    {
      _dominantSpeakerEnergy = state.rmsEnergy;
      _dominantSpeakerId = id;
    }
  }
}

std::uint32_t AudioMixer::dominantSpeaker() const noexcept
{
  return _dominantSpeakerId;
}

bool AudioMixer::isSpeaking(std::uint32_t participantId) const
{
  auto it = _participants.find(participantId);
  if (it == _participants.end())
  {
    return false;
  }
  return it->second.speaking;
}

void AudioMixer::setVadCallback(VadCallback cb)
{
  _vadCallback = std::move(cb);
}

bool AudioMixer::shouldIncludeInMix(std::uint32_t participantId,
                                    const ParticipantState& state) const
{
  // VAD filter: exclude silent participants.
  if (_params.enableVad && !state.speaking)
  {
    return false;
  }

  // maxActiveSpeakers filter: only include top N loudest.
  if (_params.maxActiveSpeakers > 0)
  {
    // Count how many participants have higher energy.
    std::uint32_t louderCount = 0;
    for (const auto& [id, s] : _participants)
    {
      if (id != participantId && s.hasData && s.rmsEnergy > state.rmsEnergy)
      {
        ++louderCount;
      }
    }
    if (louderCount >= _params.maxActiveSpeakers)
    {
      return false;
    }
  }

  return true;
}

} // namespace codecs
} // namespace iora
