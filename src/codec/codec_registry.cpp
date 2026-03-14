#include "iora/codecs/codec/codec_registry.hpp"

#include <stdexcept>

namespace iora {
namespace codecs {

void CodecRegistry::registerFactory(std::shared_ptr<ICodecFactory> factory)
{
  if (!factory)
  {
    throw std::invalid_argument("CodecRegistry::registerFactory: factory is null");
  }

  const auto& name = factory->codecInfo().name;
  std::lock_guard<std::mutex> lock(_mutex);

  if (_factories.count(name))
  {
    throw std::runtime_error(
      "CodecRegistry::registerFactory: factory already registered for '" + name + "'");
  }

  _factories.emplace(name, std::move(factory));
}

void CodecRegistry::unregisterFactory(const std::string& name)
{
  std::lock_guard<std::mutex> lock(_mutex);
  _factories.erase(name);
}

std::unique_ptr<ICodec> CodecRegistry::createEncoder(const CodecInfo& info)
{
  // Copy factory shared_ptr under the lock, then call outside the lock
  // to avoid deadlock if the factory calls back into the registry.
  std::shared_ptr<ICodecFactory> factory;
  {
    std::lock_guard<std::mutex> lock(_mutex);
    for (const auto& [name, f] : _factories)
    {
      if (f->supports(info))
      {
        factory = f;
        break;
      }
    }
  }
  return factory ? factory->createEncoder(info) : nullptr;
}

std::unique_ptr<ICodec> CodecRegistry::createDecoder(const CodecInfo& info)
{
  std::shared_ptr<ICodecFactory> factory;
  {
    std::lock_guard<std::mutex> lock(_mutex);
    for (const auto& [name, f] : _factories)
    {
      if (f->supports(info))
      {
        factory = f;
        break;
      }
    }
  }
  return factory ? factory->createDecoder(info) : nullptr;
}

std::vector<CodecInfo> CodecRegistry::enumerateCodecs() const
{
  std::lock_guard<std::mutex> lock(_mutex);
  std::vector<CodecInfo> result;
  result.reserve(_factories.size());
  for (const auto& [name, factory] : _factories)
  {
    result.push_back(factory->codecInfo());
  }
  return result;
}

std::optional<CodecInfo> CodecRegistry::findByName(const std::string& name) const
{
  std::lock_guard<std::mutex> lock(_mutex);
  auto it = _factories.find(name);
  if (it != _factories.end())
  {
    return it->second->codecInfo();
  }
  return std::nullopt;
}

std::optional<CodecInfo> CodecRegistry::findByPayloadType(std::uint8_t pt) const
{
  std::lock_guard<std::mutex> lock(_mutex);
  for (const auto& [name, factory] : _factories)
  {
    if (factory->codecInfo().defaultPayloadType == pt)
    {
      return factory->codecInfo();
    }
  }
  return std::nullopt;
}

} // namespace codecs
} // namespace iora
