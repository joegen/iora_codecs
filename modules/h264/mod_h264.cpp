#include "h264_codec_factory.hpp"
#include "iora/codecs/codec/codec_registry.hpp"

#include <iora/iora.hpp>

#include <memory>

namespace iora {
namespace codecs {

/// Iora plugin that registers the H.264 codec factory.
class H264CodecPlugin : public IoraService::Plugin
{
public:
  explicit H264CodecPlugin(IoraService* service)
    : Plugin(service)
  {
  }

  void onLoad(IoraService* service) override
  {
    _factory = std::make_shared<H264CodecFactory>(H264CodecFactory::makeH264Info());

    service->exportApi(*this, "h264.factory",
      [this]() -> std::shared_ptr<ICodecFactory> { return _factory; });

    service->exportApi(*this, "h264.setVideoParams",
      [this](std::uint32_t width, std::uint32_t height,
             std::uint32_t bitrate, float framerate) {
        _factory->setVideoParams(width, height, bitrate, framerate);
      });

    service->exportApi(*this, "h264.setBitrate",
      [this](std::uint32_t bps) { _factory->setDefaultBitrate(bps); });

    service->exportApi(*this, "h264.setProfile",
      [this](std::uint32_t profileIdc) { _factory->setDefaultProfile(profileIdc); });

    service->exportApi(*this, "h264.requestKeyFrame",
      [this]() { _factory->requestKeyFrame(); });

    // Register with CodecRegistry if available
    try
    {
      auto& registry = service->callExportedApi<CodecRegistry&>("codecs.registry");
      registry.registerFactory(_factory);
      _registeredWithRegistry = true;
    }
    catch (const std::exception&)
    {
      // CodecRegistry not yet exported — caller will register manually
    }
  }

  void onUnload() override
  {
    if (_registeredWithRegistry)
    {
      try
      {
        auto& registry = service()->callExportedApi<CodecRegistry&>("codecs.registry");
        registry.unregisterFactory("H264");
      }
      catch (const std::exception&)
      {
      }
    }
  }

private:
  std::shared_ptr<H264CodecFactory> _factory;
  bool _registeredWithRegistry = false;
};

} // namespace codecs
} // namespace iora

IORA_DECLARE_PLUGIN(iora::codecs::H264CodecPlugin)
