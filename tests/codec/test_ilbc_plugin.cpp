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
  auto candidate = iora::util::resolveRelativePath(exeDir, "../modules/ilbc/mod_ilbc.so");
  if (std::filesystem::exists(candidate))
  {
    return candidate;
  }
  candidate = exeDir + "/mod_ilbc.so";
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

/// Initializes IoraService and loads the iLBC plugin exactly once.
iora::IoraService& ensurePluginLoaded()
{
  static bool initialized = false;
  if (!initialized)
  {
    iora::IoraService::Config config;
    config.server.port = 18732;
    config.log.file = "ilbc_plugin_test";
    config.log.level = "info";

    shutdownService();
    iora::IoraService::init(config);
    std::atexit(shutdownService);

    auto& svc = iora::IoraService::instanceRef();

    auto pluginPath = findPluginPath();
    if (pluginPath.empty() || !svc.loadSingleModule(pluginPath))
    {
      throw std::runtime_error("Failed to load mod_ilbc.so from: " + pluginPath);
    }

    initialized = true;
  }
  return iora::IoraService::instanceRef();
}

} // anonymous namespace

TEST_CASE("IlbcCodecPlugin: module loads successfully", "[ilbc][plugin]")
{
  auto& svc = ensurePluginLoaded();

  auto apis = svc.getExportedApiNames();
  bool hasFactory = false;
  bool hasSetFrameMode = false;
  for (const auto& name : apis)
  {
    if (name == "ilbc.factory") hasFactory = true;
    if (name == "ilbc.setFrameMode") hasSetFrameMode = true;
  }
  CHECK(hasFactory);
  CHECK(hasSetFrameMode);
}

TEST_CASE("IlbcCodecPlugin: exported factory API", "[ilbc][plugin]")
{
  auto& svc = ensurePluginLoaded();

  auto factory = svc.callExportedApi<std::shared_ptr<ICodecFactory>>("ilbc.factory");
  REQUIRE(factory != nullptr);
  CHECK(factory->codecInfo().name == "iLBC");
  CHECK(factory->codecInfo().clockRate == 8000);
  CHECK(factory->codecInfo().channels == 1);

  auto encoder = factory->createEncoder(factory->codecInfo());
  REQUIRE(encoder != nullptr);
  CHECK(encoder->info().name == "iLBC");
}

TEST_CASE("IlbcCodecPlugin: setFrameMode configures factory", "[ilbc][plugin]")
{
  auto& svc = ensurePluginLoaded();

  // Switch to 20ms mode
  svc.callExportedApi<void>("ilbc.setFrameMode", std::uint32_t{20});

  auto factory = svc.callExportedApi<std::shared_ptr<ICodecFactory>>("ilbc.factory");
  REQUIRE(factory != nullptr);
  auto encoder = factory->createEncoder(factory->codecInfo());
  REQUIRE(encoder != nullptr);
  CHECK(encoder->getParameter("frameMode") == 20);

  // Reset to 30ms mode for other tests
  svc.callExportedApi<void>("ilbc.setFrameMode", std::uint32_t{30});
}

TEST_CASE("IlbcCodecPlugin: encode/decode via plugin-loaded factory", "[ilbc][plugin]")
{
  auto& svc = ensurePluginLoaded();

  auto factory = svc.callExportedApi<std::shared_ptr<ICodecFactory>>("ilbc.factory");
  REQUIRE(factory != nullptr);

  auto encoder = factory->createEncoder(factory->codecInfo());
  auto decoder = factory->createDecoder(factory->codecInfo());
  REQUIRE(encoder != nullptr);
  REQUIRE(decoder != nullptr);

  // Create a 30ms frame of S16 PCM at 8kHz mono (240 samples)
  constexpr std::size_t frameSamples = 240;
  auto pcmBuf = MediaBuffer::create(frameSamples * 2);
  auto* pcm = reinterpret_cast<std::int16_t*>(pcmBuf->data());
  for (std::size_t i = 0; i < frameSamples; ++i)
  {
    pcm[i] = static_cast<std::int16_t>(
      10000.0 * std::sin(2.0 * kPi * 400.0 * static_cast<double>(i) / 8000.0));
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
