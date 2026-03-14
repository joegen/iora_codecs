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
  auto candidate = iora::util::resolveRelativePath(exeDir, "../modules/amr/mod_amr.so");
  if (std::filesystem::exists(candidate))
  {
    return candidate;
  }
  candidate = exeDir + "/mod_amr.so";
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

/// Initializes IoraService and loads the AMR plugin exactly once.
iora::IoraService& ensurePluginLoaded()
{
  static bool initialized = false;
  if (!initialized)
  {
    iora::IoraService::Config config;
    config.server.port = 18742;
    config.log.file = "amr_plugin_test";
    config.log.level = "info";

    shutdownService();
    iora::IoraService::init(config);
    std::atexit(shutdownService);

    auto& svc = iora::IoraService::instanceRef();

    auto pluginPath = findPluginPath();
    if (pluginPath.empty() || !svc.loadSingleModule(pluginPath))
    {
      throw std::runtime_error("Failed to load mod_amr.so from: " + pluginPath);
    }

    initialized = true;
  }
  return iora::IoraService::instanceRef();
}

} // anonymous namespace

TEST_CASE("AmrCodecPlugin: module loads successfully", "[amr][plugin]")
{
  auto& svc = ensurePluginLoaded();

  auto apis = svc.getExportedApiNames();
  bool hasNbFactory = false;
  bool hasWbFactory = false;
  bool hasNbSetMode = false;
  bool hasNbEnableDtx = false;
  bool hasWbSetMode = false;
  bool hasWbEnableDtx = false;
  for (const auto& name : apis)
  {
    if (name == "amr-nb.factory") hasNbFactory = true;
    if (name == "amr-wb.factory") hasWbFactory = true;
    if (name == "amr-nb.setMode") hasNbSetMode = true;
    if (name == "amr-nb.enableDtx") hasNbEnableDtx = true;
    if (name == "amr-wb.setMode") hasWbSetMode = true;
    if (name == "amr-wb.enableDtx") hasWbEnableDtx = true;
  }
  CHECK(hasNbFactory);
  CHECK(hasWbFactory);
  CHECK(hasNbSetMode);
  CHECK(hasNbEnableDtx);
  CHECK(hasWbSetMode);
  CHECK(hasWbEnableDtx);
}

TEST_CASE("AmrCodecPlugin: exported AMR-NB factory API", "[amr-nb][plugin]")
{
  auto& svc = ensurePluginLoaded();

  auto factory = svc.callExportedApi<std::shared_ptr<ICodecFactory>>("amr-nb.factory");
  REQUIRE(factory != nullptr);
  CHECK(factory->codecInfo().name == "AMR");
  CHECK(factory->codecInfo().clockRate == 8000);

  auto encoder = factory->createEncoder(factory->codecInfo());
  REQUIRE(encoder != nullptr);
  CHECK(encoder->info().name == "AMR");
}

TEST_CASE("AmrCodecPlugin: exported AMR-WB factory API", "[amr-wb][plugin]")
{
  auto& svc = ensurePluginLoaded();

  auto factory = svc.callExportedApi<std::shared_ptr<ICodecFactory>>("amr-wb.factory");
  REQUIRE(factory != nullptr);
  CHECK(factory->codecInfo().name == "AMR-WB");
  CHECK(factory->codecInfo().clockRate == 16000);

  auto encoder = factory->createEncoder(factory->codecInfo());
  REQUIRE(encoder != nullptr);
  CHECK(encoder->info().name == "AMR-WB");
}

TEST_CASE("AmrCodecPlugin: AMR-NB config APIs affect factory", "[amr-nb][plugin]")
{
  auto& svc = ensurePluginLoaded();

  // Configure AMR-NB: mode 0 (MR475 = 4.75kbps), DTX on
  svc.callExportedApi<void>("amr-nb.setMode", std::uint32_t{0});
  svc.callExportedApi<void>("amr-nb.enableDtx", true);

  auto factory = svc.callExportedApi<std::shared_ptr<ICodecFactory>>("amr-nb.factory");
  REQUIRE(factory != nullptr);
  auto encoder = factory->createEncoder(factory->codecInfo());
  REQUIRE(encoder != nullptr);
  CHECK(encoder->getParameter("bitrateMode") == 0);
  CHECK(encoder->getParameter("dtx") == 1);

  // Reset
  svc.callExportedApi<void>("amr-nb.setMode", std::uint32_t{7});
  svc.callExportedApi<void>("amr-nb.enableDtx", false);
}

TEST_CASE("AmrCodecPlugin: AMR-WB config APIs affect factory", "[amr-wb][plugin]")
{
  auto& svc = ensurePluginLoaded();

  // Configure AMR-WB: mode 4, DTX on
  svc.callExportedApi<void>("amr-wb.setMode", std::uint32_t{4});
  svc.callExportedApi<void>("amr-wb.enableDtx", true);

  auto factory = svc.callExportedApi<std::shared_ptr<ICodecFactory>>("amr-wb.factory");
  REQUIRE(factory != nullptr);
  auto encoder = factory->createEncoder(factory->codecInfo());
  REQUIRE(encoder != nullptr);
  CHECK(encoder->getParameter("bitrateMode") == 4);
  CHECK(encoder->getParameter("dtx") == 1);

  // Reset
  svc.callExportedApi<void>("amr-wb.setMode", std::uint32_t{8});
  svc.callExportedApi<void>("amr-wb.enableDtx", false);
}

TEST_CASE("AmrCodecPlugin: AMR-NB encode/decode via plugin", "[amr-nb][plugin]")
{
  auto& svc = ensurePluginLoaded();

  auto factory = svc.callExportedApi<std::shared_ptr<ICodecFactory>>("amr-nb.factory");
  REQUIRE(factory != nullptr);

  auto encoder = factory->createEncoder(factory->codecInfo());
  auto decoder = factory->createDecoder(factory->codecInfo());
  REQUIRE(encoder != nullptr);
  REQUIRE(decoder != nullptr);

  // 20ms at 8kHz = 160 samples
  constexpr std::size_t frameSamples = 160;
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

TEST_CASE("AmrCodecPlugin: AMR-WB encode/decode via plugin", "[amr-wb][plugin]")
{
  auto& svc = ensurePluginLoaded();

  auto factory = svc.callExportedApi<std::shared_ptr<ICodecFactory>>("amr-wb.factory");
  REQUIRE(factory != nullptr);

  auto encoder = factory->createEncoder(factory->codecInfo());
  auto decoder = factory->createDecoder(factory->codecInfo());
  REQUIRE(encoder != nullptr);
  REQUIRE(decoder != nullptr);

  // 20ms at 16kHz = 320 samples
  constexpr std::size_t frameSamples = 320;
  auto pcmBuf = MediaBuffer::create(frameSamples * 2);
  auto* pcm = reinterpret_cast<std::int16_t*>(pcmBuf->data());
  for (std::size_t i = 0; i < frameSamples; ++i)
  {
    pcm[i] = static_cast<std::int16_t>(
      10000.0 * std::sin(2.0 * kPi * 400.0 * static_cast<double>(i) / 16000.0));
  }
  pcmBuf->setSize(frameSamples * 2);
  pcmBuf->setTimestamp(2000);

  auto encoded = encoder->encode(*pcmBuf);
  REQUIRE(encoded != nullptr);
  CHECK(encoded->size() > 0);
  CHECK(encoded->timestamp() == 2000);

  auto decoded = decoder->decode(*encoded);
  REQUIRE(decoded != nullptr);
  CHECK(decoded->size() == frameSamples * 2);
  CHECK(decoded->timestamp() == 2000);
}
