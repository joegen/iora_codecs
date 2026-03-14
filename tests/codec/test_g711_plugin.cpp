#include <catch2/catch_test_macros.hpp>

#include <iora/iora.hpp>

#include "iora/codecs/codec/i_codec_factory.hpp"
#include "iora/codecs/core/media_buffer.hpp"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>

using namespace iora::codecs;

namespace {

std::string findPluginPath()
{
  auto exeDir = iora::util::getExecutableDir();
  auto candidate = iora::util::resolveRelativePath(exeDir, "../modules/g711/mod_g711.so");
  if (std::filesystem::exists(candidate))
  {
    return candidate;
  }
  candidate = exeDir + "/mod_g711.so";
  if (std::filesystem::exists(candidate))
  {
    return candidate;
  }
  return {};
}

void shutdownService()
{
  try
  {
    iora::IoraService::shutdown();
  }
  catch (...)
  {
  }
}

/// Initializes IoraService and loads the G.711 plugin exactly once.
/// Safe to call from any test in any order.
iora::IoraService& ensurePluginLoaded()
{
  static bool initialized = false;
  if (!initialized)
  {
    iora::IoraService::Config config;
    config.server.port = 18711;
    config.log.file = "g711_plugin_test";
    config.log.level = "info";

    shutdownService();
    iora::IoraService::init(config);
    std::atexit(shutdownService);

    auto& svc = iora::IoraService::instanceRef();

    auto pluginPath = findPluginPath();
    if (pluginPath.empty() || !svc.loadSingleModule(pluginPath))
    {
      throw std::runtime_error("Failed to load mod_g711.so from: " + pluginPath);
    }

    initialized = true;
  }
  return iora::IoraService::instanceRef();
}

} // anonymous namespace

TEST_CASE("G711CodecPlugin: module loads successfully", "[g711][plugin]")
{
  auto& svc = ensurePluginLoaded();

  auto apis = svc.getExportedApiNames();
  bool hasPcmu = false;
  bool hasPcma = false;
  for (const auto& name : apis)
  {
    if (name == "g711.pcmu.factory")
    {
      hasPcmu = true;
    }
    if (name == "g711.pcma.factory")
    {
      hasPcma = true;
    }
  }
  CHECK(hasPcmu);
  CHECK(hasPcma);
}

TEST_CASE("G711CodecPlugin: exported PCMU factory API", "[g711][plugin]")
{
  auto& svc = ensurePluginLoaded();

  auto pcmuFactory = svc.callExportedApi<std::shared_ptr<ICodecFactory>>("g711.pcmu.factory");
  REQUIRE(pcmuFactory != nullptr);
  CHECK(pcmuFactory->codecInfo().name == "PCMU");
  CHECK(pcmuFactory->codecInfo().clockRate == 8000);
  CHECK(pcmuFactory->codecInfo().defaultPayloadType == 0);

  auto encoder = pcmuFactory->createEncoder(pcmuFactory->codecInfo());
  REQUIRE(encoder != nullptr);
  CHECK(encoder->info().name == "PCMU");
}

TEST_CASE("G711CodecPlugin: exported PCMA factory API", "[g711][plugin]")
{
  auto& svc = ensurePluginLoaded();

  auto pcmaFactory = svc.callExportedApi<std::shared_ptr<ICodecFactory>>("g711.pcma.factory");
  REQUIRE(pcmaFactory != nullptr);
  CHECK(pcmaFactory->codecInfo().name == "PCMA");
  CHECK(pcmaFactory->codecInfo().clockRate == 8000);
  CHECK(pcmaFactory->codecInfo().defaultPayloadType == 8);

  auto decoder = pcmaFactory->createDecoder(pcmaFactory->codecInfo());
  REQUIRE(decoder != nullptr);
  CHECK(decoder->info().name == "PCMA");
}

TEST_CASE("G711CodecPlugin: encode/decode via plugin-loaded factory", "[g711][plugin]")
{
  auto& svc = ensurePluginLoaded();

  auto pcmuFactory = svc.callExportedApi<std::shared_ptr<ICodecFactory>>("g711.pcmu.factory");
  REQUIRE(pcmuFactory != nullptr);

  auto encoder = pcmuFactory->createEncoder(pcmuFactory->codecInfo());
  auto decoder = pcmuFactory->createDecoder(pcmuFactory->codecInfo());
  REQUIRE(encoder != nullptr);
  REQUIRE(decoder != nullptr);

  // Create a 20ms frame of S16 PCM (160 samples)
  constexpr std::size_t numSamples = 160;
  auto pcmBuf = MediaBuffer::create(numSamples * 2);
  auto* pcm = reinterpret_cast<std::int16_t*>(pcmBuf->data());
  for (std::size_t i = 0; i < numSamples; ++i)
  {
    pcm[i] = static_cast<std::int16_t>(5000 * (i % 2 == 0 ? 1 : -1));
  }
  pcmBuf->setSize(numSamples * 2);
  pcmBuf->setTimestamp(1000);

  auto encoded = encoder->encode(*pcmBuf);
  REQUIRE(encoded->size() == numSamples);
  CHECK(encoded->timestamp() == 1000);

  auto decoded = decoder->decode(*encoded);
  REQUIRE(decoded->size() == numSamples * 2);
  CHECK(decoded->timestamp() == 1000);

  const auto* result = reinterpret_cast<const std::int16_t*>(decoded->data());
  for (std::size_t i = 0; i < numSamples; ++i)
  {
    CHECK(std::abs(static_cast<int>(result[i]) - static_cast<int>(pcm[i])) < 500);
  }
}
