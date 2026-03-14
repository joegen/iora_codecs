#include "amr_nb_codec_factory.hpp"
#include "amr_wb_codec_factory.hpp"
#include "iora/codecs/codec/codec_registry.hpp"

#include <iora/iora.hpp>

#include <memory>

namespace iora {
namespace codecs {

/// Iora plugin that registers the AMR-NB and AMR-WB codec factories.
class AmrCodecPlugin : public IoraService::Plugin
{
public:
  explicit AmrCodecPlugin(IoraService* service)
    : Plugin(service)
  {
  }

  void onLoad(IoraService* service) override
  {
    _nbFactory = std::make_shared<AmrNbCodecFactory>(AmrNbCodecFactory::makeAmrNbInfo());
    _wbFactory = std::make_shared<AmrWbCodecFactory>(AmrWbCodecFactory::makeAmrWbInfo());

    service->exportApi(*this, "amr-nb.factory",
      [this]() -> std::shared_ptr<ICodecFactory> { return _nbFactory; });

    service->exportApi(*this, "amr-wb.factory",
      [this]() -> std::shared_ptr<ICodecFactory> { return _wbFactory; });

    service->exportApi(*this, "amr-nb.setMode",
      [this](std::uint32_t bitrateMode) { _nbFactory->setDefaultMode(bitrateMode); });

    service->exportApi(*this, "amr-nb.enableDtx",
      [this](bool enable) { _nbFactory->setDefaultDtx(enable); });

    service->exportApi(*this, "amr-wb.setMode",
      [this](std::uint32_t bitrateMode) { _wbFactory->setDefaultMode(bitrateMode); });

    service->exportApi(*this, "amr-wb.enableDtx",
      [this](bool enable) { _wbFactory->setDefaultDtx(enable); });

    // Register with CodecRegistry if available
    try
    {
      auto& registry = service->callExportedApi<CodecRegistry&>("codecs.registry");
      registry.registerFactory(_nbFactory);
      registry.registerFactory(_wbFactory);
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
        registry.unregisterFactory("AMR");
        registry.unregisterFactory("AMR-WB");
      }
      catch (const std::exception&)
      {
      }
    }
  }

private:
  std::shared_ptr<AmrNbCodecFactory> _nbFactory;
  std::shared_ptr<AmrWbCodecFactory> _wbFactory;
  bool _registeredWithRegistry = false;
};

} // namespace codecs
} // namespace iora

IORA_DECLARE_PLUGIN(iora::codecs::AmrCodecPlugin)
