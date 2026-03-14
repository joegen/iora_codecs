#include "vpx_codec_factory.hpp"
#include "iora/codecs/codec/codec_registry.hpp"

#include <iora/iora.hpp>

#include <memory>

namespace iora {
namespace codecs {

/// Iora plugin that registers VP8 and VP9 codec factories.
class VpxCodecPlugin : public IoraService::Plugin
{
public:
  explicit VpxCodecPlugin(IoraService* service)
    : Plugin(service)
  {
  }

  void onLoad(IoraService* service) override
  {
    _vp8Factory = std::make_shared<VpxCodecFactory>(VpxCodecFactory::makeVp8Info(),
                                                     VpxVariant::VP8);
    _vp9Factory = std::make_shared<VpxCodecFactory>(VpxCodecFactory::makeVp9Info(),
                                                     VpxVariant::VP9);

    // Per-variant factory exports (AMR pattern)
    service->exportApi(*this, "vpx.vp8.factory",
      [this]() -> std::shared_ptr<ICodecFactory> { return _vp8Factory; });

    service->exportApi(*this, "vpx.vp9.factory",
      [this]() -> std::shared_ptr<ICodecFactory> { return _vp9Factory; });

    // Config APIs — apply to both factories
    service->exportApi(*this, "vpx.setVideoParams",
      [this](std::uint32_t width, std::uint32_t height,
             std::uint32_t bitrate, float framerate) {
        _vp8Factory->setVideoParams(width, height, bitrate, framerate);
        _vp9Factory->setVideoParams(width, height, bitrate, framerate);
      });

    service->exportApi(*this, "vpx.setBitrate",
      [this](std::uint32_t bps) {
        _vp8Factory->setDefaultBitrate(bps);
        _vp9Factory->setDefaultBitrate(bps);
      });

    service->exportApi(*this, "vpx.setSpeed",
      [this](std::uint32_t speed) {
        _vp8Factory->setDefaultSpeed(speed);
        _vp9Factory->setDefaultSpeed(speed);
      });

    service->exportApi(*this, "vpx.requestKeyFrame",
      [this]() {
        _vp8Factory->requestKeyFrame();
        _vp9Factory->requestKeyFrame();
      });

    // Register with CodecRegistry if available
    try
    {
      auto& registry = service->callExportedApi<CodecRegistry&>("codecs.registry");
      registry.registerFactory(_vp8Factory);
      registry.registerFactory(_vp9Factory);
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
        registry.unregisterFactory("VP8");
        registry.unregisterFactory("VP9");
      }
      catch (const std::exception&)
      {
      }
    }
  }

private:
  std::shared_ptr<VpxCodecFactory> _vp8Factory;
  std::shared_ptr<VpxCodecFactory> _vp9Factory;
  bool _registeredWithRegistry = false;
};

} // namespace codecs
} // namespace iora

IORA_DECLARE_PLUGIN(iora::codecs::VpxCodecPlugin)
