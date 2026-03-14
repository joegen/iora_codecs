#include <catch2/catch_test_macros.hpp>

#include "iora/codecs/dsp/wav_reader.hpp"
#include "iora/codecs/dsp/wav_writer.hpp"

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
  return "/tmp/iora_test_reader_" + name + ".wav";
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

/// Write a helper WAV file using WavWriter and return the path.
std::string writeTestWav(const std::string& name,
                         const std::vector<std::int16_t>& samples,
                         std::uint32_t sampleRate = 8000,
                         std::uint16_t channels = 1)
{
  auto path = tempPath(name);
  WavWriter writer(WavParams{sampleRate, channels, 16});
  writer.open(path);
  writer.write(samples.data(), samples.size());
  writer.close();
  return path;
}

/// Write raw bytes to a file.
void writeRawFile(const std::string& path, const std::vector<std::uint8_t>& data)
{
  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  f.write(reinterpret_cast<const char*>(data.data()),
          static_cast<std::streamsize>(data.size()));
}

} // namespace

TEST_CASE("WavReader — open and read WavWriter output (round-trip)", "[dsp][wav]")
{
  auto samples = makeSine(8000, 440.0f, 0.5f, 800);
  auto path = writeTestWav("roundtrip", samples);

  WavReader reader;
  REQUIRE(reader.open(path));

  auto buf = reader.readAll();
  REQUIRE(buf != nullptr);
  auto result = readS16(*buf);
  REQUIRE(result.size() == samples.size());
  CHECK(result == samples);

  std::remove(path.c_str());
}

TEST_CASE("WavReader — info() returns correct metadata", "[dsp][wav]")
{
  auto samples = makeSine(16000, 440.0f, 0.5f, 1600);
  auto path = writeTestWav("info", samples, 16000);

  WavReader reader;
  REQUIRE(reader.open(path));

  auto& info = reader.info();
  CHECK(info.sampleRate == 16000);
  CHECK(info.channels == 1);
  CHECK(info.bitsPerSample == 16);
  CHECK(info.totalSamples == 1600);
  CHECK(info.durationMs == 100);
  CHECK(info.dataOffset == 44);  // standard 44-byte header
  CHECK(info.dataSize == 3200);  // 1600 * 2 bytes

  std::remove(path.c_str());
}

TEST_CASE("WavReader — read(N) returns correct sample count", "[dsp][wav]")
{
  auto samples = makeSine(8000, 440.0f, 0.5f, 800);
  auto path = writeTestWav("readn", samples);

  WavReader reader;
  REQUIRE(reader.open(path));

  auto buf = reader.read(200);
  REQUIRE(buf != nullptr);
  CHECK(buf->size() == 200 * sizeof(std::int16_t));
  CHECK(reader.remaining() == 600);

  auto result = readS16(*buf);
  for (std::size_t i = 0; i < 200; ++i)
  {
    CHECK(result[i] == samples[i]);
  }

  std::remove(path.c_str());
}

TEST_CASE("WavReader — read(N) at EOF returns nullptr", "[dsp][wav]")
{
  auto samples = makeSine(8000, 440.0f, 0.5f, 100);
  auto path = writeTestWav("eof", samples);

  WavReader reader;
  REQUIRE(reader.open(path));

  // Read all
  auto buf1 = reader.read(100);
  REQUIRE(buf1 != nullptr);
  CHECK(reader.remaining() == 0);

  // Read again — EOF
  auto buf2 = reader.read(100);
  CHECK(buf2 == nullptr);

  std::remove(path.c_str());
}

TEST_CASE("WavReader — readAll() returns complete data", "[dsp][wav]")
{
  auto samples = makeSine(8000, 440.0f, 0.5f, 1000);
  auto path = writeTestWav("readall", samples);

  WavReader reader;
  REQUIRE(reader.open(path));

  auto buf = reader.readAll();
  REQUIRE(buf != nullptr);
  auto result = readS16(*buf);
  CHECK(result == samples);
  CHECK(reader.remaining() == 0);

  std::remove(path.c_str());
}

TEST_CASE("WavReader — seek to middle and read", "[dsp][wav]")
{
  auto samples = makeSine(8000, 440.0f, 0.5f, 1000);
  auto path = writeTestWav("seek_mid", samples);

  WavReader reader;
  REQUIRE(reader.open(path));

  // Seek to sample 500
  REQUIRE(reader.seek(500));
  CHECK(reader.remaining() == 500);

  auto buf = reader.read(500);
  REQUIRE(buf != nullptr);
  auto result = readS16(*buf);
  REQUIRE(result.size() == 500);
  for (std::size_t i = 0; i < 500; ++i)
  {
    CHECK(result[i] == samples[500 + i]);
  }

  std::remove(path.c_str());
}

TEST_CASE("WavReader — seek to beginning after partial read", "[dsp][wav]")
{
  auto samples = makeSine(8000, 440.0f, 0.5f, 800);
  auto path = writeTestWav("seek_begin", samples);

  WavReader reader;
  REQUIRE(reader.open(path));

  // Read partial
  reader.read(300);
  CHECK(reader.remaining() == 500);

  // Seek back to start
  REQUIRE(reader.seek(0));
  CHECK(reader.remaining() == 800);

  auto buf = reader.readAll();
  REQUIRE(buf != nullptr);
  auto result = readS16(*buf);
  CHECK(result == samples);

  std::remove(path.c_str());
}

TEST_CASE("WavReader — remaining() decreases after reads", "[dsp][wav]")
{
  auto samples = makeSine(8000, 440.0f, 0.5f, 500);
  auto path = writeTestWav("remaining", samples);

  WavReader reader;
  REQUIRE(reader.open(path));
  CHECK(reader.remaining() == 500);

  reader.read(100);
  CHECK(reader.remaining() == 400);

  reader.read(200);
  CHECK(reader.remaining() == 200);

  reader.read(200);
  CHECK(reader.remaining() == 0);

  std::remove(path.c_str());
}

TEST_CASE("WavReader — rejects non-RIFF file", "[dsp][wav]")
{
  auto path = tempPath("not_riff");
  writeRawFile(path, {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                       0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F});

  WavReader reader;
  CHECK(!reader.open(path));

  std::remove(path.c_str());
}

TEST_CASE("WavReader — rejects non-WAVE format", "[dsp][wav]")
{
  auto path = tempPath("not_wave");
  // Valid RIFF header but format is "AVI " instead of "WAVE"
  std::vector<std::uint8_t> data = {
    'R', 'I', 'F', 'F',
    0x24, 0x00, 0x00, 0x00,  // chunk size
    'A', 'V', 'I', ' '       // not WAVE
  };
  writeRawFile(path, data);

  WavReader reader;
  CHECK(!reader.open(path));

  std::remove(path.c_str());
}

TEST_CASE("WavReader — rejects non-PCM AudioFormat", "[dsp][wav]")
{
  auto path = tempPath("not_pcm");
  // Build a complete WAV header with AudioFormat=3 (IEEE float)
  std::vector<std::uint8_t> data = {
    'R', 'I', 'F', 'F',
    0x24, 0x00, 0x00, 0x00,  // chunk size = 36
    'W', 'A', 'V', 'E',
    'f', 'm', 't', ' ',
    0x10, 0x00, 0x00, 0x00,  // subchunk1 size = 16
    0x03, 0x00,               // AudioFormat = 3 (IEEE float, NOT PCM)
    0x01, 0x00,               // channels = 1
    0x40, 0x1F, 0x00, 0x00,  // sample rate = 8000
    0x00, 0x7D, 0x00, 0x00,  // byte rate = 32000
    0x04, 0x00,               // block align = 4
    0x20, 0x00,               // bits per sample = 32
    'd', 'a', 't', 'a',
    0x00, 0x00, 0x00, 0x00   // data size = 0
  };
  writeRawFile(path, data);

  WavReader reader;
  CHECK(!reader.open(path));

  std::remove(path.c_str());
}

TEST_CASE("WavReader — rejects unsupported bitsPerSample", "[dsp][wav]")
{
  auto path = tempPath("bad_bps");
  // Build a WAV header with BitsPerSample=32
  std::vector<std::uint8_t> data = {
    'R', 'I', 'F', 'F',
    0x24, 0x00, 0x00, 0x00,
    'W', 'A', 'V', 'E',
    'f', 'm', 't', ' ',
    0x10, 0x00, 0x00, 0x00,
    0x01, 0x00,               // PCM
    0x01, 0x00,               // 1 channel
    0x40, 0x1F, 0x00, 0x00,  // 8000 Hz
    0x00, 0x7D, 0x00, 0x00,  // byte rate
    0x04, 0x00,               // block align
    0x20, 0x00,               // 32 bits per sample — unsupported
    'd', 'a', 't', 'a',
    0x00, 0x00, 0x00, 0x00
  };
  writeRawFile(path, data);

  WavReader reader;
  CHECK(!reader.open(path));

  std::remove(path.c_str());
}

TEST_CASE("WavReader — handles truncated WAV file", "[dsp][wav]")
{
  auto path = tempPath("truncated");

  // Write a valid WAV with 800 samples, then truncate
  auto samples = makeSine(8000, 440.0f, 0.5f, 800);
  {
    WavWriter writer(WavParams{8000, 1, 16});
    writer.open(path);
    writer.write(samples.data(), samples.size());
    writer.close();
  }

  // Truncate the file mid-data (keep header + partial data)
  {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    auto fullSize = in.tellg();
    in.seekg(0);
    std::vector<char> buf(static_cast<std::size_t>(fullSize));
    in.read(buf.data(), fullSize);
    in.close();

    // Keep header (44 bytes) + only 100 bytes of data (50 samples)
    std::size_t truncatedSize = 44 + 100;
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(buf.data(), static_cast<std::streamsize>(truncatedSize));
  }

  // Reader should still open (header claims 800 samples but only 50 exist)
  WavReader reader;
  REQUIRE(reader.open(path));
  // Header says 800 samples (from original size field)
  CHECK(reader.info().totalSamples == 800);

  // Reading all should return only what's actually in the file
  auto result = reader.read(800);
  REQUIRE(result != nullptr);
  // Should get fewer bytes than requested
  CHECK(result->size() <= 800 * sizeof(std::int16_t));
  CHECK(result->size() >= 100);  // at least the truncated data

  std::remove(path.c_str());
}

TEST_CASE("WavReader — open fails on nonexistent file", "[dsp][wav]")
{
  WavReader reader;
  CHECK(!reader.open("/nonexistent/path/to/file.wav"));
  CHECK(!reader.isOpen());
}
