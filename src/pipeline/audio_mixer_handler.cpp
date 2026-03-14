#include "iora/codecs/pipeline/audio_mixer_handler.hpp"

#include <utility>

namespace iora {
namespace codecs {

AudioMixerHandler::AudioMixerHandler(const MixParams& params,
                                     std::shared_ptr<MediaBufferPool> bufferPool)
  : _mixer(params)
  , _bufferPool(std::move(bufferPool))
{
}

void AudioMixerHandler::addParticipant(std::uint32_t ssrc,
                                       std::shared_ptr<IMediaHandler> outputHandler,
                                       ICodec* decoder)
{
  _mixer.addParticipant(ssrc);

  ParticipantInfo info;
  info.outputHandler = std::move(outputHandler);
  info.decoder = decoder;
  _participants.emplace(ssrc, std::move(info));
}

void AudioMixerHandler::removeParticipant(std::uint32_t ssrc)
{
  _mixer.removeParticipant(ssrc);
  _participants.erase(ssrc);
}

void AudioMixerHandler::incoming(std::shared_ptr<MediaBuffer> buffer)
{
  if (!buffer || buffer->size() == 0)
  {
    return;
  }

  auto ssrc = buffer->ssrc();
  auto it = _participants.find(ssrc);
  if (it == _participants.end())
  {
    return;
  }

  auto& info = it->second;

  // Bounded frame queue — drop oldest if full.
  if (info.frameQueue.size() >= kMaxFramesPerParticipant)
  {
    info.frameQueue.pop_front();
  }
  info.frameQueue.push_back(std::move(buffer));
}

void AudioMixerHandler::mix()
{
  // Step 1: For each participant, push their oldest buffered frame
  // (or PLC/silence) to the AudioMixer.
  for (auto& [ssrc, info] : _participants)
  {
    if (!info.frameQueue.empty())
    {
      _mixer.pushAudio(ssrc, info.frameQueue.front());
      info.frameQueue.pop_front();
    }
    else if (info.decoder)
    {
      // PLC — generate concealment audio for missing frame.
      auto frameSamples = static_cast<std::size_t>(
        _mixer.params().targetSampleRate * 20 / 1000);
      auto plcBuf = info.decoder->plc(frameSamples);
      if (plcBuf)
      {
        _mixer.pushAudio(ssrc, plcBuf);
      }
      // If PLC returns nullptr, participant is simply missing this round.
    }
    // No decoder and no frame → participant skipped (silence).
  }

  // Step 2: For each participant, get their N-1 mix and forward to output.
  for (auto& [ssrc, info] : _participants)
  {
    if (!info.outputHandler)
    {
      continue;
    }

    auto mixed = _mixer.mixFor(ssrc);
    if (mixed)
    {
      mixed->setSsrc(ssrc);
      info.outputHandler->incoming(std::move(mixed));
    }
  }

  // Step 3: Clear mixer buffers for next round.
  _mixer.clearBuffers();
}

std::size_t AudioMixerHandler::participantCount() const noexcept
{
  return _participants.size();
}

std::size_t AudioMixerHandler::bufferCount(std::uint32_t ssrc) const
{
  auto it = _participants.find(ssrc);
  if (it == _participants.end())
  {
    return 0;
  }
  return it->second.frameQueue.size();
}

} // namespace codecs
} // namespace iora
