#include "g722_codec_factory.hpp"
#include "iora/codecs/codec/codec_registry.hpp"

#include <iora/iora.hpp>

#include <memory>

namespace iora {
namespace codecs {

/// Iora plugin that registers the G.722 codec factory.
class G722CodecPlugin : public IoraService::Plugin
{
public:
  explicit G722CodecPlugin(IoraService* service)
    : Plugin(service)
  {
  }

  void onLoad(IoraService* service) override
  {
    _factory = std::make_shared<G722CodecFactory>(G722CodecFactory::makeG722Info());

    service->exportApi(*this, "g722.factory",
      [this]() -> std::shared_ptr<ICodecFactory> { return _factory; });

    service->exportApi(*this, "g722.setMode",
      [this](std::uint32_t mode) { _factory->setDefaultMode(mode); });

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
        registry.unregisterFactory("G722");
      }
      catch (const std::exception&)
      {
      }
    }
  }

private:
  std::shared_ptr<G722CodecFactory> _factory;
  bool _registeredWithRegistry = false;
};

} // namespace codecs
} // namespace iora

IORA_DECLARE_PLUGIN(iora::codecs::G722CodecPlugin)
