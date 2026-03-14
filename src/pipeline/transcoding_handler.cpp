#include "iora/codecs/pipeline/transcoding_handler.hpp"

#include <stdexcept>
#include <utility>

namespace iora {
namespace codecs {

TranscodingHandler::TranscodingHandler(std::unique_ptr<ICodec> decoder,
                                       std::unique_ptr<ICodec> encoder,
                                       std::uint32_t channels)
  : _decoder(std::move(decoder))
  , _encoder(std::move(encoder))
  , _channels(channels)
{
  if (!_decoder)
  {
    throw std::invalid_argument("TranscodingHandler: decoder must not be null");
  }
  if (!_encoder)
  {
    throw std::invalid_argument("TranscodingHandler: encoder must not be null");
  }
  initPipeline();
}

void TranscodingHandler::initPipeline()
{
  auto decoderRate = _decoder->info().clockRate;
  auto encoderRate = _encoder->info().clockRate;

  // Create Resampler only when sample rates differ.
  _resampler.reset();
  if (decoderRate != encoderRate)
  {
    _resampler.emplace(decoderRate, encoderRate, _channels);
  }

  // Pre-allocate resample output buffer for worst-case frame.
  // Use 20ms at 48kHz stereo as upper bound: 960 * 2 = 1920 samples.
  // After upsampling 8kHz→48kHz: 160→960 samples per channel.
  // estimateOutputSamples gives per-channel count; multiply by channels.
  if (_resampler)
  {
    // Estimate for 20ms of decoder-rate audio.
    auto decoderFrameSamples =
      static_cast<std::uint32_t>(decoderRate * 20 / 1000);
    auto estimatedOutput =
      Resampler::estimateOutputSamples(decoderFrameSamples, decoderRate, encoderRate);
    // Add headroom (+16 samples) for resampler state variation.
    auto totalSamples = (estimatedOutput + 16) * _channels;
    _resampleBuf.resize(totalSamples);
    // Pre-allocate the MediaBuffer wrapper for resampled PCM.
    _resampleMediaBuf = MediaBuffer::create(totalSamples * sizeof(std::int16_t));
  }
  else
  {
    _resampleBuf.clear();
    _resampleMediaBuf.reset();
  }
}

void TranscodingHandler::incoming(std::shared_ptr<MediaBuffer> buffer)
{
  if (!buffer || buffer->size() == 0)
  {
    return;
  }

  // Step 1: Decode compressed frame → PCM.
  auto pcm = _decoder->decode(*buffer);
  if (!pcm)
  {
    // Decode failed — attempt PLC.
    auto decoderRate = _decoder->info().clockRate;
    auto frameSamples = static_cast<std::size_t>(decoderRate * 20 / 1000);
    pcm = _decoder->plc(frameSamples);
    if (!pcm)
    {
      // PLC also failed — drop the frame.
      return;
    }
  }

  // Step 2: Resample if needed.
  std::shared_ptr<MediaBuffer> encoderInput;
  if (_resampler)
  {
    auto pcmSamples = static_cast<std::uint32_t>(pcm->size() / sizeof(std::int16_t));
    auto inLen = pcmSamples / _channels; // per-channel
    auto outLen = static_cast<std::uint32_t>(_resampleBuf.size() / _channels);

    const auto* inPtr = reinterpret_cast<const std::int16_t*>(pcm->data());

    _resampler->process(inPtr, inLen, _resampleBuf.data(), outLen);

    // outLen now contains actual per-channel samples written.
    auto totalBytes = static_cast<std::size_t>(outLen) * _channels * sizeof(std::int16_t);
    std::memcpy(_resampleMediaBuf->data(), _resampleBuf.data(), totalBytes);
    _resampleMediaBuf->setSize(totalBytes);
    encoderInput = _resampleMediaBuf;
  }
  else
  {
    encoderInput = std::move(pcm);
  }

  // Step 3: Encode PCM → compressed frame.
  auto encoded = _encoder->encode(*encoderInput);
  if (!encoded)
  {
    return;
  }

  // Step 4: Propagate metadata from the original input buffer.
  encoded->copyMetadataFrom(*buffer);
  encoded->setPayloadType(_encoder->info().defaultPayloadType);

  // Step 5: Forward to next handler.
  forwardIncoming(std::move(encoded));
}

void TranscodingHandler::swapCodecs(std::unique_ptr<ICodec> decoder,
                                    std::unique_ptr<ICodec> encoder)
{
  if (!decoder)
  {
    throw std::invalid_argument("TranscodingHandler::swapCodecs: decoder must not be null");
  }
  if (!encoder)
  {
    throw std::invalid_argument("TranscodingHandler::swapCodecs: encoder must not be null");
  }
  _decoder = std::move(decoder);
  _encoder = std::move(encoder);
  initPipeline();
}

} // namespace codecs
} // namespace iora
