#include <catch2/catch_test_macros.hpp>

#include <iora/iora.hpp>

#include "iora/codecs/codec/i_codec_factory.hpp"
#include "iora/codecs/core/media_buffer.hpp"

#include <cmath>
#include <cstdlib>
#include <dlfcn.h>
#include <filesystem>
#include <memory>
#include <string>

using namespace iora::codecs;

namespace {

std::string findPluginPath()
{
  auto exeDir = iora::util::getExecutableDir();
  auto candidate = iora::util::resolveRelativePath(exeDir, "../modules/g729/mod_g729.so");
  if (std::filesystem::exists(candidate))
  {
    return candidate;
  }
  candidate = exeDir + "/mod_g729.so";
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

iora::IoraService& ensurePluginLoaded()
{
  static bool initialized = false;
  if (!initialized)
  {
    iora::IoraService::Config config;
    config.server.port = 18748;
    config.log.file = "g729_plugin_test";
    config.log.level = "info";

    shutdownService();
    iora::IoraService::init(config);
    std::atexit(shutdownService);

    auto path = findPluginPath();
    if (path.empty())
    {
      throw std::runtime_error("mod_g729.so not found in build tree");
    }

    auto& svc = iora::IoraService::instanceRef();
    if (!svc.loadSingleModule(path))
    {
      throw std::runtime_error("Failed to load mod_g729.so from: " + path);
    }
    initialized = true;
  }
  return iora::IoraService::instanceRef();
}

} // namespace

TEST_CASE("G729CodecPlugin: module loads successfully", "[g729][plugin]")
{
  auto& svc = ensurePluginLoaded();
  auto factory = svc.callExportedApi<std::shared_ptr<ICodecFactory>>("g729.factory");
  REQUIRE(factory != nullptr);
}

TEST_CASE("G729CodecPlugin: factory has correct CodecInfo", "[g729][plugin]")
{
  auto& svc = ensurePluginLoaded();
  auto factory = svc.callExportedApi<std::shared_ptr<ICodecFactory>>("g729.factory");
  REQUIRE(factory != nullptr);

  const auto& info = factory->codecInfo();
  REQUIRE(info.name == "G729");
  REQUIRE(info.clockRate == 8000);
  REQUIRE(info.channels == 1);
  REQUIRE(info.defaultPayloadType == 18);
  REQUIRE(info.defaultBitrate == 8000);
}

TEST_CASE("G729CodecPlugin: encode/decode round-trip via plugin", "[g729][plugin]")
{
  auto& svc = ensurePluginLoaded();
  auto factory = svc.callExportedApi<std::shared_ptr<ICodecFactory>>("g729.factory");
  REQUIRE(factory != nullptr);

  auto info = factory->codecInfo();
  auto encoder = factory->createEncoder(info);
  auto decoder = factory->createDecoder(info);
  REQUIRE(encoder != nullptr);
  REQUIRE(decoder != nullptr);

  // Create 20ms frame (160 samples at 8kHz)
  constexpr int kSamples = 160;
  auto pcm = MediaBuffer::create(kSamples * 2);
  auto* samples = reinterpret_cast<int16_t*>(pcm->data());
  for (int i = 0; i < kSamples; ++i)
  {
    samples[i] = static_cast<int16_t>(5000.0 * std::sin(2.0 * 3.14159265 * 400.0 * i / 8000.0));
  }
  pcm->setSize(kSamples * 2);

  auto encoded = encoder->encode(*pcm);
  REQUIRE(encoded != nullptr);
  REQUIRE(encoded->size() == 20); // two 10-byte sub-frames

  auto decoded = decoder->decode(*encoded);
  REQUIRE(decoded != nullptr);
  REQUIRE(decoded->size() == kSamples * 2);

  // Verify energy in output
  auto* out = reinterpret_cast<const int16_t*>(decoded->data());
  double rms = 0;
  for (int i = 0; i < kSamples; ++i)
  {
    rms += static_cast<double>(out[i]) * out[i];
  }
  rms = std::sqrt(rms / kSamples);
  REQUIRE(rms > 10.0);
}

TEST_CASE("G729CodecPlugin: RTLD_LOCAL isolation — bcg729 symbols not global", "[g729][plugin]")
{
  // Ensure the plugin is loaded first
  ensurePluginLoaded();

  // bcg729 symbols must NOT be visible in the global namespace
  // because mod_g729.so is loaded with RTLD_LOCAL
  void* sym1 = dlsym(RTLD_DEFAULT, "initBcg729EncoderChannel");
  void* sym2 = dlsym(RTLD_DEFAULT, "bcg729Encoder");
  void* sym3 = dlsym(RTLD_DEFAULT, "initBcg729DecoderChannel");
  void* sym4 = dlsym(RTLD_DEFAULT, "bcg729Decoder");

  REQUIRE(sym1 == nullptr);
  REQUIRE(sym2 == nullptr);
  REQUIRE(sym3 == nullptr);
  REQUIRE(sym4 == nullptr);
}
