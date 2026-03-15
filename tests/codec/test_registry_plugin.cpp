#include <catch2/catch_test_macros.hpp>

#include <iora/iora.hpp>

#include "iora/codecs/codec/codec_registry.hpp"
#include "iora/codecs/codec/i_codec_factory.hpp"
#include "iora/codecs/core/media_buffer.hpp"

#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>

using namespace iora::codecs;

namespace {

std::string findPluginPath(const std::string& moduleName)
{
  auto exeDir = iora::util::getExecutableDir();
  auto candidate = iora::util::resolveRelativePath(
    exeDir, "../modules/" + moduleName + "/mod_" + moduleName + ".so");
  if (std::filesystem::exists(candidate))
  {
    return candidate;
  }
  candidate = exeDir + "/mod_" + moduleName + ".so";
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

/// Initializes IoraService, loads mod_codec_registry, then loads mod_g711.
iora::IoraService& ensurePluginsLoaded()
{
  static bool initialized = false;
  if (!initialized)
  {
    iora::IoraService::Config config;
    config.server.port = 18900;
    config.log.file = "registry_plugin_test";
    config.log.level = "info";

    shutdownService();
    iora::IoraService::init(config);
    std::atexit(shutdownService);

    auto& svc = iora::IoraService::instanceRef();

    // Load registry first
    auto registryPath = findPluginPath("codec_registry");
    if (registryPath.empty() || !svc.loadSingleModule(registryPath))
    {
      throw std::runtime_error("Failed to load mod_codec_registry.so from: " + registryPath);
    }

    // Load G.711 codec — should auto-register with the registry
    auto g711Path = findPluginPath("g711");
    if (g711Path.empty() || !svc.loadSingleModule(g711Path))
    {
      throw std::runtime_error("Failed to load mod_g711.so from: " + g711Path);
    }

    initialized = true;
  }
  return iora::IoraService::instanceRef();
}

} // anonymous namespace

TEST_CASE("RegistryPlugin: module loads and exports codecs.registry", "[registry][plugin]")
{
  auto& svc = ensurePluginsLoaded();

  auto apis = svc.getExportedApiNames();
  bool hasRegistry = false;
  for (const auto& name : apis)
  {
    if (name == "codecs.registry")
    {
      hasRegistry = true;
      break;
    }
  }
  CHECK(hasRegistry);
}

TEST_CASE("RegistryPlugin: G.711 auto-registered PCMU and PCMA", "[registry][plugin]")
{
  auto& svc = ensurePluginsLoaded();

  auto& registry = svc.callExportedApi<CodecRegistry&>("codecs.registry");

  auto pcmu = registry.findByName("PCMU");
  REQUIRE(pcmu.has_value());
  CHECK(pcmu->clockRate == 8000);
  CHECK(pcmu->defaultPayloadType == 0);

  auto pcma = registry.findByName("PCMA");
  REQUIRE(pcma.has_value());
  CHECK(pcma->clockRate == 8000);
  CHECK(pcma->defaultPayloadType == 8);
}

TEST_CASE("RegistryPlugin: enumerateCodecs returns registered codecs", "[registry][plugin]")
{
  auto& svc = ensurePluginsLoaded();

  auto& registry = svc.callExportedApi<CodecRegistry&>("codecs.registry");
  auto codecs = registry.enumerateCodecs();

  REQUIRE(codecs.size() >= 2);

  bool hasPcmu = false;
  bool hasPcma = false;
  for (const auto& info : codecs)
  {
    if (info.name == "PCMU")
    {
      hasPcmu = true;
    }
    if (info.name == "PCMA")
    {
      hasPcma = true;
    }
  }
  CHECK(hasPcmu);
  CHECK(hasPcma);
}

TEST_CASE("RegistryPlugin: findByPayloadType via registry", "[registry][plugin]")
{
  auto& svc = ensurePluginsLoaded();

  auto& registry = svc.callExportedApi<CodecRegistry&>("codecs.registry");

  auto pcmu = registry.findByPayloadType(0);
  REQUIRE(pcmu.has_value());
  CHECK(pcmu->name == "PCMU");

  auto pcma = registry.findByPayloadType(8);
  REQUIRE(pcma.has_value());
  CHECK(pcma->name == "PCMA");

  CHECK_FALSE(registry.findByPayloadType(99).has_value());
}

TEST_CASE("RegistryPlugin: createEncoder via registry", "[registry][plugin]")
{
  auto& svc = ensurePluginsLoaded();

  auto& registry = svc.callExportedApi<CodecRegistry&>("codecs.registry");

  auto pcmu = registry.findByName("PCMU");
  REQUIRE(pcmu.has_value());

  auto encoder = registry.createEncoder(*pcmu);
  REQUIRE(encoder != nullptr);
  CHECK(encoder->info().name == "PCMU");
}

TEST_CASE("RegistryPlugin: createDecoder via registry", "[registry][plugin]")
{
  auto& svc = ensurePluginsLoaded();

  auto& registry = svc.callExportedApi<CodecRegistry&>("codecs.registry");

  auto pcma = registry.findByName("PCMA");
  REQUIRE(pcma.has_value());

  auto decoder = registry.createDecoder(*pcma);
  REQUIRE(decoder != nullptr);
  CHECK(decoder->info().name == "PCMA");
}

TEST_CASE("RegistryPlugin: encode/decode round-trip via registry-created codecs", "[registry][plugin]")
{
  auto& svc = ensurePluginsLoaded();

  auto& registry = svc.callExportedApi<CodecRegistry&>("codecs.registry");

  auto pcmuInfo = registry.findByName("PCMU");
  REQUIRE(pcmuInfo.has_value());

  auto encoder = registry.createEncoder(*pcmuInfo);
  auto decoder = registry.createDecoder(*pcmuInfo);
  REQUIRE(encoder != nullptr);
  REQUIRE(decoder != nullptr);

  // Create a 20ms frame of S16 PCM (160 samples at 8kHz)
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
  REQUIRE(encoded != nullptr);
  REQUIRE(encoded->size() == numSamples);

  auto decoded = decoder->decode(*encoded);
  REQUIRE(decoded != nullptr);
  REQUIRE(decoded->size() == numSamples * 2);

  const auto* result = reinterpret_cast<const std::int16_t*>(decoded->data());
  for (std::size_t i = 0; i < numSamples; ++i)
  {
    CHECK(std::abs(static_cast<int>(result[i]) - static_cast<int>(pcm[i])) < 500);
  }
}

TEST_CASE("RegistryPlugin: exported convenience APIs", "[registry][plugin]")
{
  auto& svc = ensurePluginsLoaded();

  // Test codecs.registry.enumerate
  auto codecs = svc.callExportedApi<std::vector<CodecInfo>>("codecs.registry.enumerate");
  CHECK(codecs.size() >= 2);

  // Test codecs.registry.findByName
  auto pcmu = svc.callExportedApi<std::optional<CodecInfo>>("codecs.registry.findByName",
    std::string("PCMU"));
  REQUIRE(pcmu.has_value());
  CHECK(pcmu->name == "PCMU");

  // Test codecs.registry.findByPayloadType
  auto pcma = svc.callExportedApi<std::optional<CodecInfo>>("codecs.registry.findByPayloadType",
    static_cast<std::uint8_t>(8));
  REQUIRE(pcma.has_value());
  CHECK(pcma->name == "PCMA");
}
