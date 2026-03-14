#pragma once

/// @file media_buffer_pool.hpp
/// @brief Pre-allocated pool of MediaBuffers with automatic recycling.

#include "iora/codecs/core/media_buffer.hpp"

#include <cassert>
#include <cstddef>
#include <memory>
#include <mutex>
#include <vector>

namespace iora {
namespace codecs {

/// Thread-safe pool of pre-allocated MediaBuffers.
///
/// acquire() returns a shared_ptr with a custom deleter that returns
/// the buffer to the pool instead of freeing it. When the last
/// shared_ptr reference is dropped, the buffer is automatically
/// recycled.
///
/// **Lifetime requirement**: The pool MUST outlive all shared_ptr
/// instances returned by acquire(). The destructor asserts that all
/// buffers have been returned. Violating this causes use-after-free.
class MediaBufferPool : public std::enable_shared_from_this<MediaBufferPool>
{
public:
  /// Construct a pool with @p poolSize buffers, each of @p bufferCapacity bytes.
  MediaBufferPool(std::size_t poolSize, std::size_t bufferCapacity)
    : _poolSize(poolSize)
    , _bufferCapacity(bufferCapacity)
  {
    _free.reserve(poolSize);
    for (std::size_t i = 0; i < poolSize; ++i)
    {
      _free.push_back(std::make_unique<MediaBuffer>(bufferCapacity));
    }
  }

  ~MediaBufferPool()
  {
    std::lock_guard<std::mutex> lock(_mutex);
    assert(_free.size() == _poolSize &&
      "MediaBufferPool destroyed while buffers are still outstanding");
  }

  // Non-copyable, non-movable (shared_ptr deleters capture pool pointer).
  MediaBufferPool(const MediaBufferPool&) = delete;
  MediaBufferPool& operator=(const MediaBufferPool&) = delete;
  MediaBufferPool(MediaBufferPool&&) = delete;
  MediaBufferPool& operator=(MediaBufferPool&&) = delete;

  /// Factory for creating a pool as a shared_ptr, enabling safe
  /// custom deleters that prevent use-after-free.
  static std::shared_ptr<MediaBufferPool> create(
    std::size_t poolSize, std::size_t bufferCapacity)
  {
    return std::make_shared<MediaBufferPool>(poolSize, bufferCapacity);
  }

  /// Acquire a buffer from the pool. Returns nullptr if the pool is
  /// exhausted. The returned shared_ptr's custom deleter recycles the
  /// buffer back to the pool on final release.
  ///
  /// When the pool is managed via shared_ptr (constructed with create()),
  /// the deleter captures a weak_ptr to the pool to prevent use-after-free.
  /// When used directly (stack/member), the caller must ensure the pool
  /// outlives all acquired buffers.
  std::shared_ptr<MediaBuffer> acquire()
  {
    std::lock_guard<std::mutex> lock(_mutex);
    if (_free.empty())
    {
      return nullptr;
    }

    auto* raw = _free.back().release();
    _free.pop_back();

    // Reset data size for reuse; capacity and allocation are preserved.
    raw->setSize(0);

    // Try to get a weak_ptr for safe deletion; falls back to raw `this`
    // when the pool is not managed by shared_ptr.
    std::weak_ptr<MediaBufferPool> weak;
    try
    {
      weak = shared_from_this();
    }
    catch (const std::bad_weak_ptr&)
    {
      // Pool not managed by shared_ptr — use raw this (caller
      // guarantees pool outlives all buffers per the contract).
      return std::shared_ptr<MediaBuffer>(raw, [this](MediaBuffer* buf) {
        recycle(buf);
      });
    }

    return std::shared_ptr<MediaBuffer>(raw,
      [weak = std::move(weak)](MediaBuffer* buf) {
        if (auto pool = weak.lock())
        {
          pool->recycle(buf);
        }
        else
        {
          // Pool was destroyed — free the buffer directly.
          delete buf;
        }
      });
  }

  /// Number of buffers currently available in the pool.
  std::size_t availableCount() const
  {
    std::lock_guard<std::mutex> lock(_mutex);
    return _free.size();
  }

  /// The byte capacity of each buffer in this pool.
  std::size_t bufferCapacity() const noexcept
  {
    return _bufferCapacity;
  }

private:
  void recycle(MediaBuffer* buf)
  {
    std::lock_guard<std::mutex> lock(_mutex);
    _free.push_back(std::unique_ptr<MediaBuffer>(buf));
  }

  std::size_t _poolSize;
  std::size_t _bufferCapacity;
  mutable std::mutex _mutex;
  std::vector<std::unique_ptr<MediaBuffer>> _free;
};

} // namespace codecs
} // namespace iora
