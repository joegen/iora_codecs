#include <catch2/catch_test_macros.hpp>

#include "iora/codecs/codec/codec_registry.hpp"
#include "mock_codec.hpp"

#include <thread>
#include <vector>

using namespace iora::codecs;
using namespace iora::codecs::testing;
using namespace std::chrono_literals;

static CodecInfo makeOpusInfo()
{
  CodecInfo info;
  info.name = "opus";
  info.type = CodecType::Audio;
  info.mediaSubtype = "opus";
  info.clockRate = 48000;
  info.channels = 2;
  info.defaultPayloadType = 111;
  info.defaultBitrate = 64000;
  info.frameSize = 20000us;
  info.features = CodecFeatures::Fec | CodecFeatures::Dtx
                | CodecFeatures::Vbr | CodecFeatures::Plc;
  return info;
}

static CodecInfo makePcmuInfo()
{
  CodecInfo info;
  info.name = "PCMU";
  info.type = CodecType::Audio;
  info.mediaSubtype = "PCMU";
  info.clockRate = 8000;
  info.channels = 1;
  info.defaultPayloadType = 0;
  info.defaultBitrate = 64000;
  info.frameSize = 20000us;
  info.features = CodecFeatures::Cbr;
  return info;
}

TEST_CASE("CodecRegistry: register and enumerate", "[codec][registry]")
{
  CodecRegistry registry;
  auto factory = std::make_shared<MockCodecFactory>(makeOpusInfo());
  registry.registerFactory(factory);

  auto codecs = registry.enumerateCodecs();
  REQUIRE(codecs.size() == 1);
  CHECK(codecs[0].name == "opus");
}

TEST_CASE("CodecRegistry: register multiple factories", "[codec][registry]")
{
  CodecRegistry registry;
  registry.registerFactory(std::make_shared<MockCodecFactory>(makeOpusInfo()));
  registry.registerFactory(std::make_shared<MockCodecFactory>(makePcmuInfo()));

  auto codecs = registry.enumerateCodecs();
  CHECK(codecs.size() == 2);
}

TEST_CASE("CodecRegistry: findByName", "[codec][registry]")
{
  CodecRegistry registry;
  registry.registerFactory(std::make_shared<MockCodecFactory>(makeOpusInfo()));
  registry.registerFactory(std::make_shared<MockCodecFactory>(makePcmuInfo()));

  auto opus = registry.findByName("opus");
  REQUIRE(opus.has_value());
  CHECK(opus->name == "opus");
  CHECK(opus->clockRate == 48000);

  auto pcmu = registry.findByName("PCMU");
  REQUIRE(pcmu.has_value());
  CHECK(pcmu->clockRate == 8000);

  CHECK_FALSE(registry.findByName("nonexistent").has_value());
}

TEST_CASE("CodecRegistry: findByPayloadType", "[codec][registry]")
{
  CodecRegistry registry;
  registry.registerFactory(std::make_shared<MockCodecFactory>(makeOpusInfo()));
  registry.registerFactory(std::make_shared<MockCodecFactory>(makePcmuInfo()));

  auto opus = registry.findByPayloadType(111);
  REQUIRE(opus.has_value());
  CHECK(opus->name == "opus");

  auto pcmu = registry.findByPayloadType(0);
  REQUIRE(pcmu.has_value());
  CHECK(pcmu->name == "PCMU");

  CHECK_FALSE(registry.findByPayloadType(99).has_value());
}

TEST_CASE("CodecRegistry: createEncoder", "[codec][registry]")
{
  CodecRegistry registry;
  registry.registerFactory(std::make_shared<MockCodecFactory>(makeOpusInfo()));

  auto encoder = registry.createEncoder(makeOpusInfo());
  REQUIRE(encoder != nullptr);
  CHECK(encoder->info().name == "opus");
}

TEST_CASE("CodecRegistry: createDecoder", "[codec][registry]")
{
  CodecRegistry registry;
  registry.registerFactory(std::make_shared<MockCodecFactory>(makePcmuInfo()));

  auto decoder = registry.createDecoder(makePcmuInfo());
  REQUIRE(decoder != nullptr);
  CHECK(decoder->info().name == "PCMU");
}

TEST_CASE("CodecRegistry: createEncoder unknown codec returns nullptr", "[codec][registry]")
{
  CodecRegistry registry;
  registry.registerFactory(std::make_shared<MockCodecFactory>(makeOpusInfo()));

  CHECK(registry.createEncoder(makePcmuInfo()) == nullptr);
}

TEST_CASE("CodecRegistry: unregisterFactory", "[codec][registry]")
{
  CodecRegistry registry;
  registry.registerFactory(std::make_shared<MockCodecFactory>(makeOpusInfo()));

  CHECK(registry.findByName("opus").has_value());
  registry.unregisterFactory("opus");
  CHECK_FALSE(registry.findByName("opus").has_value());
  CHECK(registry.enumerateCodecs().empty());
}

TEST_CASE("CodecRegistry: unregister nonexistent is no-op", "[codec][registry]")
{
  CodecRegistry registry;
  registry.unregisterFactory("nonexistent"); // Should not throw
  CHECK(registry.enumerateCodecs().empty());
}

TEST_CASE("CodecRegistry: double register throws", "[codec][registry]")
{
  CodecRegistry registry;
  registry.registerFactory(std::make_shared<MockCodecFactory>(makeOpusInfo()));

  CHECK_THROWS_AS(
    registry.registerFactory(std::make_shared<MockCodecFactory>(makeOpusInfo())),
    std::runtime_error);
}

TEST_CASE("CodecRegistry: null factory throws", "[codec][registry]")
{
  CodecRegistry registry;
  CHECK_THROWS_AS(
    registry.registerFactory(nullptr),
    std::invalid_argument);
}

TEST_CASE("CodecRegistry: concurrent register/enumerate", "[codec][registry]")
{
  CodecRegistry registry;
  constexpr int numThreads = 4;
  constexpr int iterations = 100;

  // Pre-register one factory so enumerate always has something
  registry.registerFactory(std::make_shared<MockCodecFactory>(makeOpusInfo()));

  auto worker = [&](int threadId) {
    for (int i = 0; i < iterations; ++i)
    {
      // Enumerate (read operation)
      auto codecs = registry.enumerateCodecs();
      CHECK(codecs.size() >= 1);

      // Register/unregister a unique codec per thread+iteration
      CodecInfo info;
      info.name = "test_" + std::to_string(threadId) + "_" + std::to_string(i);
      info.clockRate = 8000;
      info.channels = 1;
      auto factory = std::make_shared<MockCodecFactory>(info);

      registry.registerFactory(factory);
      registry.unregisterFactory(info.name);
    }
  };

  std::vector<std::thread> threads;
  threads.reserve(numThreads);
  for (int i = 0; i < numThreads; ++i)
  {
    threads.emplace_back(worker, i);
  }
  for (auto& t : threads)
  {
    t.join();
  }

  // Only the original opus factory should remain
  auto remaining = registry.enumerateCodecs();
  CHECK(remaining.size() == 1);
  CHECK(remaining[0].name == "opus");
}
