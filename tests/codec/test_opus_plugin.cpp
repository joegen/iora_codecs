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
  auto candidate = iora::util::resolveRelativePath(exeDir, "../modules/opus/mod_opus.so");
  if (std::filesystem::exists(candidate))
  {
    return candidate;
  }
  candidate = exeDir + "/mod_opus.so";
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

/// Initializes IoraService and loads the Opus plugin exactly once.
/// Safe to call from any test in any order.
iora::IoraService& ensurePluginLoaded()
{
  static bool initialized = false;
  if (!initialized)
  {
    iora::IoraService::Config config;
    config.server.port = 18712;
    config.log.file = "opus_plugin_test";
    config.log.level = "info";

    shutdownService();
    iora::IoraService::init(config);
    std::atexit(shutdownService);

    auto& svc = iora::IoraService::instanceRef();

    auto pluginPath = findPluginPath();
    if (pluginPath.empty() || !svc.loadSingleModule(pluginPath))
    {
      throw std::runtime_error("Failed to load mod_opus.so from: " + pluginPath);
    }

    initialized = true;
  }
  return iora::IoraService::instanceRef();
}

} // anonymous namespace

TEST_CASE("OpusCodecPlugin: module loads successfully", "[opus][plugin]")
{
  auto& svc = ensurePluginLoaded();

  auto apis = svc.getExportedApiNames();
  bool hasFactory = false;
  bool hasSetBitrate = false;
  bool hasSetComplexity = false;
  bool hasEnableFec = false;
  bool hasEnableDtx = false;
  for (const auto& name : apis)
  {
    if (name == "opus.factory") hasFactory = true;
    if (name == "opus.setBitrate") hasSetBitrate = true;
    if (name == "opus.setComplexity") hasSetComplexity = true;
    if (name == "opus.enableFec") hasEnableFec = true;
    if (name == "opus.enableDtx") hasEnableDtx = true;
  }
  CHECK(hasFactory);
  CHECK(hasSetBitrate);
  CHECK(hasSetComplexity);
  CHECK(hasEnableFec);
  CHECK(hasEnableDtx);
}

TEST_CASE("OpusCodecPlugin: exported factory API", "[opus][plugin]")
{
  auto& svc = ensurePluginLoaded();

  auto factory = svc.callExportedApi<std::shared_ptr<ICodecFactory>>("opus.factory");
  REQUIRE(factory != nullptr);
  CHECK(factory->codecInfo().name == "opus");
  CHECK(factory->codecInfo().clockRate == 48000);
  CHECK(factory->codecInfo().defaultPayloadType == 111);

  auto encoder = factory->createEncoder(factory->codecInfo());
  REQUIRE(encoder != nullptr);
  CHECK(encoder->info().name == "opus");
}

TEST_CASE("OpusCodecPlugin: config APIs affect factory defaults", "[opus][plugin]")
{
  auto& svc = ensurePluginLoaded();

  // Configure defaults via exported APIs
  svc.callExportedApi<void>("opus.setBitrate", std::uint32_t{32000});
  svc.callExportedApi<void>("opus.setComplexity", std::uint32_t{10});
  svc.callExportedApi<void>("opus.enableFec", true);
  svc.callExportedApi<void>("opus.enableDtx", true);

  // Create encoder — should have the configured defaults
  auto factory = svc.callExportedApi<std::shared_ptr<ICodecFactory>>("opus.factory");
  REQUIRE(factory != nullptr);
  auto encoder = factory->createEncoder(factory->codecInfo());
  REQUIRE(encoder != nullptr);

  CHECK(encoder->getParameter("bitrate") == 32000);
  CHECK(encoder->getParameter("complexity") == 10);
  CHECK(encoder->getParameter("fec") == 1);
  CHECK(encoder->getParameter("dtx") == 1);

  // Reset to defaults for other tests
  svc.callExportedApi<void>("opus.setComplexity", std::uint32_t{5});
  svc.callExportedApi<void>("opus.enableFec", false);
  svc.callExportedApi<void>("opus.enableDtx", false);
}

TEST_CASE("OpusCodecPlugin: encode/decode via plugin-loaded factory", "[opus][plugin]")
{
  auto& svc = ensurePluginLoaded();

  auto factory = svc.callExportedApi<std::shared_ptr<ICodecFactory>>("opus.factory");
  REQUIRE(factory != nullptr);

  auto encoder = factory->createEncoder(factory->codecInfo());
  auto decoder = factory->createDecoder(factory->codecInfo());
  REQUIRE(encoder != nullptr);
  REQUIRE(decoder != nullptr);

  // Create a 20ms frame of S16 PCM at 48kHz stereo (960 samples * 2 channels)
  constexpr std::size_t frameSamples = 960;
  constexpr int channels = 2;
  constexpr std::size_t totalSamples = frameSamples * channels;
  auto pcmBuf = MediaBuffer::create(totalSamples * 2);
  auto* pcm = reinterpret_cast<std::int16_t*>(pcmBuf->data());
  for (std::size_t i = 0; i < frameSamples; ++i)
  {
    auto val = static_cast<std::int16_t>(
      10000.0 * std::sin(2.0 * kPi * 440.0 * static_cast<double>(i) / 48000.0));
    pcm[i * channels] = val;
    pcm[i * channels + 1] = val;
  }
  pcmBuf->setSize(totalSamples * 2);
  pcmBuf->setTimestamp(1000);

  auto encoded = encoder->encode(*pcmBuf);
  REQUIRE(encoded != nullptr);
  CHECK(encoded->size() > 0);
  CHECK(encoded->timestamp() == 1000);

  auto decoded = decoder->decode(*encoded);
  REQUIRE(decoded != nullptr);
  CHECK(decoded->size() == totalSamples * 2);
  CHECK(decoded->timestamp() == 1000);
}
