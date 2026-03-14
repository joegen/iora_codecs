#pragma once

/// @file stage_metrics.hpp
/// @brief Per-stage metrics collection and instrumented handler wrapper.

#include "iora/codecs/pipeline/i_media_handler.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>

namespace iora {
namespace codecs {

/// Point-in-time snapshot of stage metrics — a plain copyable value type.
struct StageMetricsSnapshot
{
  std::string stageName;
  std::uint64_t framesIn = 0;
  std::uint64_t framesOut = 0;
  std::uint64_t framesDropped = 0;
  std::uint64_t errorCount = 0;
  std::chrono::microseconds totalLatencyUs{0};
  std::chrono::microseconds maxLatencyUs{0};
  std::chrono::microseconds minLatencyUs{std::chrono::microseconds::max()};

  double averageLatencyUs() const
  {
    if (framesIn == 0)
    {
      return 0.0;
    }
    return static_cast<double>(totalLatencyUs.count())
           / static_cast<double>(framesIn);
  }
};

/// Atomic per-stage counters and latency statistics.
///
/// All fields are std::atomic for lock-free reads from monitoring threads.
/// Writers (processing thread) update with relaxed ordering — snapshot()
/// provides a best-effort point-in-time copy, not a synchronization barrier.
///
/// Non-copyable, non-movable (owned by InstrumentedStage).
class StageMetrics
{
public:
  explicit StageMetrics(std::string name);

  /// Record an incoming frame with measured processing latency.
  void recordIncoming(std::chrono::microseconds latency) noexcept;

  /// Record an outgoing frame with measured processing latency.
  void recordOutgoing(std::chrono::microseconds latency) noexcept;

  /// Record a dropped frame (no downstream handler).
  void recordDrop() noexcept;

  /// Record a processing error (handler threw an exception).
  void recordError() noexcept;

  /// Take a point-in-time snapshot for safe reading from any thread.
  StageMetricsSnapshot snapshot() const;

  const std::string& stageName() const noexcept;

  StageMetrics(const StageMetrics&) = delete;
  StageMetrics& operator=(const StageMetrics&) = delete;
  StageMetrics(StageMetrics&&) = delete;
  StageMetrics& operator=(StageMetrics&&) = delete;

private:
  void updateLatency(std::chrono::microseconds latency) noexcept;

  std::string _stageName;
  std::atomic<std::uint64_t> _framesIn{0};
  std::atomic<std::uint64_t> _framesOut{0};
  std::atomic<std::uint64_t> _framesDropped{0};
  std::atomic<std::uint64_t> _errorCount{0};
  std::atomic<std::int64_t> _totalLatencyUs{0};
  std::atomic<std::int64_t> _maxLatencyUs{0};
  std::atomic<std::int64_t> _minLatencyUs{std::numeric_limits<std::int64_t>::max()};
};

/// IMediaHandler decorator that wraps any handler with timing
/// instrumentation and per-frame metrics collection.
///
/// The InstrumentedStage delegates all processing to the wrapped handler
/// and measures latency around each incoming()/outgoing() call.
///
/// Chain management: addToChain()/chainWith() on this object delegate
/// to the wrapped handler so that the wrapped handler's forwardIncoming()
/// routes correctly.
class InstrumentedStage : public IMediaHandler
{
public:
  InstrumentedStage(std::string name,
                    std::shared_ptr<IMediaHandler> wrapped);

  void incoming(std::shared_ptr<MediaBuffer> buffer) override;
  void outgoing(std::shared_ptr<MediaBuffer> buffer) override;

  /// Delegates to wrapped handler's chain. Shadows IMediaHandler::addToChain.
  void addToChain(std::shared_ptr<IMediaHandler> next);

  /// Delegates to wrapped handler's chain. Shadows IMediaHandler::chainWith.
  IMediaHandler& chainWith(std::shared_ptr<IMediaHandler> handler);

  StageMetricsSnapshot snapshot() const;
  const std::string& stageName() const noexcept;

  std::shared_ptr<IMediaHandler> wrappedHandler();
  const std::shared_ptr<IMediaHandler>& wrappedHandler() const;

  bool hasDownstream() const noexcept;

private:
  std::shared_ptr<IMediaHandler> _wrapped;
  StageMetrics _metrics;
  bool _hasDownstream = false;
};

} // namespace codecs
} // namespace iora
