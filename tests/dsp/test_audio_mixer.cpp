#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "iora/codecs/dsp/audio_mixer.hpp"

#include <cmath>
#include <cstring>
#include <memory>
#include <vector>

using namespace iora::codecs;

namespace {

/// Create a MediaBuffer filled with a constant S16 sample value.
std::shared_ptr<MediaBuffer> makeConstBuffer(std::int16_t value,
                                             std::size_t sampleCount)
{
  auto buf = MediaBuffer::create(sampleCount * sizeof(std::int16_t));
  auto* ptr = reinterpret_cast<std::int16_t*>(buf->data());
  for (std::size_t i = 0; i < sampleCount; ++i)
  {
    ptr[i] = value;
  }
  buf->setSize(sampleCount * sizeof(std::int16_t));
  return buf;
}

/// Create a MediaBuffer filled with a sine wave at the given frequency.
std::shared_ptr<MediaBuffer> makeSineBuffer(float frequency,
                                            std::uint32_t sampleRate,
                                            std::size_t sampleCount,
                                            std::int16_t amplitude = 16000)
{
  auto buf = MediaBuffer::create(sampleCount * sizeof(std::int16_t));
  auto* ptr = reinterpret_cast<std::int16_t*>(buf->data());
  for (std::size_t i = 0; i < sampleCount; ++i)
  {
    float t = static_cast<float>(i) / static_cast<float>(sampleRate);
    ptr[i] = static_cast<std::int16_t>(
      amplitude * std::sin(2.0f * 3.14159265f * frequency * t));
  }
  buf->setSize(sampleCount * sizeof(std::int16_t));
  return buf;
}

/// Read S16 samples from a MediaBuffer.
std::vector<std::int16_t> readSamples(const std::shared_ptr<MediaBuffer>& buf)
{
  auto count = buf->size() / sizeof(std::int16_t);
  std::vector<std::int16_t> samples(count);
  std::memcpy(samples.data(), buf->data(), buf->size());
  return samples;
}

/// Compute RMS of S16 samples.
double computeRms(const std::vector<std::int16_t>& samples)
{
  if (samples.empty())
  {
    return 0.0;
  }
  double sum = 0.0;
  for (auto s : samples)
  {
    sum += static_cast<double>(s) * static_cast<double>(s);
  }
  return std::sqrt(sum / static_cast<double>(samples.size()));
}

} // namespace

// 20ms at 16kHz = 320 samples
static constexpr std::size_t kFrameSamples = 320;
static constexpr std::uint32_t kRate = 16000;

TEST_CASE("AudioMixer: 2-participant N-1 mix", "[dsp][mixer]")
{
  MixParams params;
  params.targetSampleRate = kRate;
  params.channels = 1;
  params.algorithm = MixAlgorithm::SampleAverage;

  AudioMixer mixer(params);
  mixer.addParticipant(1);
  mixer.addParticipant(2);

  auto bufA = makeConstBuffer(1000, kFrameSamples);
  auto bufB = makeConstBuffer(2000, kFrameSamples);

  mixer.pushAudio(1, bufA);
  mixer.pushAudio(2, bufB);

  // Participant 1 hears only participant 2.
  auto mixForA = mixer.mixFor(1);
  REQUIRE(mixForA != nullptr);
  auto samplesA = readSamples(mixForA);
  REQUIRE(samplesA.size() == kFrameSamples);
  // SampleAverage with 1 source: value / 1 = value
  CHECK(samplesA[0] == 2000);

  // Participant 2 hears only participant 1.
  auto mixForB = mixer.mixFor(2);
  REQUIRE(mixForB != nullptr);
  auto samplesB = readSamples(mixForB);
  CHECK(samplesB[0] == 1000);
}

TEST_CASE("AudioMixer: 3-participant N-1 mix", "[dsp][mixer]")
{
  MixParams params;
  params.targetSampleRate = kRate;
  params.channels = 1;
  params.algorithm = MixAlgorithm::SampleAverage;

  AudioMixer mixer(params);
  mixer.addParticipant(1);
  mixer.addParticipant(2);
  mixer.addParticipant(3);

  auto buf1 = makeConstBuffer(1000, kFrameSamples);
  auto buf2 = makeConstBuffer(2000, kFrameSamples);
  auto buf3 = makeConstBuffer(3000, kFrameSamples);

  mixer.pushAudio(1, buf1);
  mixer.pushAudio(2, buf2);
  mixer.pushAudio(3, buf3);

  // Participant 1 hears (2000 + 3000) / 2 = 2500
  auto mix1 = mixer.mixFor(1);
  REQUIRE(mix1 != nullptr);
  CHECK(readSamples(mix1)[0] == 2500);

  // Participant 2 hears (1000 + 3000) / 2 = 2000
  auto mix2 = mixer.mixFor(2);
  REQUIRE(mix2 != nullptr);
  CHECK(readSamples(mix2)[0] == 2000);

  // Participant 3 hears (1000 + 2000) / 2 = 1500
  auto mix3 = mixer.mixFor(3);
  REQUIRE(mix3 != nullptr);
  CHECK(readSamples(mix3)[0] == 1500);
}

TEST_CASE("AudioMixer: single participant returns nullptr", "[dsp][mixer]")
{
  MixParams params;
  params.targetSampleRate = kRate;
  AudioMixer mixer(params);
  mixer.addParticipant(1);

  mixer.pushAudio(1, makeConstBuffer(1000, kFrameSamples));

  // No other participants → nothing to mix.
  CHECK(mixer.mixFor(1) == nullptr);
}

TEST_CASE("AudioMixer: sine wave accuracy", "[dsp][mixer]")
{
  MixParams params;
  params.targetSampleRate = kRate;
  params.algorithm = MixAlgorithm::SaturatingAdd;

  AudioMixer mixer(params);
  mixer.addParticipant(1);
  mixer.addParticipant(2);

  auto sine440 = makeSineBuffer(440.0f, kRate, kFrameSamples, 10000);
  auto sine880 = makeSineBuffer(880.0f, kRate, kFrameSamples, 10000);

  mixer.pushAudio(1, sine440);
  mixer.pushAudio(2, sine880);

  // Participant 1 should hear sine 880Hz.
  auto mix1 = mixer.mixFor(1);
  REQUIRE(mix1 != nullptr);
  auto samples = readSamples(mix1);

  // Verify the output roughly matches the 880Hz input.
  auto expected = readSamples(sine880);
  std::int64_t error = 0;
  for (std::size_t i = 0; i < samples.size(); ++i)
  {
    auto diff = static_cast<std::int64_t>(samples[i]) - expected[i];
    error += diff * diff;
  }
  double mse = static_cast<double>(error) / static_cast<double>(samples.size());
  // MSE should be very small (< 1) since it's a direct copy with SaturatingAdd.
  CHECK(mse < 1.0);
}

TEST_CASE("AudioMixer: int32 accumulator prevents overflow", "[dsp][mixer]")
{
  MixParams params;
  params.targetSampleRate = kRate;
  params.algorithm = MixAlgorithm::SaturatingAdd;

  AudioMixer mixer(params);

  // 4 participants all at max amplitude.
  for (std::uint32_t i = 1; i <= 4; ++i)
  {
    mixer.addParticipant(i);
    mixer.pushAudio(i, makeConstBuffer(32767, kFrameSamples));
  }

  // Participant 1 hears 3 others: sum = 32767 * 3 = 98301.
  // SaturatingAdd clamps to 32767.
  auto mix = mixer.mixFor(1);
  REQUIRE(mix != nullptr);
  auto samples = readSamples(mix);
  CHECK(samples[0] == 32767);
}

TEST_CASE("AudioMixer: overflow stress test with 8 participants", "[dsp][mixer]")
{
  MixParams params;
  params.targetSampleRate = kRate;
  params.algorithm = MixAlgorithm::SaturatingAdd;

  AudioMixer mixer(params);

  for (std::uint32_t i = 1; i <= 8; ++i)
  {
    mixer.addParticipant(i);
    mixer.pushAudio(i, makeConstBuffer(32767, kFrameSamples));
  }

  // Participant 1 hears 7 others: sum = 32767 * 7 = 229369.
  // SaturatingAdd clamps to 32767 without distortion/wrapping.
  auto mix = mixer.mixFor(1);
  REQUIRE(mix != nullptr);
  auto samples = readSamples(mix);
  CHECK(samples[0] == 32767);

  // Negative max amplitude test.
  AudioMixer mixer2(params);
  for (std::uint32_t i = 1; i <= 8; ++i)
  {
    mixer2.addParticipant(i);
    mixer2.pushAudio(i, makeConstBuffer(-32768, kFrameSamples));
  }

  auto mix2 = mixer2.mixFor(1);
  REQUIRE(mix2 != nullptr);
  auto samples2 = readSamples(mix2);
  CHECK(samples2[0] == -32768);
}

TEST_CASE("AudioMixer: add/remove participant mid-session", "[dsp][mixer]")
{
  MixParams params;
  params.targetSampleRate = kRate;
  params.algorithm = MixAlgorithm::SaturatingAdd;

  AudioMixer mixer(params);
  mixer.addParticipant(1);
  mixer.addParticipant(2);

  CHECK(mixer.participantCount() == 2);

  mixer.pushAudio(1, makeConstBuffer(1000, kFrameSamples));
  mixer.pushAudio(2, makeConstBuffer(2000, kFrameSamples));

  auto mix1 = mixer.mixFor(1);
  REQUIRE(mix1 != nullptr);
  CHECK(readSamples(mix1)[0] == 2000);

  // Add participant 3.
  mixer.clearBuffers();
  mixer.addParticipant(3);
  CHECK(mixer.participantCount() == 3);

  mixer.pushAudio(1, makeConstBuffer(1000, kFrameSamples));
  mixer.pushAudio(2, makeConstBuffer(2000, kFrameSamples));
  mixer.pushAudio(3, makeConstBuffer(3000, kFrameSamples));

  // Participant 1 now hears 2 + 3.
  auto mix1b = mixer.mixFor(1);
  REQUIRE(mix1b != nullptr);
  CHECK(readSamples(mix1b)[0] == 5000); // SaturatingAdd: 2000 + 3000

  // Remove participant 2.
  mixer.clearBuffers();
  mixer.removeParticipant(2);
  CHECK(mixer.participantCount() == 2);

  mixer.pushAudio(1, makeConstBuffer(1000, kFrameSamples));
  mixer.pushAudio(3, makeConstBuffer(3000, kFrameSamples));

  auto mix1c = mixer.mixFor(1);
  REQUIRE(mix1c != nullptr);
  CHECK(readSamples(mix1c)[0] == 3000);
}

TEST_CASE("AudioMixer: empty input / zero-size buffer", "[dsp][mixer]")
{
  MixParams params;
  params.targetSampleRate = kRate;
  AudioMixer mixer(params);
  mixer.addParticipant(1);
  mixer.addParticipant(2);

  // Null buffer.
  mixer.pushAudio(1, nullptr);
  CHECK_FALSE(mixer.hasAudio(1));

  // Empty buffer.
  auto empty = MediaBuffer::create(64);
  empty->setSize(0);
  mixer.pushAudio(1, empty);
  CHECK_FALSE(mixer.hasAudio(1));

  // Unknown participant.
  mixer.pushAudio(999, makeConstBuffer(1000, kFrameSamples));
  CHECK(mixer.mixFor(999) == nullptr);
}

TEST_CASE("AudioMixer: duplicate addParticipant is no-op", "[dsp][mixer]")
{
  MixParams params;
  params.targetSampleRate = kRate;
  AudioMixer mixer(params);
  mixer.addParticipant(1);
  mixer.addParticipant(1); // duplicate
  CHECK(mixer.participantCount() == 1);
}

TEST_CASE("AudioMixer: SampleAverage algorithm", "[dsp][mixer][algorithm]")
{
  MixParams params;
  params.targetSampleRate = kRate;
  params.algorithm = MixAlgorithm::SampleAverage;

  AudioMixer mixer(params);
  mixer.addParticipant(1);
  mixer.addParticipant(2);
  mixer.addParticipant(3);

  mixer.pushAudio(1, makeConstBuffer(6000, kFrameSamples));
  mixer.pushAudio(2, makeConstBuffer(4000, kFrameSamples));
  mixer.pushAudio(3, makeConstBuffer(2000, kFrameSamples));

  // Participant 1: (4000 + 2000) / 2 = 3000
  auto mix = mixer.mixFor(1);
  CHECK(readSamples(mix)[0] == 3000);
}

TEST_CASE("AudioMixer: SaturatingAdd algorithm", "[dsp][mixer][algorithm]")
{
  MixParams params;
  params.targetSampleRate = kRate;
  params.algorithm = MixAlgorithm::SaturatingAdd;

  AudioMixer mixer(params);
  mixer.addParticipant(1);
  mixer.addParticipant(2);
  mixer.addParticipant(3);

  mixer.pushAudio(1, makeConstBuffer(6000, kFrameSamples));
  mixer.pushAudio(2, makeConstBuffer(4000, kFrameSamples));
  mixer.pushAudio(3, makeConstBuffer(2000, kFrameSamples));

  // Participant 1: 4000 + 2000 = 6000 (no division)
  auto mix = mixer.mixFor(1);
  CHECK(readSamples(mix)[0] == 6000);
}

TEST_CASE("AudioMixer: AgcNormalized maintains consistent level", "[dsp][mixer][algorithm]")
{
  MixParams params;
  params.targetSampleRate = kRate;
  params.algorithm = MixAlgorithm::AgcNormalized;
  params.agcTargetLevel = 0.7f;
  params.agcWindowFrames = 5;

  // Test with 2 participants at low level.
  AudioMixer mixer2(params);
  mixer2.addParticipant(1);
  mixer2.addParticipant(2);

  // Feed several frames so AGC stabilizes.
  for (int frame = 0; frame < 10; ++frame)
  {
    mixer2.pushAudio(1, makeConstBuffer(1000, kFrameSamples));
    mixer2.pushAudio(2, makeConstBuffer(1000, kFrameSamples));
    mixer2.mixFor(1);
    mixer2.clearBuffers();
  }

  // After stabilization, output should be boosted toward target.
  mixer2.pushAudio(1, makeConstBuffer(1000, kFrameSamples));
  mixer2.pushAudio(2, makeConstBuffer(1000, kFrameSamples));
  auto mix = mixer2.mixFor(1);
  REQUIRE(mix != nullptr);
  auto samples = readSamples(mix);
  auto rms = computeRms(samples);

  // AGC should have boosted the signal significantly above input level (1000).
  CHECK(rms > 2000.0);

  // Test with 8 participants at high level — AGC should tame it.
  AudioMixer mixer8(params);
  for (std::uint32_t i = 1; i <= 8; ++i)
  {
    mixer8.addParticipant(i);
  }

  for (int frame = 0; frame < 10; ++frame)
  {
    for (std::uint32_t i = 1; i <= 8; ++i)
    {
      mixer8.pushAudio(i, makeConstBuffer(20000, kFrameSamples));
    }
    mixer8.mixFor(1);
    mixer8.clearBuffers();
  }

  for (std::uint32_t i = 1; i <= 8; ++i)
  {
    mixer8.pushAudio(i, makeConstBuffer(20000, kFrameSamples));
  }
  auto mix8 = mixer8.mixFor(1);
  REQUIRE(mix8 != nullptr);
  auto samples8 = readSamples(mix8);
  auto rms8 = computeRms(samples8);

  // AGC target = 0.7 * 32767 ≈ 22937. RMS should be in that ballpark.
  CHECK(rms8 > 15000.0);
  CHECK(rms8 < 30000.0);
}

TEST_CASE("AudioMixer: resampling from different input rate", "[dsp][mixer]")
{
  MixParams params;
  params.targetSampleRate = kRate;
  params.channels = 1;
  params.algorithm = MixAlgorithm::SaturatingAdd;

  AudioMixer mixer(params);
  mixer.addParticipant(1); // at target rate (16kHz)
  mixer.addParticipant(2, 8000); // at 8kHz — needs resampling

  // 20ms at 16kHz = 320 samples, 20ms at 8kHz = 160 samples.
  auto buf16k = makeConstBuffer(5000, 320);
  auto buf8k = makeConstBuffer(5000, 160);

  mixer.pushAudio(1, buf16k);
  mixer.pushAudio(2, buf8k);

  // Participant 1 should hear resampled participant 2.
  auto mix = mixer.mixFor(1);
  REQUIRE(mix != nullptr);
  auto samples = readSamples(mix);

  // Resampled 8kHz→16kHz constant should preserve the value approximately.
  // Allow some tolerance for resampler filter ringing.
  double rms = computeRms(samples);
  CHECK(rms > 4000.0);
  CHECK(rms < 6000.0);
}

TEST_CASE("AudioMixer: clearBuffers resets state", "[dsp][mixer]")
{
  MixParams params;
  params.targetSampleRate = kRate;
  AudioMixer mixer(params);
  mixer.addParticipant(1);
  mixer.addParticipant(2);

  mixer.pushAudio(1, makeConstBuffer(1000, kFrameSamples));
  mixer.pushAudio(2, makeConstBuffer(2000, kFrameSamples));
  CHECK(mixer.hasAudio(1));
  CHECK(mixer.hasAudio(2));

  mixer.clearBuffers();
  CHECK_FALSE(mixer.hasAudio(1));
  CHECK_FALSE(mixer.hasAudio(2));

  // After clear, mixFor returns nullptr (no data).
  CHECK(mixer.mixFor(1) == nullptr);
}

// =========================================================================
// Phase 3: Clock drift compensation
// =========================================================================

TEST_CASE("AudioMixer: driftPpm returns 0 without clocks", "[dsp][mixer][drift]")
{
  MixParams params;
  params.targetSampleRate = kRate;
  AudioMixer mixer(params);
  mixer.addParticipant(1);
  CHECK(mixer.driftPpm(1) == 0.0);
  CHECK(mixer.driftPpm(999) == 0.0);
}

TEST_CASE("AudioMixer: setParticipantClock enables drift detection", "[dsp][mixer][drift]")
{
  MixParams params;
  params.targetSampleRate = kRate;
  AudioMixer mixer(params);
  mixer.addParticipant(1);

  // Both clocks at same rate → drift should be ~0.
  mixer.setParticipantClock(1, std::make_unique<MediaClock>(kRate));

  // Need some time to elapse for meaningful measurement.
  // With both clocks at same rate from same timepoint, drift ≈ 0.
  double drift = mixer.driftPpm(1);
  CHECK(std::abs(drift) < 100.0); // Very close to 0.
}

TEST_CASE("AudioMixer: drift compensation does not crash on normal audio", "[dsp][mixer][drift]")
{
  MixParams params;
  params.targetSampleRate = kRate;
  params.driftThresholdPpm = 50.0;

  AudioMixer mixer(params);
  mixer.addParticipant(1);
  mixer.addParticipant(2);
  mixer.setParticipantClock(1, std::make_unique<MediaClock>(kRate));
  mixer.setParticipantClock(2, std::make_unique<MediaClock>(kRate));

  // Process multiple frames — drift comp should run without crashing.
  for (int i = 0; i < 100; ++i)
  {
    mixer.pushAudio(1, makeConstBuffer(1000, kFrameSamples));
    mixer.pushAudio(2, makeConstBuffer(2000, kFrameSamples));
    auto mix = mixer.mixFor(1);
    mixer.clearBuffers();
  }
  // If we get here without crash, drift compensation is safe.
  CHECK(true);
}

// =========================================================================
// Phase 4: VAD and dominant speaker
// =========================================================================

TEST_CASE("AudioMixer: VAD classifies loud vs silent", "[dsp][mixer][vad]")
{
  MixParams params;
  params.targetSampleRate = kRate;
  params.enableVad = true;
  params.vadSilenceThreshold = 100.0f;
  params.algorithm = MixAlgorithm::SaturatingAdd;

  AudioMixer mixer(params);
  mixer.addParticipant(1);
  mixer.addParticipant(2);
  mixer.addParticipant(3);

  // Participant 1: loud (speaking).
  mixer.pushAudio(1, makeSineBuffer(440.0f, kRate, kFrameSamples, 10000));
  // Participant 2: silence (not speaking).
  mixer.pushAudio(2, makeConstBuffer(0, kFrameSamples));
  // Participant 3: loud.
  mixer.pushAudio(3, makeSineBuffer(880.0f, kRate, kFrameSamples, 10000));

  CHECK(mixer.isSpeaking(1));
  CHECK_FALSE(mixer.isSpeaking(2));
  CHECK(mixer.isSpeaking(3));
}

TEST_CASE("AudioMixer: silent participant excluded from mix with VAD", "[dsp][mixer][vad]")
{
  MixParams params;
  params.targetSampleRate = kRate;
  params.enableVad = true;
  params.vadSilenceThreshold = 100.0f;
  params.algorithm = MixAlgorithm::SaturatingAdd;

  AudioMixer mixer(params);
  mixer.addParticipant(1);
  mixer.addParticipant(2);
  mixer.addParticipant(3);

  mixer.pushAudio(1, makeConstBuffer(5000, kFrameSamples));
  mixer.pushAudio(2, makeConstBuffer(0, kFrameSamples)); // silent
  mixer.pushAudio(3, makeConstBuffer(3000, kFrameSamples));

  // Participant 1 should hear only participant 3 (P2 silent, excluded).
  auto mix = mixer.mixFor(1);
  REQUIRE(mix != nullptr);
  CHECK(readSamples(mix)[0] == 3000);
}

TEST_CASE("AudioMixer: dominant speaker detection", "[dsp][mixer][vad]")
{
  MixParams params;
  params.targetSampleRate = kRate;
  params.enableVad = true;
  params.vadSilenceThreshold = 50.0f;

  AudioMixer mixer(params);
  mixer.addParticipant(1);
  mixer.addParticipant(2);
  mixer.addParticipant(3);

  // Participant 2 is the loudest.
  mixer.pushAudio(1, makeConstBuffer(1000, kFrameSamples));
  mixer.pushAudio(2, makeConstBuffer(5000, kFrameSamples));
  mixer.pushAudio(3, makeConstBuffer(3000, kFrameSamples));

  mixer.mixFor(1); // triggers updateDominantSpeaker
  CHECK(mixer.dominantSpeaker() == 2);
}

TEST_CASE("AudioMixer: dominant speaker switches", "[dsp][mixer][vad]")
{
  MixParams params;
  params.targetSampleRate = kRate;
  params.enableVad = true;
  params.vadSilenceThreshold = 50.0f;

  AudioMixer mixer(params);
  mixer.addParticipant(1);
  mixer.addParticipant(2);

  // Round 1: P1 is louder.
  mixer.pushAudio(1, makeConstBuffer(10000, kFrameSamples));
  mixer.pushAudio(2, makeConstBuffer(1000, kFrameSamples));
  mixer.mixFor(1);
  CHECK(mixer.dominantSpeaker() == 1);

  // Round 2: P2 becomes louder.
  mixer.clearBuffers();
  mixer.pushAudio(1, makeConstBuffer(1000, kFrameSamples));
  mixer.pushAudio(2, makeConstBuffer(10000, kFrameSamples));
  mixer.mixFor(1);
  CHECK(mixer.dominantSpeaker() == 2);
}

TEST_CASE("AudioMixer: maxActiveSpeakers limits mix sources", "[dsp][mixer][vad]")
{
  MixParams params;
  params.targetSampleRate = kRate;
  params.enableVad = true;
  params.vadSilenceThreshold = 50.0f;
  params.maxActiveSpeakers = 2;
  params.algorithm = MixAlgorithm::SaturatingAdd;

  AudioMixer mixer(params);
  for (std::uint32_t i = 1; i <= 5; ++i)
  {
    mixer.addParticipant(i);
  }

  // P1=1000, P2=5000, P3=3000, P4=4000, P5=2000
  // Top 2 loudest: P2(5000), P4(4000)
  mixer.pushAudio(1, makeConstBuffer(1000, kFrameSamples));
  mixer.pushAudio(2, makeConstBuffer(5000, kFrameSamples));
  mixer.pushAudio(3, makeConstBuffer(3000, kFrameSamples));
  mixer.pushAudio(4, makeConstBuffer(4000, kFrameSamples));
  mixer.pushAudio(5, makeConstBuffer(2000, kFrameSamples));

  // P1 should hear top 2 (excluding self): P2 + P4 = 9000
  auto mix = mixer.mixFor(1);
  REQUIRE(mix != nullptr);
  CHECK(readSamples(mix)[0] == 9000);

  // P2 should hear top 2 globally (excluding self from the mix output,
  // but global ranking includes P2): top 2 globally are P2(5000) and
  // P4(4000). Since P2 is excluded from its own mix, only P4 passes.
  auto mix2 = mixer.mixFor(2);
  REQUIRE(mix2 != nullptr);
  CHECK(readSamples(mix2)[0] == 4000);
}

TEST_CASE("AudioMixer: VAD callback fires on transitions", "[dsp][mixer][vad]")
{
  MixParams params;
  params.targetSampleRate = kRate;
  params.enableVad = true;
  params.vadSilenceThreshold = 100.0f;

  AudioMixer mixer(params);
  mixer.addParticipant(1);
  mixer.addParticipant(2);

  std::vector<std::pair<std::uint32_t, bool>> transitions;
  mixer.setVadCallback([&](std::uint32_t id, bool speaking) {
    transitions.push_back({id, speaking});
  });

  // P1 speaking, P2 silent.
  mixer.pushAudio(1, makeConstBuffer(5000, kFrameSamples));
  mixer.pushAudio(2, makeConstBuffer(0, kFrameSamples));
  // First push → transitions from default(false) to speaking/silent.
  // P1: false→true (speaking), P2: stays false (no transition)
  CHECK(transitions.size() == 1);
  CHECK(transitions[0] == std::make_pair(std::uint32_t(1), true));

  transitions.clear();
  mixer.clearBuffers();

  // P1 goes silent, P2 speaks.
  mixer.pushAudio(1, makeConstBuffer(0, kFrameSamples));
  mixer.pushAudio(2, makeConstBuffer(5000, kFrameSamples));
  // P1: true→false, P2: false→true
  CHECK(transitions.size() == 2);
}

TEST_CASE("AudioMixer: VAD disabled means all participants included", "[dsp][mixer][vad]")
{
  MixParams params;
  params.targetSampleRate = kRate;
  params.enableVad = false; // VAD off
  params.algorithm = MixAlgorithm::SaturatingAdd;

  AudioMixer mixer(params);
  mixer.addParticipant(1);
  mixer.addParticipant(2);
  mixer.addParticipant(3);

  // Even silent participant is included.
  mixer.pushAudio(1, makeConstBuffer(5000, kFrameSamples));
  mixer.pushAudio(2, makeConstBuffer(0, kFrameSamples)); // silent
  mixer.pushAudio(3, makeConstBuffer(3000, kFrameSamples));

  // P1 hears P2(0) + P3(3000) = 3000
  auto mix = mixer.mixFor(1);
  REQUIRE(mix != nullptr);
  CHECK(readSamples(mix)[0] == 3000);
}
