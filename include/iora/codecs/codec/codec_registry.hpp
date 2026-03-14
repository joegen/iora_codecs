#pragma once

/// @file codec_registry.hpp
/// @brief Central codec registration and lookup.

#include "iora/codecs/codec/codec_info.hpp"
#include "iora/codecs/codec/i_codec.hpp"
#include "iora/codecs/codec/i_codec_factory.hpp"

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace iora {
namespace codecs {

/// Central registry of codec factories.
///
/// Stores all registered ICodecFactory instances and provides lookup
/// by name, payload type, and CodecInfo matching. NOT a singleton —
/// created by the host application and exported via
/// IoraService::exportApi().
///
/// Thread-safe: all methods are guarded by a mutex. Registry
/// operations are infrequent setup-time calls, not hot path.
class CodecRegistry
{
public:
  CodecRegistry() = default;

  // Non-copyable, non-movable.
  CodecRegistry(const CodecRegistry&) = delete;
  CodecRegistry& operator=(const CodecRegistry&) = delete;
  CodecRegistry(CodecRegistry&&) = delete;
  CodecRegistry& operator=(CodecRegistry&&) = delete;

  /// Register a codec factory. Throws std::runtime_error if a factory
  /// with the same codec name is already registered.
  void registerFactory(std::shared_ptr<ICodecFactory> factory);

  /// Remove a factory by codec name. No-op if not found.
  void unregisterFactory(const std::string& name);

  /// Create an encoder for a codec matching @p info.
  /// Returns nullptr if no matching factory is registered.
  std::unique_ptr<ICodec> createEncoder(const CodecInfo& info);

  /// Create a decoder for a codec matching @p info.
  /// Returns nullptr if no matching factory is registered.
  std::unique_ptr<ICodec> createDecoder(const CodecInfo& info);

  /// Returns CodecInfo for all registered factories.
  std::vector<CodecInfo> enumerateCodecs() const;

  /// Find a codec by SDP encoding name. Returns empty optional if not found.
  std::optional<CodecInfo> findByName(const std::string& name) const;

  /// Find a codec by static RTP payload type. Returns empty optional if not found.
  std::optional<CodecInfo> findByPayloadType(std::uint8_t pt) const;

private:
  mutable std::mutex _mutex;
  std::unordered_map<std::string, std::shared_ptr<ICodecFactory>> _factories;
};

} // namespace codecs
} // namespace iora
