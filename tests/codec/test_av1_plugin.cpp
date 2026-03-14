#include <catch2/catch_test_macros.hpp>

#include <iora/iora.hpp>

#include "iora/codecs/codec/i_codec_factory.hpp"
#include "iora/codecs/core/media_buffer.hpp"

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>

using namespace iora::codecs;

namespace {

std::string findPluginPath()
{
  auto exeDir = iora::util::getExecutableDir();
  auto candidate = iora::util::resolveRelativePath(exeDir, "../modules/av1/mod_av1.so");
  if (std::filesystem::exists(candidate))
  {
    return candidate;
  }
  candidate = exeDir + "/mod_av1.so";
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

/// Initializes IoraService and loads the AV1 plugin exactly once.
iora::IoraService& ensurePluginLoaded()
{
  static bool initialized = false;
  if (!initialized)
  {
    iora::IoraService::Config config;
    config.server.port = 18747;
    config.log.file = "av1_plugin_test";
    config.log.level = "info";

    shutdownService();
    iora::IoraService::init(config);
    std::atexit(shutdownService);

    auto& svc = iora::IoraService::instanceRef();

    auto pluginPath = findPluginPath();
    if (pluginPath.empty() || !svc.loadSingleModule(pluginPath))
    {
      throw std::runtime_error("Failed to load mod_av1.so from: " + pluginPath);
    }

    initialized = true;
  }
  return iora::IoraService::instanceRef();
}

/// Create a simple I420 frame for testing.
std::shared_ptr<MediaBuffer> makeI420Frame(std::uint32_t width, std::uint32_t height)
{
  std::size_t ySize = static_cast<std::size_t>(width) * height;
  std::size_t uvSize = static_cast<std::size_t>(width / 2) * (height / 2);
  std::size_t totalSize = ySize + uvSize * 2;
  auto buf = MediaBuffer::create(totalSize);

  std::uint8_t* data = buf->data();

  // Y plane: horizontal gradient
  for (std::uint32_t row = 0; row < height; ++row)
  {
    for (std::uint32_t col = 0; col < width; ++col)
    {
      data[row * width + col] = static_cast<std::uint8_t>((col * 255) / (width - 1));
    }
  }

  // U/V planes: neutral 128
  std::memset(data + ySize, 128, uvSize * 2);

  buf->setSize(totalSize);
  return buf;
}

} // anonymous namespace

TEST_CASE("Av1CodecPlugin: module loads successfully", "[av1][plugin]")
{
  auto& svc = ensurePluginLoaded();

  auto apis = svc.getExportedApiNames();
  bool hasFactory = false;
  bool hasSetVideoParams = false;
  bool hasSetBitrate = false;
  bool hasSetSpeed = false;
  bool hasRequestKeyFrame = false;
  for (const auto& name : apis)
  {
    if (name == "av1.factory") hasFactory = true;
    if (name == "av1.setVideoParams") hasSetVideoParams = true;
    if (name == "av1.setBitrate") hasSetBitrate = true;
    if (name == "av1.setSpeed") hasSetSpeed = true;
    if (name == "av1.requestKeyFrame") hasRequestKeyFrame = true;
  }
  CHECK(hasFactory);
  CHECK(hasSetVideoParams);
  CHECK(hasSetBitrate);
  CHECK(hasSetSpeed);
  CHECK(hasRequestKeyFrame);
}

TEST_CASE("Av1CodecPlugin: factory API", "[av1][plugin]")
{
  auto& svc = ensurePluginLoaded();

  auto factory = svc.callExportedApi<std::shared_ptr<ICodecFactory>>("av1.factory");
  REQUIRE(factory != nullptr);
  CHECK(factory->codecInfo().name == "AV1");
  CHECK(factory->codecInfo().clockRate == 90000);
  CHECK(factory->codecInfo().type == CodecType::Video);

  auto encoder = factory->createEncoder(factory->codecInfo());
  REQUIRE(encoder != nullptr);
  CHECK(encoder->info().name == "AV1");

  auto decoder = factory->createDecoder(factory->codecInfo());
  REQUIRE(decoder != nullptr);
  CHECK(decoder->info().name == "AV1");
}

TEST_CASE("Av1CodecPlugin: encode/decode via plugin", "[av1][plugin]")
{
  auto& svc = ensurePluginLoaded();

  auto factory = svc.callExportedApi<std::shared_ptr<ICodecFactory>>("av1.factory");
  REQUIRE(factory != nullptr);

  auto encoder = factory->createEncoder(factory->codecInfo());
  auto decoder = factory->createDecoder(factory->codecInfo());
  REQUIRE(encoder != nullptr);
  REQUIRE(decoder != nullptr);

  auto frame = makeI420Frame(640, 480);
  frame->setTimestamp(90000);

  std::shared_ptr<MediaBuffer> decoded;
  for (int i = 0; i < 15; ++i)
  {
    auto encoded = encoder->encode(*frame);
    if (encoded != nullptr)
    {
      CHECK(encoded->timestamp() == 90000);
      decoded = decoder->decode(*encoded);
      if (decoded != nullptr)
      {
        break;
      }
    }
  }

  REQUIRE(decoded != nullptr);
  CHECK(decoded->size() == 640u * 480 * 3 / 2);
  CHECK(decoded->timestamp() == 90000);
}

TEST_CASE("Av1CodecPlugin: setVideoParams configures factory", "[av1][plugin]")
{
  auto& svc = ensurePluginLoaded();

  // Configure for 320x240
  svc.callExportedApi<void>("av1.setVideoParams",
    std::uint32_t{320}, std::uint32_t{240},
    std::uint32_t{200000}, float{30.0f});

  auto factory = svc.callExportedApi<std::shared_ptr<ICodecFactory>>("av1.factory");
  REQUIRE(factory != nullptr);

  auto encoder = factory->createEncoder(factory->codecInfo());
  REQUIRE(encoder != nullptr);

  auto frame = makeI420Frame(320, 240);
  auto encoded = encoder->encode(*frame);
  REQUIRE(encoded != nullptr);
  CHECK(encoded->size() > 0);

  // Restore default for other tests
  svc.callExportedApi<void>("av1.setVideoParams",
    std::uint32_t{640}, std::uint32_t{480},
    std::uint32_t{300000}, float{30.0f});
}

TEST_CASE("Av1CodecPlugin: setBitrate configures factory", "[av1][plugin]")
{
  auto& svc = ensurePluginLoaded();

  svc.callExportedApi<void>("av1.setBitrate", std::uint32_t{200000});

  auto factory = svc.callExportedApi<std::shared_ptr<ICodecFactory>>("av1.factory");
  REQUIRE(factory != nullptr);

  auto encoder = factory->createEncoder(factory->codecInfo());
  REQUIRE(encoder != nullptr);

  auto frame = makeI420Frame(640, 480);
  auto encoded = encoder->encode(*frame);
  REQUIRE(encoded != nullptr);
  CHECK(encoded->size() > 0);

  // Restore default
  svc.callExportedApi<void>("av1.setBitrate", std::uint32_t{300000});
}
