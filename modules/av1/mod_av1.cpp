#include "av1_codec_factory.hpp"
#include "iora/codecs/codec/codec_registry.hpp"

#include <iora/iora.hpp>

#include <memory>

namespace iora {
namespace codecs {

/// Iora plugin that registers AV1 codec factory.
class Av1CodecPlugin : public IoraService::Plugin
{
public:
  explicit Av1CodecPlugin(IoraService* service)
    : Plugin(service)
  {
  }

  void onLoad(IoraService* service) override
  {
    _factory = std::make_shared<Av1CodecFactory>(Av1CodecFactory::makeAv1Info());

    // Factory export
    service->exportApi(*this, "av1.factory",
      [this]() -> std::shared_ptr<ICodecFactory> { return _factory; });

    // Config APIs
    service->exportApi(*this, "av1.setVideoParams",
      [this](std::uint32_t width, std::uint32_t height,
             std::uint32_t bitrate, float framerate) {
        _factory->setVideoParams(width, height, bitrate, framerate);
      });

    service->exportApi(*this, "av1.setBitrate",
      [this](std::uint32_t bps) {
        _factory->setDefaultBitrate(bps);
      });

    service->exportApi(*this, "av1.setSpeed",
      [this](std::uint32_t speed) {
        _factory->setDefaultSpeed(speed);
      });

    service->exportApi(*this, "av1.requestKeyFrame",
      [this]() {
        _factory->requestKeyFrame();
      });

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
        registry.unregisterFactory("AV1");
      }
      catch (const std::exception&)
      {
      }
    }
  }

private:
  std::shared_ptr<Av1CodecFactory> _factory;
  bool _registeredWithRegistry = false;
};

} // namespace codecs
} // namespace iora

IORA_DECLARE_PLUGIN(iora::codecs::Av1CodecPlugin)
