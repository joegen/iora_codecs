#include "ilbc_codec_factory.hpp"
#include "iora/codecs/codec/codec_registry.hpp"

#include <iora/iora.hpp>

#include <memory>

namespace iora {
namespace codecs {

/// Iora plugin that registers the iLBC codec factory.
class IlbcCodecPlugin : public IoraService::Plugin
{
public:
  explicit IlbcCodecPlugin(IoraService* service)
    : Plugin(service)
  {
  }

  void onLoad(IoraService* service) override
  {
    _factory = std::make_shared<IlbcCodecFactory>(IlbcCodecFactory::makeIlbcInfo());

    service->exportApi(*this, "ilbc.factory",
      [this]() -> std::shared_ptr<ICodecFactory> { return _factory; });

    service->exportApi(*this, "ilbc.setFrameMode",
      [this](std::uint32_t frameLenMs) { _factory->setDefaultFrameMode(frameLenMs); });

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
        registry.unregisterFactory("iLBC");
      }
      catch (const std::exception&)
      {
      }
    }
  }

private:
  std::shared_ptr<IlbcCodecFactory> _factory;
  bool _registeredWithRegistry = false;
};

} // namespace codecs
} // namespace iora

IORA_DECLARE_PLUGIN(iora::codecs::IlbcCodecPlugin)
