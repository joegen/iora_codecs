#include "iora/codecs/codec/codec_registry.hpp"

#include <iora/iora.hpp>

#include <memory>

namespace iora {
namespace codecs {

/// Iora plugin that owns the central CodecRegistry and exports it
/// as "codecs.registry". Load this module before any codec modules
/// so they can auto-register their factories.
class CodecRegistryPlugin : public IoraService::Plugin
{
public:
  explicit CodecRegistryPlugin(IoraService* service)
    : Plugin(service)
  {
  }

  void onLoad(IoraService* service) override
  {
    _registry = std::make_shared<CodecRegistry>();

    service->exportApi(*this, "codecs.registry",
      [this]() -> CodecRegistry& { return *_registry; });

    service->exportApi(*this, "codecs.registry.enumerate",
      [this]() -> std::vector<CodecInfo> { return _registry->enumerateCodecs(); });

    service->exportApi(*this, "codecs.registry.findByName",
      [this](std::string name) -> std::optional<CodecInfo> {
        return _registry->findByName(name);
      });

    service->exportApi(*this, "codecs.registry.findByPayloadType",
      [this](std::uint8_t pt) -> std::optional<CodecInfo> {
        return _registry->findByPayloadType(pt);
      });
  }

  void onUnload() override
  {
    _registry.reset();
  }

private:
  std::shared_ptr<CodecRegistry> _registry;
};

} // namespace codecs
} // namespace iora

IORA_DECLARE_PLUGIN(iora::codecs::CodecRegistryPlugin)
