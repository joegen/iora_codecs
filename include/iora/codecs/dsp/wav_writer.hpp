#pragma once

/// @file wav_writer.hpp
/// @brief WAV file writer and pipeline recording handler.

#include "iora/codecs/core/media_buffer.hpp"
#include "iora/codecs/pipeline/i_media_handler.hpp"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>

namespace iora {
namespace codecs {

/// Parameters for WAV file writing.
struct WavParams
{
  std::uint32_t sampleRate = 16000;
  std::uint16_t channels = 1;
  std::uint16_t bitsPerSample = 16;
};

/// WAV file writer — writes standard RIFF WAV format with PCM data.
/// Supports streaming writes with deferred header finalization.
class WavWriter
{
public:
  explicit WavWriter(const WavParams& params = WavParams{})
    : _params(params)
  {
    if (_params.sampleRate == 0)
    {
      throw std::invalid_argument("WavWriter: sampleRate must be > 0");
    }
    if (_params.channels == 0)
    {
      throw std::invalid_argument("WavWriter: channels must be > 0");
    }
    if (_params.bitsPerSample != 16)
    {
      throw std::invalid_argument("WavWriter: bitsPerSample must be 16 (S16 PCM only)");
    }
  }

  ~WavWriter()
  {
    close();
  }

  WavWriter(const WavWriter&) = delete;
  WavWriter& operator=(const WavWriter&) = delete;

  WavWriter(WavWriter&& other) noexcept
    : _params(other._params)
    , _file(std::move(other._file))
    , _dataBytes(other._dataBytes)
    , _open(other._open)
  {
    other._dataBytes = 0;
    other._open = false;
  }

  WavWriter& operator=(WavWriter&& other) noexcept
  {
    if (this != &other)
    {
      close();
      _params = other._params;
      _file = std::move(other._file);
      _dataBytes = other._dataBytes;
      _open = other._open;
      other._dataBytes = 0;
      other._open = false;
    }
    return *this;
  }

  bool open(const std::string& filePath)
  {
    if (_open)
    {
      close();
    }

    _file.open(filePath, std::ios::binary | std::ios::trunc);
    if (!_file.is_open())
    {
      return false;
    }

    _dataBytes = 0;
    _open = true;
    writeHeader();
    return true;
  }

  bool write(const std::int16_t* samples, std::size_t sampleCount)
  {
    if (!_open || samples == nullptr || sampleCount == 0)
    {
      return false;
    }

    std::size_t bytes = sampleCount * sizeof(std::int16_t);

    // WAV format limits data chunk to UINT32_MAX bytes
    std::uint64_t newTotal = static_cast<std::uint64_t>(_dataBytes) + bytes;
    if (newTotal > UINT32_MAX)
    {
      return false;
    }

    _file.write(reinterpret_cast<const char*>(samples), static_cast<std::streamsize>(bytes));
    if (!_file.good())
    {
      return false;
    }
    _dataBytes = static_cast<std::uint32_t>(newTotal);
    return true;
  }

  bool write(const MediaBuffer& buf)
  {
    if (buf.size() % sizeof(std::int16_t) != 0)
    {
      return false;
    }
    std::size_t sampleCount = buf.size() / sizeof(std::int16_t);
    return write(reinterpret_cast<const std::int16_t*>(buf.data()), sampleCount);
  }

  void close()
  {
    if (!_open)
    {
      return;
    }
    finalizeHeader();
    _file.close();
    _open = false;
  }

  bool isOpen() const noexcept { return _open; }

  std::uint32_t samplesWritten() const noexcept
  {
    std::uint16_t bytesPerSample = _params.bitsPerSample / 8;
    if (bytesPerSample == 0)
    {
      return 0;
    }
    return _dataBytes / (bytesPerSample * _params.channels);
  }

  std::uint32_t bytesWritten() const noexcept { return _dataBytes; }

  std::uint32_t durationMs() const noexcept
  {
    if (_params.sampleRate == 0)
    {
      return 0;
    }
    std::uint64_t samples = samplesWritten();
    return static_cast<std::uint32_t>((samples * 1000) / _params.sampleRate);
  }

  const WavParams& params() const noexcept { return _params; }

private:
  void writeHeader()
  {
    // RIFF header — 44 bytes total
    std::uint16_t bytesPerSample = _params.bitsPerSample / 8;
    std::uint32_t byteRate = _params.sampleRate * _params.channels * bytesPerSample;
    std::uint16_t blockAlign = static_cast<std::uint16_t>(_params.channels * bytesPerSample);

    // Placeholder sizes — updated on close()
    std::uint32_t chunkSize = 0xFFFFFFFF;
    std::uint32_t subchunk2Size = 0xFFFFFFFF;
    std::uint32_t subchunk1Size = 16; // PCM
    std::uint16_t audioFormat = 1;    // PCM

    // ChunkID
    _file.write("RIFF", 4);
    // ChunkSize (placeholder)
    writeU32(chunkSize);
    // Format
    _file.write("WAVE", 4);

    // Subchunk1ID
    _file.write("fmt ", 4);
    // Subchunk1Size
    writeU32(subchunk1Size);
    // AudioFormat
    writeU16(audioFormat);
    // NumChannels
    writeU16(_params.channels);
    // SampleRate
    writeU32(_params.sampleRate);
    // ByteRate
    writeU32(byteRate);
    // BlockAlign
    writeU16(blockAlign);
    // BitsPerSample
    writeU16(_params.bitsPerSample);

    // Subchunk2ID
    _file.write("data", 4);
    // Subchunk2Size (placeholder)
    writeU32(subchunk2Size);
  }

  void finalizeHeader()
  {
    if (!_file.is_open())
    {
      return;
    }

    // Seek to ChunkSize (offset 4) and write actual size
    std::uint32_t chunkSize = 36 + _dataBytes;
    _file.seekp(4);
    writeU32(chunkSize);

    // Seek to Subchunk2Size (offset 40) and write actual data size
    _file.seekp(40);
    writeU32(_dataBytes);

    // Seek back to end
    _file.seekp(0, std::ios::end);
  }

  void writeU16(std::uint16_t value)
  {
    std::uint8_t buf[2];
    buf[0] = static_cast<std::uint8_t>(value & 0xFF);
    buf[1] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
    _file.write(reinterpret_cast<const char*>(buf), 2);
  }

  void writeU32(std::uint32_t value)
  {
    std::uint8_t buf[4];
    buf[0] = static_cast<std::uint8_t>(value & 0xFF);
    buf[1] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
    buf[2] = static_cast<std::uint8_t>((value >> 16) & 0xFF);
    buf[3] = static_cast<std::uint8_t>((value >> 24) & 0xFF);
    _file.write(reinterpret_cast<const char*>(buf), 4);
  }

  WavParams _params;
  std::ofstream _file;
  std::uint32_t _dataBytes = 0;
  bool _open = false;
};

/// Direction for recording in WavRecorderHandler.
enum class RecordDirection
{
  INCOMING,
  OUTGOING,
  BOTH
};

/// Pipeline recording handler — passive tap that writes audio to a WAV file
/// and forwards buffers unchanged to the next handler.
class WavRecorderHandler : public IMediaHandler
{
public:
  WavRecorderHandler(const WavParams& params, const std::string& filePath,
                     RecordDirection direction = RecordDirection::INCOMING)
    : _params(params)
    , _direction(direction)
    , _writer(params)
  {
    _writer.open(filePath);
  }

  WavRecorderHandler(const WavParams& params,
                     RecordDirection direction = RecordDirection::INCOMING)
    : _params(params)
    , _direction(direction)
    , _writer(params)
  {
  }

  void incoming(std::shared_ptr<MediaBuffer> buffer) override
  {
    if (buffer && _writer.isOpen())
    {
      if (_direction == RecordDirection::INCOMING || _direction == RecordDirection::BOTH)
      {
        _writer.write(*buffer);
      }
    }
    forwardIncoming(std::move(buffer));
  }

  void outgoing(std::shared_ptr<MediaBuffer> buffer) override
  {
    if (buffer && _writer.isOpen())
    {
      if (_direction == RecordDirection::OUTGOING || _direction == RecordDirection::BOTH)
      {
        _writer.write(*buffer);
      }
    }
    forwardOutgoing(std::move(buffer));
  }

  bool startRecording(const std::string& filePath)
  {
    stopRecording();
    _writer = WavWriter(_params);
    return _writer.open(filePath);
  }

  void stopRecording()
  {
    _writer.close();
  }

  bool isRecording() const noexcept { return _writer.isOpen(); }

  std::uint32_t durationMs() const noexcept { return _writer.durationMs(); }

  WavWriter& writer() noexcept { return _writer; }
  const WavWriter& writer() const noexcept { return _writer; }

private:
  WavParams _params;
  RecordDirection _direction;
  WavWriter _writer;
};

} // namespace codecs
} // namespace iora
