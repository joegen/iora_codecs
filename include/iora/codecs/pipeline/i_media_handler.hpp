#pragma once

/// @file i_media_handler.hpp
/// @brief Base interface for chainable media pipeline processing nodes.

#include "iora/codecs/core/media_buffer.hpp"

#include <memory>
#include <utility>

namespace iora {
namespace codecs {

/// Abstract base class for media pipeline processing nodes.
///
/// Implements the chain-of-responsibility pattern: each handler processes
/// a MediaBuffer and optionally forwards it to the next handler in the
/// chain. Handlers are linked via addToChain() or the fluent chainWith().
///
/// ## Directionality contract
///
/// Two independent processing directions exist:
///   - incoming() — receive path (network → application): e.g., decode
///   - outgoing() — send path (application → network): e.g., encode
///
/// Handlers are NOT required to process both directions. A handler may
/// override only one direction; the other inherits the default behavior
/// (forward to next handler unchanged). For bidirectional transcoding
/// (e.g., SBC), callers chain two separate TranscodingHandler instances,
/// one per direction.
///
/// ## Threading model
///
/// Both incoming() and outgoing() execute synchronously in the caller's
/// thread — no hidden queuing or thread hops on the hot path. This keeps
/// latency predictable and avoids lock contention. If a handler needs
/// async processing (e.g., video encode on a worker thread), it must
/// explicitly dispatch to ThreadPool and return immediately.
class IMediaHandler
{
public:
  virtual ~IMediaHandler() = default;

  IMediaHandler() = default;

  // Non-copyable.
  IMediaHandler(const IMediaHandler&) = delete;
  IMediaHandler& operator=(const IMediaHandler&) = delete;

  // Movable.
  IMediaHandler(IMediaHandler&&) noexcept = default;
  IMediaHandler& operator=(IMediaHandler&&) noexcept = default;

  /// Process media arriving from an external source toward the application.
  /// Default implementation forwards to the next handler in the chain.
  virtual void incoming(std::shared_ptr<MediaBuffer> buffer)
  {
    forwardIncoming(std::move(buffer));
  }

  /// Process media going from the application toward an external sink.
  /// Default implementation forwards to the next handler in the chain.
  virtual void outgoing(std::shared_ptr<MediaBuffer> buffer)
  {
    forwardOutgoing(std::move(buffer));
  }

  /// Set the next handler in the chain.
  void addToChain(std::shared_ptr<IMediaHandler> next)
  {
    _next = std::move(next);
  }

  /// Fluent API for building chains.
  /// Returns a reference to the added handler for further chaining:
  ///   handlerA.chainWith(handlerB).chainWith(handlerC);
  IMediaHandler& chainWith(std::shared_ptr<IMediaHandler> handler)
  {
    auto& ref = *handler;
    _next = std::move(handler);
    return ref;
  }

protected:
  /// Forward a buffer to the next handler's incoming path (if chained).
  void forwardIncoming(std::shared_ptr<MediaBuffer> buffer)
  {
    if (_next)
    {
      _next->incoming(std::move(buffer));
    }
  }

  /// Forward a buffer to the next handler's outgoing path (if chained).
  void forwardOutgoing(std::shared_ptr<MediaBuffer> buffer)
  {
    if (_next)
    {
      _next->outgoing(std::move(buffer));
    }
  }

  std::shared_ptr<IMediaHandler> _next;
};

} // namespace codecs
} // namespace iora
