#include "opus_codec_factory.hpp"
#include "iora/codecs/codec/codec_registry.hpp"

#include <iora/iora.hpp>

#include <memory>

namespace iora {
namespace codecs {

/// Iora plugin that registers the Opus codec factory.
class OpusCodecPlugin : public IoraService::Plugin
{
public:
  explicit OpusCodecPlugin(IoraService* service)
    : Plugin(service)
  {
  }

  void onLoad(IoraService* service) override
  {
    _factory = std::make_shared<OpusCodecFactory>(OpusCodecFactory::makeOpusInfo());

    service->exportApi(*this, "opus.factory",
      [this]() -> std::shared_ptr<ICodecFactory> { return _factory; });

    service->exportApi(*this, "opus.setBitrate",
      [this](std::uint32_t bps) { _factory->setDefaultBitrate(bps); });

    service->exportApi(*this, "opus.setComplexity",
      [this](std::uint32_t complexity) { _factory->setDefaultComplexity(complexity); });

    service->exportApi(*this, "opus.enableFec",
      [this](bool enable) { _factory->setDefaultFec(enable); });

    service->exportApi(*this, "opus.enableDtx",
      [this](bool enable) { _factory->setDefaultDtx(enable); });

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
        registry.unregisterFactory("opus");
      }
      catch (const std::exception&)
      {
      }
    }
  }

private:
  std::shared_ptr<OpusCodecFactory> _factory;
  bool _registeredWithRegistry = false;
};

} // namespace codecs
} // namespace iora

IORA_DECLARE_PLUGIN(iora::codecs::OpusCodecPlugin)
