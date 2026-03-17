#include "g729_codec_factory.hpp"
#include "iora/codecs/codec/codec_registry.hpp"

#include <iora/iora.hpp>

#include <memory>

namespace iora {
namespace codecs {

/// Iora plugin that registers the G.729 Annex A codec factory.
///
/// GPL-3.0 notice: This module links bcg729 (GPL-3.0). The GPL scope
/// is confined to this shared library (mod_g729.so) only. It is loaded
/// with RTLD_LOCAL for symbol isolation.
class G729CodecPlugin : public IoraService::Plugin
{
public:
  explicit G729CodecPlugin(IoraService* service)
    : Plugin(service)
  {
  }

  void onLoad(IoraService* service) override
  {
    _factory = std::make_shared<G729CodecFactory>(G729CodecFactory::makeG729Info());

    service->exportApi(*this, "g729.factory",
      [this]() -> std::shared_ptr<ICodecFactory> { return _factory; });

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
        registry.unregisterFactory("G729");
      }
      catch (const std::exception&)
      {
      }
    }
  }

private:
  std::shared_ptr<G729CodecFactory> _factory;
  bool _registeredWithRegistry = false;
};

} // namespace codecs
} // namespace iora

IORA_DECLARE_PLUGIN(iora::codecs::G729CodecPlugin)
