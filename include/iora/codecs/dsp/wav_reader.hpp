#pragma once

/// @file wav_reader.hpp
/// @brief WAV file reader — parses RIFF WAV headers and reads PCM audio data.

#include "iora/codecs/core/media_buffer.hpp"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>

namespace iora {
namespace codecs {

/// Metadata parsed from a WAV file header.
struct WavFileInfo
{
  std::uint32_t sampleRate = 0;
  std::uint16_t channels = 0;
  std::uint16_t bitsPerSample = 0;
  std::uint32_t totalSamples = 0;
  std::uint32_t durationMs = 0;
  std::uint32_t dataOffset = 0;
  std::uint32_t dataSize = 0;
};

/// WAV file reader — parses RIFF WAV headers and reads PCM audio data into MediaBuffers.
class WavReader
{
public:
  WavReader() = default;

  ~WavReader()
  {
    close();
  }

  WavReader(const WavReader&) = delete;
  WavReader& operator=(const WavReader&) = delete;

  WavReader(WavReader&& other) noexcept
    : _file(std::move(other._file))
    , _info(other._info)
    , _samplesRead(other._samplesRead)
    , _open(other._open)
  {
    other._samplesRead = 0;
    other._open = false;
    other._info = WavFileInfo{};
  }

  WavReader& operator=(WavReader&& other) noexcept
  {
    if (this != &other)
    {
      close();
      _file = std::move(other._file);
      _info = other._info;
      _samplesRead = other._samplesRead;
      _open = other._open;
      other._samplesRead = 0;
      other._open = false;
      other._info = WavFileInfo{};
    }
    return *this;
  }

  bool open(const std::string& filePath)
  {
    if (_open)
    {
      close();
    }

    _file.open(filePath, std::ios::binary);
    if (!_file.is_open())
    {
      return false;
    }

    if (!parseHeader())
    {
      _file.close();
      return false;
    }

    _samplesRead = 0;
    _open = true;
    return true;
  }

  std::shared_ptr<MediaBuffer> read(std::size_t sampleCount)
  {
    if (!_open || sampleCount == 0)
    {
      return nullptr;
    }

    std::uint32_t rem = remaining();
    if (rem == 0)
    {
      return nullptr;
    }

    std::size_t toRead = (sampleCount > rem) ? rem : sampleCount;
    std::uint16_t bytesPerSample = _info.bitsPerSample / 8;
    std::size_t frameBytes = bytesPerSample * _info.channels;
    std::size_t bytes = toRead * frameBytes;

    auto buf = MediaBuffer::create(bytes);
    _file.read(reinterpret_cast<char*>(buf->data()), static_cast<std::streamsize>(bytes));
    std::size_t actualBytes = static_cast<std::size_t>(_file.gcount());
    if (actualBytes == 0)
    {
      return nullptr;
    }

    buf->setSize(actualBytes);
    std::size_t actualSamples = actualBytes / frameBytes;
    _samplesRead += static_cast<std::uint32_t>(actualSamples);
    return buf;
  }

  std::shared_ptr<MediaBuffer> readAll()
  {
    if (!_open)
    {
      return nullptr;
    }

    // Seek to data start
    _file.seekg(_info.dataOffset);
    _samplesRead = 0;

    return read(_info.totalSamples);
  }

  void close()
  {
    if (!_open)
    {
      return;
    }
    _file.close();
    _open = false;
  }

  bool isOpen() const noexcept { return _open; }

  const WavFileInfo& info() const noexcept { return _info; }

  bool seek(std::uint32_t sampleOffset)
  {
    if (!_open)
    {
      return false;
    }
    if (sampleOffset > _info.totalSamples)
    {
      return false;
    }

    std::uint16_t bytesPerSample = _info.bitsPerSample / 8;
    std::size_t frameBytes = bytesPerSample * _info.channels;
    std::uint64_t byteOffset = static_cast<std::uint64_t>(sampleOffset) * frameBytes;
    _file.seekg(static_cast<std::streamoff>(_info.dataOffset + byteOffset));
    _samplesRead = sampleOffset;
    return _file.good();
  }

  std::uint32_t remaining() const noexcept
  {
    if (_samplesRead >= _info.totalSamples)
    {
      return 0;
    }
    return _info.totalSamples - _samplesRead;
  }

private:
  bool parseHeader()
  {
    // Read RIFF chunk descriptor
    char chunkId[4];
    if (!readBytes(chunkId, 4))
    {
      return false;
    }
    if (std::memcmp(chunkId, "RIFF", 4) != 0)
    {
      return false;
    }

    std::uint32_t chunkSize = 0;
    if (!readU32(chunkSize))
    {
      return false;
    }

    char format[4];
    if (!readBytes(format, 4))
    {
      return false;
    }
    if (std::memcmp(format, "WAVE", 4) != 0)
    {
      return false;
    }

    // Find fmt and data chunks
    bool fmtFound = false;
    bool dataFound = false;

    while (!fmtFound || !dataFound)
    {
      char subchunkId[4];
      if (!readBytes(subchunkId, 4))
      {
        return false;
      }

      std::uint32_t subchunkSize = 0;
      if (!readU32(subchunkSize))
      {
        return false;
      }

      if (std::memcmp(subchunkId, "fmt ", 4) == 0)
      {
        if (!parseFmtChunk(subchunkSize))
        {
          return false;
        }
        fmtFound = true;
      }
      else if (std::memcmp(subchunkId, "data", 4) == 0)
      {
        _info.dataOffset = static_cast<std::uint32_t>(_file.tellg());
        _info.dataSize = subchunkSize;
        dataFound = true;
      }
      else
      {
        // Skip unknown chunk — RIFF pads odd-sized chunks to even boundary
        std::uint32_t skipBytes = subchunkSize + (subchunkSize & 1);
        _file.seekg(skipBytes, std::ios::cur);
        if (!_file.good())
        {
          return false;
        }
      }
    }

    // Calculate derived fields
    std::uint16_t bytesPerSample = _info.bitsPerSample / 8;
    std::size_t frameBytes = bytesPerSample * _info.channels;
    if (frameBytes > 0)
    {
      _info.totalSamples = _info.dataSize / static_cast<std::uint32_t>(frameBytes);
    }
    if (_info.sampleRate > 0)
    {
      _info.durationMs = static_cast<std::uint32_t>(
        (static_cast<std::uint64_t>(_info.totalSamples) * 1000) / _info.sampleRate);
    }

    return true;
  }

  bool parseFmtChunk(std::uint32_t chunkSize)
  {
    if (chunkSize < 16)
    {
      return false;
    }

    std::uint16_t audioFormat = 0;
    if (!readU16(audioFormat))
    {
      return false;
    }
    if (audioFormat != 1) // PCM only
    {
      return false;
    }

    if (!readU16(_info.channels))
    {
      return false;
    }
    if (!readU32(_info.sampleRate))
    {
      return false;
    }

    std::uint32_t byteRate = 0;
    if (!readU32(byteRate))
    {
      return false;
    }

    std::uint16_t blockAlign = 0;
    if (!readU16(blockAlign))
    {
      return false;
    }

    if (!readU16(_info.bitsPerSample))
    {
      return false;
    }

    // Validate bitsPerSample
    if (_info.bitsPerSample != 8 && _info.bitsPerSample != 16 && _info.bitsPerSample != 24)
    {
      return false;
    }

    // Skip any extra fmt bytes
    if (chunkSize > 16)
    {
      _file.seekg(chunkSize - 16, std::ios::cur);
    }

    return _file.good();
  }

  bool readBytes(char* buf, std::size_t count)
  {
    _file.read(buf, static_cast<std::streamsize>(count));
    return _file.gcount() == static_cast<std::streamsize>(count);
  }

  bool readU16(std::uint16_t& value)
  {
    std::uint8_t buf[2];
    if (!readBytes(reinterpret_cast<char*>(buf), 2))
    {
      return false;
    }
    value = static_cast<std::uint16_t>(buf[0] | (buf[1] << 8));
    return true;
  }

  bool readU32(std::uint32_t& value)
  {
    std::uint8_t buf[4];
    if (!readBytes(reinterpret_cast<char*>(buf), 4))
    {
      return false;
    }
    value = static_cast<std::uint32_t>(buf[0]) |
            (static_cast<std::uint32_t>(buf[1]) << 8) |
            (static_cast<std::uint32_t>(buf[2]) << 16) |
            (static_cast<std::uint32_t>(buf[3]) << 24);
    return true;
  }

  std::ifstream _file;
  WavFileInfo _info{};
  std::uint32_t _samplesRead = 0;
  bool _open = false;
};

} // namespace codecs
} // namespace iora
