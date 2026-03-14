#include "iora/codecs/dsp/resampler.hpp"

#include <speex/speex_resampler.h>

#include <stdexcept>
#include <string>
#include <utility>

namespace iora {
namespace codecs {

Resampler::Resampler(std::uint32_t inputRate, std::uint32_t outputRate,
                     std::uint32_t channels, int quality)
  : _inputRate(inputRate)
  , _outputRate(outputRate)
  , _channels(channels)
{
  int err = 0;
  _state = speex_resampler_init(channels, inputRate, outputRate, quality, &err);
  if (!_state || err != RESAMPLER_ERR_SUCCESS)
  {
    throw std::runtime_error(
      std::string("Resampler init failed: ") + speex_resampler_strerror(err));
  }
}

Resampler::~Resampler()
{
  if (_state)
  {
    speex_resampler_destroy(_state);
  }
}

Resampler::Resampler(Resampler&& other) noexcept
  : _state(other._state)
  , _inputRate(other._inputRate)
  , _outputRate(other._outputRate)
  , _channels(other._channels)
{
  other._state = nullptr;
  other._inputRate = 0;
  other._outputRate = 0;
  other._channels = 0;
}

Resampler& Resampler::operator=(Resampler&& other) noexcept
{
  if (this != &other)
  {
    if (_state)
    {
      speex_resampler_destroy(_state);
    }
    _state = other._state;
    _inputRate = other._inputRate;
    _outputRate = other._outputRate;
    _channels = other._channels;

    other._state = nullptr;
    other._inputRate = 0;
    other._outputRate = 0;
    other._channels = 0;
  }
  return *this;
}

bool Resampler::process(const std::int16_t* in, std::uint32_t& inLen,
                        std::int16_t* out, std::uint32_t& outLen)
{
  int err = speex_resampler_process_interleaved_int(
    _state, in, &inLen, out, &outLen);
  return err == RESAMPLER_ERR_SUCCESS;
}

bool Resampler::processFloat(const float* in, std::uint32_t& inLen,
                             float* out, std::uint32_t& outLen)
{
  int err = speex_resampler_process_interleaved_float(
    _state, in, &inLen, out, &outLen);
  return err == RESAMPLER_ERR_SUCCESS;
}

void Resampler::reset()
{
  if (_state)
  {
    speex_resampler_reset_mem(_state);
  }
}

void Resampler::setQuality(int quality)
{
  if (_state)
  {
    speex_resampler_set_quality(_state, quality);
  }
}

int Resampler::getQuality() const
{
  int quality = 0;
  speex_resampler_get_quality(_state, &quality);
  return quality;
}

bool Resampler::setRate(std::uint32_t inputRate, std::uint32_t outputRate)
{
  int err = speex_resampler_set_rate(_state, inputRate, outputRate);
  if (err == RESAMPLER_ERR_SUCCESS)
  {
    _inputRate = inputRate;
    _outputRate = outputRate;
    return true;
  }
  return false;
}

int Resampler::inputLatency() const
{
  return speex_resampler_get_input_latency(_state);
}

int Resampler::outputLatency() const
{
  return speex_resampler_get_output_latency(_state);
}

std::uint32_t Resampler::estimateOutputSamples(std::uint32_t inputSamples,
                                               std::uint32_t inputRate,
                                               std::uint32_t outputRate)
{
  return (static_cast<std::uint64_t>(inputSamples) * outputRate + inputRate - 1) / inputRate;
}

} // namespace codecs
} // namespace iora
