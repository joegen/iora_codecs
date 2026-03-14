#include <catch2/catch_test_macros.hpp>

#include "iora/codecs/dsp/wav_reader.hpp"
#include "iora/codecs/dsp/wav_writer.hpp"
#include "iora/codecs/pipeline/media_pipeline.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

using namespace iora::codecs;

namespace {

std::string tempPath(const std::string& name)
{
  return "/tmp/iora_test_" + name + ".wav";
}

std::shared_ptr<MediaBuffer> makeS16Buffer(
  const std::vector<std::int16_t>& samples)
{
  std::size_t bytes = samples.size() * sizeof(std::int16_t);
  auto buf = MediaBuffer::create(bytes);
  std::memcpy(buf->data(), samples.data(), bytes);
  buf->setSize(bytes);
  return buf;
}

std::vector<std::int16_t> readS16(const MediaBuffer& buf)
{
  std::size_t count = buf.size() / sizeof(std::int16_t);
  std::vector<std::int16_t> out(count);
  std::memcpy(out.data(), buf.data(), count * sizeof(std::int16_t));
  return out;
}

std::vector<std::int16_t> makeSine(
  std::uint32_t sampleRate, float freqHz, float amplitude, std::size_t sampleCount)
{
  std::vector<std::int16_t> out(sampleCount);
  for (std::size_t i = 0; i < sampleCount; ++i)
  {
    static constexpr double kPi = 3.14159265358979323846;
    double phase = 2.0 * kPi * freqHz * static_cast<double>(i) / sampleRate;
    out[i] = static_cast<std::int16_t>(amplitude * 32767.0 * std::sin(phase));
  }
  return out;
}

class CaptureHandler : public IMediaHandler
{
public:
  void incoming(std::shared_ptr<MediaBuffer> buffer) override
  {
    buffers.push_back(std::move(buffer));
  }
  void outgoing(std::shared_ptr<MediaBuffer> buffer) override
  {
    outBuffers.push_back(std::move(buffer));
  }
  std::vector<std::shared_ptr<MediaBuffer>> buffers;
  std::vector<std::shared_ptr<MediaBuffer>> outBuffers;
};

} // namespace

// =============================================================================
// WavWriter tests
// =============================================================================

TEST_CASE("WavWriter — write and read back S16 mono 8kHz", "[dsp][wav]")
{
  auto path = tempPath("mono_8k");
  auto samples = makeSine(8000, 440.0f, 0.5f, 800);

  {
    WavWriter writer(WavParams{8000, 1, 16});
    REQUIRE(writer.open(path));
    REQUIRE(writer.write(samples.data(), samples.size()));
    writer.close();
  }

  WavReader reader;
  REQUIRE(reader.open(path));
  CHECK(reader.info().sampleRate == 8000);
  CHECK(reader.info().channels == 1);
  CHECK(reader.info().bitsPerSample == 16);
  CHECK(reader.info().totalSamples == 800);

  auto buf = reader.readAll();
  REQUIRE(buf != nullptr);
  auto result = readS16(*buf);
  REQUIRE(result.size() == samples.size());
  CHECK(result == samples);

  std::remove(path.c_str());
}

TEST_CASE("WavWriter — write and read back S16 mono 16kHz", "[dsp][wav]")
{
  auto path = tempPath("mono_16k");
  auto samples = makeSine(16000, 440.0f, 0.5f, 1600);

  {
    WavWriter writer(WavParams{16000, 1, 16});
    REQUIRE(writer.open(path));
    REQUIRE(writer.write(samples.data(), samples.size()));
    writer.close();
  }

  WavReader reader;
  REQUIRE(reader.open(path));
  CHECK(reader.info().sampleRate == 16000);
  CHECK(reader.info().totalSamples == 1600);

  auto buf = reader.readAll();
  REQUIRE(buf != nullptr);
  auto result = readS16(*buf);
  CHECK(result == samples);

  std::remove(path.c_str());
}

TEST_CASE("WavWriter — write and read back S16 stereo 48kHz", "[dsp][wav]")
{
  auto path = tempPath("stereo_48k");
  // Stereo: L and R interleaved, 4800 frames = 9600 samples total
  std::vector<std::int16_t> samples(9600);
  for (std::size_t i = 0; i < 4800; ++i)
  {
    static constexpr double kPi = 3.14159265358979323846;
    double phase = 2.0 * kPi * 440.0 * static_cast<double>(i) / 48000.0;
    auto s = static_cast<std::int16_t>(0.5 * 32767.0 * std::sin(phase));
    samples[i * 2] = s;       // left
    samples[i * 2 + 1] = s;   // right
  }

  {
    WavWriter writer(WavParams{48000, 2, 16});
    REQUIRE(writer.open(path));
    REQUIRE(writer.write(samples.data(), samples.size()));
    writer.close();
  }

  WavReader reader;
  REQUIRE(reader.open(path));
  CHECK(reader.info().sampleRate == 48000);
  CHECK(reader.info().channels == 2);
  // totalSamples counts frames (not individual channel samples)
  CHECK(reader.info().totalSamples == 4800);

  auto buf = reader.readAll();
  REQUIRE(buf != nullptr);
  auto result = readS16(*buf);
  CHECK(result == samples);

  std::remove(path.c_str());
}

TEST_CASE("WavWriter — streaming multi-write produces correct total size", "[dsp][wav]")
{
  auto path = tempPath("multiwrite");
  auto chunk1 = makeSine(8000, 440.0f, 0.5f, 400);
  auto chunk2 = makeSine(8000, 880.0f, 0.3f, 400);

  {
    WavWriter writer(WavParams{8000, 1, 16});
    REQUIRE(writer.open(path));
    REQUIRE(writer.write(chunk1.data(), chunk1.size()));
    REQUIRE(writer.write(chunk2.data(), chunk2.size()));
    CHECK(writer.samplesWritten() == 800);
    CHECK(writer.bytesWritten() == 1600);
    writer.close();
  }

  WavReader reader;
  REQUIRE(reader.open(path));
  CHECK(reader.info().totalSamples == 800);

  std::remove(path.c_str());
}

TEST_CASE("WavWriter — header has correct RIFF/WAVE/fmt/data chunk IDs", "[dsp][wav]")
{
  auto path = tempPath("header_ids");
  std::vector<std::int16_t> samples = {100, 200, 300};

  {
    WavWriter writer(WavParams{8000, 1, 16});
    REQUIRE(writer.open(path));
    writer.write(samples.data(), samples.size());
    writer.close();
  }

  std::ifstream f(path, std::ios::binary);
  REQUIRE(f.is_open());

  char riff[4], wave[4], fmt[4], data[4];
  f.read(riff, 4);
  f.seekg(8);
  f.read(wave, 4);
  f.seekg(12);
  f.read(fmt, 4);
  f.seekg(36);
  f.read(data, 4);

  CHECK(std::memcmp(riff, "RIFF", 4) == 0);
  CHECK(std::memcmp(wave, "WAVE", 4) == 0);
  CHECK(std::memcmp(fmt, "fmt ", 4) == 0);
  CHECK(std::memcmp(data, "data", 4) == 0);

  std::remove(path.c_str());
}

TEST_CASE("WavWriter — header fields match WavParams", "[dsp][wav]")
{
  auto path = tempPath("header_fields");
  std::vector<std::int16_t> samples = {100};

  {
    WavWriter writer(WavParams{16000, 1, 16});
    REQUIRE(writer.open(path));
    writer.write(samples.data(), samples.size());
    writer.close();
  }

  WavReader reader;
  REQUIRE(reader.open(path));
  CHECK(reader.info().sampleRate == 16000);
  CHECK(reader.info().channels == 1);
  CHECK(reader.info().bitsPerSample == 16);

  std::remove(path.c_str());
}

TEST_CASE("WavWriter — close updates file size and data size in header", "[dsp][wav]")
{
  auto path = tempPath("close_sizes");
  std::vector<std::int16_t> samples = {100, 200, 300, 400, 500};

  {
    WavWriter writer(WavParams{8000, 1, 16});
    REQUIRE(writer.open(path));
    writer.write(samples.data(), samples.size());
    writer.close();
  }

  // Read raw header to verify sizes
  std::ifstream f(path, std::ios::binary);
  REQUIRE(f.is_open());

  // ChunkSize at offset 4
  f.seekg(4);
  std::uint8_t buf[4];
  f.read(reinterpret_cast<char*>(buf), 4);
  std::uint32_t chunkSize = buf[0] | (buf[1] << 8) | (buf[2] << 16) | (buf[3] << 24);
  // ChunkSize = 36 + dataSize = 36 + 10
  CHECK(chunkSize == 46);

  // Subchunk2Size at offset 40
  f.seekg(40);
  f.read(reinterpret_cast<char*>(buf), 4);
  std::uint32_t dataSize = buf[0] | (buf[1] << 8) | (buf[2] << 16) | (buf[3] << 24);
  CHECK(dataSize == 10);  // 5 samples * 2 bytes

  std::remove(path.c_str());
}

TEST_CASE("WavWriter — durationMs calculation", "[dsp][wav]")
{
  WavWriter writer(WavParams{8000, 1, 16});
  auto path = tempPath("duration");
  REQUIRE(writer.open(path));

  // 8000 samples at 8kHz = 1000ms
  auto samples = makeSine(8000, 440.0f, 0.5f, 8000);
  writer.write(samples.data(), samples.size());
  CHECK(writer.durationMs() == 1000);

  // 4000 more samples = 1500ms total
  auto more = makeSine(8000, 440.0f, 0.5f, 4000);
  writer.write(more.data(), more.size());
  CHECK(writer.durationMs() == 1500);

  writer.close();
  std::remove(path.c_str());
}

TEST_CASE("WavWriter — samplesWritten and bytesWritten counters", "[dsp][wav]")
{
  WavWriter writer(WavParams{8000, 1, 16});
  auto path = tempPath("counters");
  REQUIRE(writer.open(path));

  CHECK(writer.samplesWritten() == 0);
  CHECK(writer.bytesWritten() == 0);

  std::vector<std::int16_t> samples = {100, 200, 300};
  writer.write(samples.data(), samples.size());

  CHECK(writer.samplesWritten() == 3);
  CHECK(writer.bytesWritten() == 6);

  writer.close();
  std::remove(path.c_str());
}

TEST_CASE("WavWriter — rejects odd-byte MediaBuffer", "[dsp][wav]")
{
  WavWriter writer(WavParams{8000, 1, 16});
  auto path = tempPath("odd_byte");
  REQUIRE(writer.open(path));

  auto buf = MediaBuffer::create(5);
  std::uint8_t data[5] = {0x01, 0x02, 0x03, 0x04, 0x05};
  std::memcpy(buf->data(), data, 5);
  buf->setSize(5);

  CHECK(!writer.write(*buf));
  CHECK(writer.bytesWritten() == 0);

  writer.close();
  std::remove(path.c_str());
}

TEST_CASE("WavWriter — open fails on invalid path", "[dsp][wav]")
{
  WavWriter writer;
  CHECK(!writer.open("/nonexistent/directory/file.wav"));
  CHECK(!writer.isOpen());
}

TEST_CASE("WavWriter — write after close returns false", "[dsp][wav]")
{
  WavWriter writer(WavParams{8000, 1, 16});
  auto path = tempPath("write_after_close");
  REQUIRE(writer.open(path));
  writer.close();

  std::vector<std::int16_t> samples = {100};
  CHECK(!writer.write(samples.data(), samples.size()));

  std::remove(path.c_str());
}

TEST_CASE("WavWriter — double close is safe", "[dsp][wav]")
{
  WavWriter writer(WavParams{8000, 1, 16});
  auto path = tempPath("double_close");
  REQUIRE(writer.open(path));
  writer.close();
  writer.close();  // should not crash
  CHECK(!writer.isOpen());

  std::remove(path.c_str());
}

TEST_CASE("WavWriter — rejects zero sample rate", "[dsp][wav]")
{
  CHECK_THROWS_AS(WavWriter(WavParams{0, 1, 16}), std::invalid_argument);
}

TEST_CASE("WavWriter — rejects zero channels", "[dsp][wav]")
{
  CHECK_THROWS_AS(WavWriter(WavParams{8000, 0, 16}), std::invalid_argument);
}

TEST_CASE("WavWriter — rejects unsupported bitsPerSample", "[dsp][wav]")
{
  CHECK_THROWS_AS(WavWriter(WavParams{8000, 1, 32}), std::invalid_argument);
  CHECK_THROWS_AS(WavWriter(WavParams{8000, 1, 0}), std::invalid_argument);
  CHECK_THROWS_AS(WavWriter(WavParams{8000, 1, 4}), std::invalid_argument);
  CHECK_THROWS_AS(WavWriter(WavParams{8000, 1, 8}), std::invalid_argument);
  CHECK_THROWS_AS(WavWriter(WavParams{8000, 1, 24}), std::invalid_argument);
}

// =============================================================================
// WavRecorderHandler tests
// =============================================================================

TEST_CASE("WavRecorderHandler — incoming audio recorded to file", "[dsp][wav]")
{
  auto path = tempPath("rec_incoming");
  auto samples = makeSine(8000, 440.0f, 0.5f, 800);

  {
    auto handler = std::make_shared<WavRecorderHandler>(
      WavParams{8000, 1, 16}, path, RecordDirection::INCOMING);
    auto capture = std::make_shared<CaptureHandler>();
    handler->addToChain(capture);

    handler->incoming(makeS16Buffer(samples));
    handler->stopRecording();
  }

  WavReader reader;
  REQUIRE(reader.open(path));
  CHECK(reader.info().totalSamples == 800);
  auto buf = reader.readAll();
  REQUIRE(buf != nullptr);
  auto result = readS16(*buf);
  CHECK(result == samples);

  std::remove(path.c_str());
}

TEST_CASE("WavRecorderHandler — outgoing audio recorded to file", "[dsp][wav]")
{
  auto path = tempPath("rec_outgoing");
  auto samples = makeSine(8000, 440.0f, 0.5f, 800);

  {
    auto handler = std::make_shared<WavRecorderHandler>(
      WavParams{8000, 1, 16}, path, RecordDirection::OUTGOING);
    auto capture = std::make_shared<CaptureHandler>();
    handler->addToChain(capture);

    handler->outgoing(makeS16Buffer(samples));
    handler->stopRecording();
  }

  WavReader reader;
  REQUIRE(reader.open(path));
  CHECK(reader.info().totalSamples == 800);

  std::remove(path.c_str());
}

TEST_CASE("WavRecorderHandler — BOTH direction records both", "[dsp][wav]")
{
  auto path = tempPath("rec_both");
  auto inSamples = makeSine(8000, 440.0f, 0.5f, 400);
  auto outSamples = makeSine(8000, 880.0f, 0.3f, 400);

  {
    auto handler = std::make_shared<WavRecorderHandler>(
      WavParams{8000, 1, 16}, path, RecordDirection::BOTH);
    auto capture = std::make_shared<CaptureHandler>();
    handler->addToChain(capture);

    handler->incoming(makeS16Buffer(inSamples));
    handler->outgoing(makeS16Buffer(outSamples));
    handler->stopRecording();
  }

  WavReader reader;
  REQUIRE(reader.open(path));
  // 400 + 400 = 800 samples total (interleaved in time order)
  CHECK(reader.info().totalSamples == 800);

  std::remove(path.c_str());
}

TEST_CASE("WavRecorderHandler — INCOMING direction ignores outgoing", "[dsp][wav]")
{
  auto path = tempPath("rec_in_only");
  auto inSamples = makeSine(8000, 440.0f, 0.5f, 400);
  auto outSamples = makeSine(8000, 880.0f, 0.3f, 400);

  {
    auto handler = std::make_shared<WavRecorderHandler>(
      WavParams{8000, 1, 16}, path, RecordDirection::INCOMING);
    auto capture = std::make_shared<CaptureHandler>();
    handler->addToChain(capture);

    handler->incoming(makeS16Buffer(inSamples));
    handler->outgoing(makeS16Buffer(outSamples));
    handler->stopRecording();
  }

  WavReader reader;
  REQUIRE(reader.open(path));
  CHECK(reader.info().totalSamples == 400);  // only incoming

  std::remove(path.c_str());
}

TEST_CASE("WavRecorderHandler — OUTGOING direction ignores incoming", "[dsp][wav]")
{
  auto path = tempPath("rec_out_only");
  auto inSamples = makeSine(8000, 440.0f, 0.5f, 400);
  auto outSamples = makeSine(8000, 880.0f, 0.3f, 400);

  {
    auto handler = std::make_shared<WavRecorderHandler>(
      WavParams{8000, 1, 16}, path, RecordDirection::OUTGOING);
    auto capture = std::make_shared<CaptureHandler>();
    handler->addToChain(capture);

    handler->incoming(makeS16Buffer(inSamples));
    handler->outgoing(makeS16Buffer(outSamples));
    handler->stopRecording();
  }

  WavReader reader;
  REQUIRE(reader.open(path));
  CHECK(reader.info().totalSamples == 400);  // only outgoing

  std::remove(path.c_str());
}

TEST_CASE("WavRecorderHandler — buffers forwarded unchanged (passive tap)", "[dsp][wav]")
{
  auto path = tempPath("rec_passthrough");
  auto samples = makeSine(8000, 440.0f, 0.5f, 800);
  auto originalBuf = makeS16Buffer(samples);

  auto handler = std::make_shared<WavRecorderHandler>(
    WavParams{8000, 1, 16}, path, RecordDirection::INCOMING);
  auto capture = std::make_shared<CaptureHandler>();
  handler->addToChain(capture);

  handler->incoming(originalBuf);

  REQUIRE(capture->buffers.size() == 1);
  // Buffer forwarded is the same object
  CHECK(capture->buffers[0] == originalBuf);
  // Data unchanged
  auto result = readS16(*capture->buffers[0]);
  CHECK(result == samples);

  handler->stopRecording();
  std::remove(path.c_str());
}

TEST_CASE("WavRecorderHandler — startRecording/stopRecording lifecycle", "[dsp][wav]")
{
  auto path1 = tempPath("rec_lc1");
  auto path2 = tempPath("rec_lc2");
  auto samples1 = makeSine(8000, 440.0f, 0.5f, 400);
  auto samples2 = makeSine(8000, 880.0f, 0.3f, 600);

  auto handler = std::make_shared<WavRecorderHandler>(
    WavParams{8000, 1, 16}, RecordDirection::INCOMING);

  // Not recording initially (no file path in constructor)
  CHECK(!handler->isRecording());

  // Start first recording
  REQUIRE(handler->startRecording(path1));
  CHECK(handler->isRecording());
  handler->incoming(makeS16Buffer(samples1));
  handler->stopRecording();
  CHECK(!handler->isRecording());

  // Start second recording
  REQUIRE(handler->startRecording(path2));
  CHECK(handler->isRecording());
  handler->incoming(makeS16Buffer(samples2));
  handler->stopRecording();

  // Verify first file
  WavReader reader1;
  REQUIRE(reader1.open(path1));
  CHECK(reader1.info().totalSamples == 400);

  // Verify second file
  WavReader reader2;
  REQUIRE(reader2.open(path2));
  CHECK(reader2.info().totalSamples == 600);

  std::remove(path1.c_str());
  std::remove(path2.c_str());
}

TEST_CASE("WavRecorderHandler — stopRecording produces valid WAV", "[dsp][wav]")
{
  auto path = tempPath("rec_valid");
  auto samples = makeSine(8000, 440.0f, 0.5f, 800);

  {
    auto handler = std::make_shared<WavRecorderHandler>(
      WavParams{8000, 1, 16}, path, RecordDirection::INCOMING);
    handler->incoming(makeS16Buffer(samples));
    handler->stopRecording();
  }

  // Verify the WAV is fully valid via WavReader
  WavReader reader;
  REQUIRE(reader.open(path));
  CHECK(reader.info().sampleRate == 8000);
  CHECK(reader.info().channels == 1);
  CHECK(reader.info().bitsPerSample == 16);
  CHECK(reader.info().totalSamples == 800);
  CHECK(reader.info().durationMs == 100);

  auto buf = reader.readAll();
  REQUIRE(buf != nullptr);
  auto result = readS16(*buf);
  CHECK(result == samples);

  std::remove(path.c_str());
}

TEST_CASE("WavRecorderHandler — in MediaPipeline as named stage", "[dsp][wav]")
{
  auto path = tempPath("rec_pipeline");
  auto samples = makeSine(8000, 440.0f, 0.5f, 800);

  MediaPipeline pipeline;
  auto source = std::make_shared<IMediaHandler>();
  auto recorder = std::make_shared<WavRecorderHandler>(
    WavParams{8000, 1, 16}, path, RecordDirection::INCOMING);
  auto sink = std::make_shared<CaptureHandler>();

  REQUIRE(pipeline.addStage("source", source));
  REQUIRE(pipeline.addStage("recorder", recorder));
  REQUIRE(pipeline.addStage("sink", sink));
  REQUIRE(pipeline.connectStages("source", "recorder"));
  REQUIRE(pipeline.connectStages("recorder", "sink"));

  auto result = pipeline.start();
  REQUIRE(result.success);

  pipeline.incoming(makeS16Buffer(samples));

  // Verify audio passed through to sink
  REQUIRE(sink->buffers.size() == 1);
  auto sinkSamples = readS16(*sink->buffers[0]);
  CHECK(sinkSamples == samples);

  // Stop recording and verify file
  recorder->stopRecording();

  WavReader reader;
  REQUIRE(reader.open(path));
  CHECK(reader.info().totalSamples == 800);

  std::remove(path.c_str());
}

TEST_CASE("WavRecorderHandler — durationMs tracks recording length", "[dsp][wav]")
{
  auto path = tempPath("rec_duration");
  auto handler = std::make_shared<WavRecorderHandler>(
    WavParams{8000, 1, 16}, path, RecordDirection::INCOMING);

  CHECK(handler->durationMs() == 0);

  // 8000 samples at 8kHz = 1000ms
  auto samples = makeSine(8000, 440.0f, 0.5f, 8000);
  handler->incoming(makeS16Buffer(samples));
  CHECK(handler->durationMs() == 1000);

  handler->stopRecording();
  std::remove(path.c_str());
}

TEST_CASE("WavRecorderHandler — write after stopRecording is no-op (buffers still forwarded)", "[dsp][wav]")
{
  auto path = tempPath("rec_after_stop");
  auto samples = makeSine(8000, 440.0f, 0.5f, 400);

  auto handler = std::make_shared<WavRecorderHandler>(
    WavParams{8000, 1, 16}, path, RecordDirection::INCOMING);
  auto capture = std::make_shared<CaptureHandler>();
  handler->addToChain(capture);

  handler->incoming(makeS16Buffer(samples));
  handler->stopRecording();

  // Send more audio after stop — should NOT record but SHOULD forward
  handler->incoming(makeS16Buffer(samples));
  CHECK(capture->buffers.size() == 2);  // both forwarded

  // File should only have the first batch
  WavReader reader;
  REQUIRE(reader.open(path));
  CHECK(reader.info().totalSamples == 400);

  std::remove(path.c_str());
}
