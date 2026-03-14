#pragma once

/// @file mock_codec.hpp
/// @brief Mock ICodec and ICodecFactory for unit testing.

#include "iora/codecs/codec/i_codec.hpp"
#include "iora/codecs/codec/i_codec_factory.hpp"

#include <cstring>

namespace iora {
namespace codecs {
namespace testing {

/// Minimal ICodec mock that returns empty buffers.
class MockCodec : public ICodec
{
public:
  explicit MockCodec(CodecInfo info)
    : _info(std::move(info))
  {
  }

  const CodecInfo& info() const override { return _info; }

  std::shared_ptr<MediaBuffer> encode(const MediaBuffer& input) override
  {
    auto buf = MediaBuffer::create(input.size());
    std::memcpy(buf->data(), input.data(), input.size());
    buf->setSize(input.size());
    buf->copyMetadataFrom(input);
    return buf;
  }

  std::shared_ptr<MediaBuffer> decode(const MediaBuffer& input) override
  {
    auto buf = MediaBuffer::create(input.size());
    std::memcpy(buf->data(), input.data(), input.size());
    buf->setSize(input.size());
    buf->copyMetadataFrom(input);
    return buf;
  }

  std::shared_ptr<MediaBuffer> plc(std::size_t frameSamples) override
  {
    auto buf = MediaBuffer::create(frameSamples * 2); // S16 = 2 bytes/sample
    std::memset(buf->data(), 0, buf->capacity());
    buf->setSize(buf->capacity());
    return buf;
  }

  bool setParameter(const std::string& key, std::uint32_t value) override
  {
    if (key == "bitrate")
    {
      _bitrate = value;
      return true;
    }
    return false;
  }

  std::uint32_t getParameter(const std::string& key) const override
  {
    if (key == "bitrate")
    {
      return _bitrate;
    }
    return 0;
  }

private:
  CodecInfo _info;
  std::uint32_t _bitrate = 0;
};

/// Minimal ICodecFactory mock that produces MockCodec instances.
class MockCodecFactory : public ICodecFactory
{
public:
  explicit MockCodecFactory(CodecInfo info)
    : _info(std::move(info))
  {
  }

  const CodecInfo& codecInfo() const override { return _info; }

  bool supports(const CodecInfo& info) const override
  {
    return _info.matches(info);
  }

  std::unique_ptr<ICodec> createEncoder(const CodecInfo& params) override
  {
    return std::make_unique<MockCodec>(params);
  }

  std::unique_ptr<ICodec> createDecoder(const CodecInfo& params) override
  {
    return std::make_unique<MockCodec>(params);
  }

private:
  CodecInfo _info;
};

} // namespace testing
} // namespace codecs
} // namespace iora
