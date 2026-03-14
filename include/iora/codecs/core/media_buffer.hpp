#pragma once

/// @file media_buffer.hpp
/// @brief Poolable media frame container with RTP and video metadata.

#include "iora/codecs/format/pixel_format.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <utility>

namespace iora {
namespace codecs {

/// Contiguous byte buffer carrying one media frame plus RTP metadata.
///
/// MediaBuffer owns a heap-allocated byte array of fixed capacity.
/// The "used" region is [0, size()). Metadata fields carry RTP
/// framing information alongside the raw media data.
///
/// Ownership model: use shared_ptr for fan-out scenarios, unique_ptr
/// for single-owner pipelines. The static create() factory returns
/// shared_ptr for convenience.
class MediaBuffer
{
public:
  /// Construct a buffer with the given byte capacity.
  explicit MediaBuffer(std::size_t capacity)
    : _data(std::make_unique<std::uint8_t[]>(capacity))
    , _capacity(capacity)
    , _size(0)
    , _timestamp(0)
    , _sequenceNumber(0)
    , _ssrc(0)
    , _payloadType(0)
    , _marker(false)
  {
  }

  /// Factory returning a shared_ptr for shared-ownership scenarios.
  static std::shared_ptr<MediaBuffer> create(std::size_t capacity)
  {
    return std::make_shared<MediaBuffer>(capacity);
  }

  // Move-only (the unique_ptr enforces this).
  MediaBuffer(MediaBuffer&&) noexcept = default;
  MediaBuffer& operator=(MediaBuffer&&) noexcept = default;
  MediaBuffer(const MediaBuffer&) = delete;
  MediaBuffer& operator=(const MediaBuffer&) = delete;

  // -- Data access --

  /// Pointer to the raw byte data.
  std::uint8_t* data() noexcept { return _data.get(); }
  const std::uint8_t* data() const noexcept { return _data.get(); }

  /// Number of bytes currently used (written).
  std::size_t size() const noexcept { return _size; }

  /// Total allocated capacity in bytes.
  std::size_t capacity() const noexcept { return _capacity; }

  /// Update the used size. Must be <= capacity().
  void setSize(std::size_t n) noexcept { _size = (n <= _capacity) ? n : _capacity; }

  // -- RTP metadata --

  std::uint32_t timestamp() const noexcept { return _timestamp; }
  void setTimestamp(std::uint32_t ts) noexcept { _timestamp = ts; }

  std::uint16_t sequenceNumber() const noexcept { return _sequenceNumber; }
  void setSequenceNumber(std::uint16_t seq) noexcept { _sequenceNumber = seq; }

  std::uint32_t ssrc() const noexcept { return _ssrc; }
  void setSsrc(std::uint32_t ssrc) noexcept { _ssrc = ssrc; }

  std::uint8_t payloadType() const noexcept { return _payloadType; }
  void setPayloadType(std::uint8_t pt) noexcept { _payloadType = pt; }

  bool marker() const noexcept { return _marker; }
  void setMarker(bool m) noexcept { _marker = m; }

  std::chrono::steady_clock::time_point captureTime() const noexcept { return _captureTime; }
  void setCaptureTime(std::chrono::steady_clock::time_point t) noexcept { _captureTime = t; }

  // -- Video frame metadata (optional — zero/None when unused) --

  std::uint32_t width() const noexcept { return _width; }
  void setWidth(std::uint32_t w) noexcept { _width = w; }

  std::uint32_t height() const noexcept { return _height; }
  void setHeight(std::uint32_t h) noexcept { _height = h; }

  std::uint32_t stride(std::size_t plane) const noexcept
  {
    return (plane < 3) ? _stride[plane] : 0;
  }
  void setStride(std::size_t plane, std::uint32_t s) noexcept
  {
    if (plane < 3) { _stride[plane] = s; }
  }

  PixelFormat pixelFormat() const noexcept { return _pixelFormat; }
  void setPixelFormat(PixelFormat fmt) noexcept { _pixelFormat = fmt; }

  /// Copy all metadata fields from another buffer without touching
  /// the data payload.
  void copyMetadataFrom(const MediaBuffer& other) noexcept
  {
    _timestamp = other._timestamp;
    _sequenceNumber = other._sequenceNumber;
    _ssrc = other._ssrc;
    _payloadType = other._payloadType;
    _marker = other._marker;
    _captureTime = other._captureTime;
    _width = other._width;
    _height = other._height;
    _stride[0] = other._stride[0];
    _stride[1] = other._stride[1];
    _stride[2] = other._stride[2];
    _pixelFormat = other._pixelFormat;
  }

  /// Create an independent copy of this buffer (data + metadata).
  std::shared_ptr<MediaBuffer> clone() const
  {
    auto copy = std::make_shared<MediaBuffer>(_capacity);
    std::memcpy(copy->_data.get(), _data.get(), _size);
    copy->_size = _size;
    copy->copyMetadataFrom(*this);
    return copy;
  }

private:
  std::unique_ptr<std::uint8_t[]> _data;
  std::size_t _capacity;
  std::size_t _size;

  // RTP metadata
  std::uint32_t _timestamp;
  std::uint16_t _sequenceNumber;
  std::uint32_t _ssrc;
  std::uint8_t _payloadType;
  bool _marker;
  std::chrono::steady_clock::time_point _captureTime{};

  // Video frame metadata (zero/None when unused)
  std::uint32_t _width = 0;
  std::uint32_t _height = 0;
  std::uint32_t _stride[3] = {0, 0, 0};
  PixelFormat _pixelFormat = PixelFormat::None;
};

} // namespace codecs
} // namespace iora
