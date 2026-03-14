#include "g711_codec_factory.hpp"
#include "iora/codecs/codec/codec_registry.hpp"

#include <iora/iora.hpp>

#include <memory>

namespace iora {
namespace codecs {

/// Iora plugin that registers G.711 PCMU and PCMA codec factories.
class G711CodecPlugin : public IoraService::Plugin
{
public:
  explicit G711CodecPlugin(IoraService* service)
    : Plugin(service)
  {
  }

  void onLoad(IoraService* service) override
  {
    _pcmuFactory = std::make_shared<G711CodecFactory>(G711CodecFactory::makePcmuInfo());
    _pcmaFactory = std::make_shared<G711CodecFactory>(G711CodecFactory::makePcmaInfo());

    service->exportApi(*this, "g711.pcmu.factory",
      [this]() -> std::shared_ptr<ICodecFactory> { return _pcmuFactory; });

    service->exportApi(*this, "g711.pcma.factory",
      [this]() -> std::shared_ptr<ICodecFactory> { return _pcmaFactory; });

    // Register with CodecRegistry if available
    try
    {
      auto& registry = service->callExportedApi<CodecRegistry&>("codecs.registry");
      registry.registerFactory(_pcmuFactory);
      registry.registerFactory(_pcmaFactory);
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
        registry.unregisterFactory("PCMU");
        registry.unregisterFactory("PCMA");
      }
      catch (const std::exception&)
      {
      }
    }
  }

private:
  std::shared_ptr<G711CodecFactory> _pcmuFactory;
  std::shared_ptr<G711CodecFactory> _pcmaFactory;
  bool _registeredWithRegistry = false;
};

} // namespace codecs
} // namespace iora

IORA_DECLARE_PLUGIN(iora::codecs::G711CodecPlugin)
