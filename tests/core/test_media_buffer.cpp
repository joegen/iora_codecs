#include <catch2/catch_test_macros.hpp>

#include "iora/codecs/core/media_buffer.hpp"
#include "iora/codecs/core/media_buffer_pool.hpp"

#include <cstring>
#include <thread>
#include <vector>

using namespace iora::codecs;

TEST_CASE("MediaBuffer: construction", "[core][buffer]")
{
  MediaBuffer buf(1024);
  CHECK(buf.capacity() == 1024);
  CHECK(buf.size() == 0);
  CHECK(buf.data() != nullptr);
}

TEST_CASE("MediaBuffer: write and read data", "[core][buffer]")
{
  MediaBuffer buf(64);
  const char* msg = "hello media";
  std::size_t len = std::strlen(msg);
  std::memcpy(buf.data(), msg, len);
  buf.setSize(len);

  CHECK(buf.size() == len);
  CHECK(std::memcmp(buf.data(), msg, len) == 0);
}

TEST_CASE("MediaBuffer: setSize clamps to capacity", "[core][buffer]")
{
  MediaBuffer buf(100);
  buf.setSize(200);
  CHECK(buf.size() == 100);
}

TEST_CASE("MediaBuffer: metadata", "[core][buffer]")
{
  MediaBuffer buf(64);

  buf.setTimestamp(12345);
  CHECK(buf.timestamp() == 12345);

  buf.setSequenceNumber(42);
  CHECK(buf.sequenceNumber() == 42);

  buf.setSsrc(0xDEADBEEF);
  CHECK(buf.ssrc() == 0xDEADBEEF);

  buf.setPayloadType(111);
  CHECK(buf.payloadType() == 111);

  buf.setMarker(true);
  CHECK(buf.marker() == true);

  auto now = std::chrono::steady_clock::now();
  buf.setCaptureTime(now);
  CHECK(buf.captureTime() == now);
}

TEST_CASE("MediaBuffer: copyMetadataFrom", "[core][buffer]")
{
  MediaBuffer src(64);
  src.setTimestamp(999);
  src.setSequenceNumber(7);
  src.setSsrc(0x12345678);
  src.setPayloadType(96);
  src.setMarker(true);
  auto t = std::chrono::steady_clock::now();
  src.setCaptureTime(t);

  // Write some data to src
  std::memset(src.data(), 0xAB, 32);
  src.setSize(32);

  MediaBuffer dst(128);
  std::memset(dst.data(), 0xCD, 64);
  dst.setSize(64);

  dst.copyMetadataFrom(src);

  // Metadata copied
  CHECK(dst.timestamp() == 999);
  CHECK(dst.sequenceNumber() == 7);
  CHECK(dst.ssrc() == 0x12345678);
  CHECK(dst.payloadType() == 96);
  CHECK(dst.marker() == true);
  CHECK(dst.captureTime() == t);

  // Data NOT touched
  CHECK(dst.size() == 64);
  CHECK(dst.data()[0] == 0xCD);
}

TEST_CASE("MediaBuffer: clone", "[core][buffer]")
{
  auto original = MediaBuffer::create(256);
  std::memset(original->data(), 0x42, 100);
  original->setSize(100);
  original->setTimestamp(5000);
  original->setSequenceNumber(10);
  original->setSsrc(0xAABBCCDD);

  auto copy = original->clone();

  // Independent allocation
  CHECK(copy->data() != original->data());
  CHECK(copy->capacity() == 256);
  CHECK(copy->size() == 100);
  CHECK(copy->data()[0] == 0x42);
  CHECK(copy->data()[99] == 0x42);

  // Metadata copied
  CHECK(copy->timestamp() == 5000);
  CHECK(copy->sequenceNumber() == 10);
  CHECK(copy->ssrc() == 0xAABBCCDD);

  // Modifying copy doesn't affect original
  copy->data()[0] = 0xFF;
  CHECK(original->data()[0] == 0x42);
}

TEST_CASE("MediaBuffer: create factory returns shared_ptr", "[core][buffer]")
{
  auto buf = MediaBuffer::create(512);
  CHECK(buf != nullptr);
  CHECK(buf->capacity() == 512);
  CHECK(buf.use_count() == 1);
}

TEST_CASE("MediaBuffer: video metadata defaults", "[core][buffer]")
{
  MediaBuffer buf(64);
  CHECK(buf.width() == 0);
  CHECK(buf.height() == 0);
  CHECK(buf.stride(0) == 0);
  CHECK(buf.stride(1) == 0);
  CHECK(buf.stride(2) == 0);
  CHECK(buf.stride(3) == 0); // out of range returns 0
  CHECK(buf.pixelFormat() == PixelFormat::None);
}

TEST_CASE("MediaBuffer: video metadata get/set", "[core][buffer]")
{
  MediaBuffer buf(64);
  buf.setWidth(1920);
  buf.setHeight(1080);
  buf.setStride(0, 1920);
  buf.setStride(1, 960);
  buf.setStride(2, 960);
  buf.setPixelFormat(PixelFormat::NV12);

  CHECK(buf.width() == 1920);
  CHECK(buf.height() == 1080);
  CHECK(buf.stride(0) == 1920);
  CHECK(buf.stride(1) == 960);
  CHECK(buf.stride(2) == 960);
  CHECK(buf.pixelFormat() == PixelFormat::NV12);
}

TEST_CASE("MediaBuffer: copyMetadataFrom includes video fields", "[core][buffer]")
{
  MediaBuffer src(64);
  src.setTimestamp(100);
  src.setWidth(640);
  src.setHeight(480);
  src.setStride(0, 640);
  src.setStride(1, 320);
  src.setStride(2, 320);
  src.setPixelFormat(PixelFormat::I420);

  MediaBuffer dst(64);
  dst.copyMetadataFrom(src);

  CHECK(dst.timestamp() == 100);
  CHECK(dst.width() == 640);
  CHECK(dst.height() == 480);
  CHECK(dst.stride(0) == 640);
  CHECK(dst.stride(1) == 320);
  CHECK(dst.stride(2) == 320);
  CHECK(dst.pixelFormat() == PixelFormat::I420);
}

TEST_CASE("MediaBuffer: clone includes video fields", "[core][buffer]")
{
  auto original = MediaBuffer::create(256);
  original->setSize(100);
  original->setWidth(320);
  original->setHeight(240);
  original->setStride(0, 320);
  original->setStride(1, 160);
  original->setStride(2, 160);
  original->setPixelFormat(PixelFormat::NV12);

  auto copy = original->clone();

  CHECK(copy->width() == 320);
  CHECK(copy->height() == 240);
  CHECK(copy->stride(0) == 320);
  CHECK(copy->stride(1) == 160);
  CHECK(copy->stride(2) == 160);
  CHECK(copy->pixelFormat() == PixelFormat::NV12);
}

// -- MediaBufferPool tests --

TEST_CASE("MediaBufferPool: basic acquire and release", "[core][pool]")
{
  MediaBufferPool pool(4, 1024);
  CHECK(pool.availableCount() == 4);
  CHECK(pool.bufferCapacity() == 1024);

  auto buf = pool.acquire();
  REQUIRE(buf != nullptr);
  CHECK(buf->capacity() == 1024);
  CHECK(buf->size() == 0);
  CHECK(pool.availableCount() == 3);

  // Release by resetting shared_ptr
  buf.reset();
  CHECK(pool.availableCount() == 4);
}

TEST_CASE("MediaBufferPool: exhaustion returns nullptr", "[core][pool]")
{
  MediaBufferPool pool(2, 64);

  auto b1 = pool.acquire();
  auto b2 = pool.acquire();
  auto b3 = pool.acquire();

  CHECK(b1 != nullptr);
  CHECK(b2 != nullptr);
  CHECK(b3 == nullptr);
  CHECK(pool.availableCount() == 0);

  // Release one, acquire again succeeds
  b1.reset();
  CHECK(pool.availableCount() == 1);

  auto b4 = pool.acquire();
  CHECK(b4 != nullptr);
}

TEST_CASE("MediaBufferPool: recycled buffer reuses memory", "[core][pool]")
{
  MediaBufferPool pool(1, 128);

  auto buf1 = pool.acquire();
  auto* ptr1 = buf1->data();
  buf1.reset();

  auto buf2 = pool.acquire();
  auto* ptr2 = buf2->data();

  CHECK(ptr1 == ptr2);
}

TEST_CASE("MediaBufferPool: concurrent acquire/release", "[core][pool]")
{
  MediaBufferPool pool(32, 64);
  constexpr int iterations = 1000;
  constexpr int numThreads = 4;

  auto worker = [&]() {
    for (int i = 0; i < iterations; ++i)
    {
      auto buf = pool.acquire();
      if (buf)
      {
        std::memset(buf->data(), 0, buf->capacity());
        buf->setSize(buf->capacity());
      }
      // buf goes out of scope, returning to pool
    }
  };

  std::vector<std::thread> threads;
  threads.reserve(numThreads);
  for (int i = 0; i < numThreads; ++i)
  {
    threads.emplace_back(worker);
  }
  for (auto& t : threads)
  {
    t.join();
  }

  // All buffers should be back in the pool
  CHECK(pool.availableCount() == 32);
}
