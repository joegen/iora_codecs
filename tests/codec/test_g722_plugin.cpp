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

static constexpr double kPi = 3.14159265358979323846;

namespace {

std::string findPluginPath()
{
  auto exeDir = iora::util::getExecutableDir();
  auto candidate = iora::util::resolveRelativePath(exeDir, "../modules/g722/mod_g722.so");
  if (std::filesystem::exists(candidate))
  {
    return candidate;
  }
  candidate = exeDir + "/mod_g722.so";
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

/// Initializes IoraService and loads the G.722 plugin exactly once.
/// Safe to call from any test in any order.
iora::IoraService& ensurePluginLoaded()
{
  static bool initialized = false;
  if (!initialized)
  {
    iora::IoraService::Config config;
    config.server.port = 18722;
    config.log.file = "g722_plugin_test";
    config.log.level = "info";

    shutdownService();
    iora::IoraService::init(config);
    std::atexit(shutdownService);

    auto& svc = iora::IoraService::instanceRef();

    auto pluginPath = findPluginPath();
    if (pluginPath.empty() || !svc.loadSingleModule(pluginPath))
    {
      throw std::runtime_error("Failed to load mod_g722.so from: " + pluginPath);
    }

    initialized = true;
  }
  return iora::IoraService::instanceRef();
}

} // anonymous namespace

TEST_CASE("G722CodecPlugin: module loads successfully", "[g722][plugin]")
{
  auto& svc = ensurePluginLoaded();

  auto apis = svc.getExportedApiNames();
  bool hasFactory = false;
  bool hasSetMode = false;
  for (const auto& name : apis)
  {
    if (name == "g722.factory") hasFactory = true;
    if (name == "g722.setMode") hasSetMode = true;
  }
  CHECK(hasFactory);
  CHECK(hasSetMode);
}

TEST_CASE("G722CodecPlugin: exported factory API", "[g722][plugin]")
{
  auto& svc = ensurePluginLoaded();

  auto factory = svc.callExportedApi<std::shared_ptr<ICodecFactory>>("g722.factory");
  REQUIRE(factory != nullptr);
  CHECK(factory->codecInfo().name == "G722");
  CHECK(factory->codecInfo().clockRate == 8000);
  CHECK(factory->codecInfo().defaultPayloadType == 9);

  auto encoder = factory->createEncoder(factory->codecInfo());
  REQUIRE(encoder != nullptr);
  CHECK(encoder->info().name == "G722");
}

TEST_CASE("G722CodecPlugin: setMode configures factory", "[g722][plugin]")
{
  auto& svc = ensurePluginLoaded();

  // Set mode 1 = 48kbps
  svc.callExportedApi<void>("g722.setMode", std::uint32_t{1});

  auto factory = svc.callExportedApi<std::shared_ptr<ICodecFactory>>("g722.factory");
  REQUIRE(factory != nullptr);
  auto encoder = factory->createEncoder(factory->codecInfo());
  REQUIRE(encoder != nullptr);
  CHECK(encoder->getParameter("mode") == 48000);

  // Reset to default mode 3 = 64kbps
  svc.callExportedApi<void>("g722.setMode", std::uint32_t{3});
}

TEST_CASE("G722CodecPlugin: encode/decode via plugin-loaded factory", "[g722][plugin]")
{
  auto& svc = ensurePluginLoaded();

  auto factory = svc.callExportedApi<std::shared_ptr<ICodecFactory>>("g722.factory");
  REQUIRE(factory != nullptr);

  auto encoder = factory->createEncoder(factory->codecInfo());
  auto decoder = factory->createDecoder(factory->codecInfo());
  REQUIRE(encoder != nullptr);
  REQUIRE(decoder != nullptr);

  // Create a 20ms frame of S16 PCM at 16kHz mono (320 samples)
  constexpr std::size_t frameSamples = 320;
  auto pcmBuf = MediaBuffer::create(frameSamples * 2);
  auto* pcm = reinterpret_cast<std::int16_t*>(pcmBuf->data());
  for (std::size_t i = 0; i < frameSamples; ++i)
  {
    pcm[i] = static_cast<std::int16_t>(
      10000.0 * std::sin(2.0 * kPi * 1000.0 * static_cast<double>(i) / 16000.0));
  }
  pcmBuf->setSize(frameSamples * 2);
  pcmBuf->setTimestamp(1000);

  auto encoded = encoder->encode(*pcmBuf);
  REQUIRE(encoded != nullptr);
  CHECK(encoded->size() > 0);
  CHECK(encoded->timestamp() == 1000);

  auto decoded = decoder->decode(*encoded);
  REQUIRE(decoded != nullptr);
  CHECK(decoded->size() == frameSamples * 2);
  CHECK(decoded->timestamp() == 1000);
}
