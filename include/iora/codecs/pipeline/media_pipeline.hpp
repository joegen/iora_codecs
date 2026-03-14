#pragma once

/// @file media_pipeline.hpp
/// @brief Orchestrator that connects codec handlers, DSP stages, and
///        external sources/sinks into a directed acyclic processing graph.

#include "iora/codecs/pipeline/i_media_handler.hpp"
#include "iora/codecs/pipeline/stage_metrics.hpp"
#include "iora/codecs/pipeline/transcoding_handler.hpp"
#include "iora/codecs/format/sample_format.hpp"

#include <iora/common/i_lifecycle_managed.hpp>

#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace iora {

namespace core {
class ThreadPool;
class TimerService;
} // namespace core

namespace codecs {

/// Result of a codec swap operation.
struct SwapResult
{
  bool success = false;
  std::string message;
  std::chrono::microseconds drainDuration{0};
};

/// Optional format descriptor for pipeline stage inputs/outputs.
/// Used by format negotiation to auto-insert conversion stages.
struct StageFormat
{
  std::optional<std::uint32_t> sampleRate;
  std::optional<SampleFormat> sampleFormat;
  std::optional<std::uint8_t> channels;
};

/// Orchestrator that connects codec handlers, DSP stages, and external
/// sources/sinks into a complete processing graph.
///
/// Implements iora::common::ILifecycleManaged with state machine:
///   Created → Running → Draining → Stopped → Reset
///
/// Stages are named IMediaHandler instances connected in a DAG topology.
/// Each stage is wrapped in an InstrumentedStage for metrics collection.
///
/// NOT thread-safe — graph mutation (addStage, connectStages, removeStage)
/// must happen in Created or Stopped state only. Processing (incoming,
/// outgoing) is synchronous in the caller's thread.
class MediaPipeline : public iora::common::ILifecycleManaged
{
public:
  explicit MediaPipeline(
    iora::core::ThreadPool* threadPool = nullptr,
    iora::core::TimerService* timerService = nullptr);

  ~MediaPipeline() override = default;

  MediaPipeline(const MediaPipeline&) = delete;
  MediaPipeline& operator=(const MediaPipeline&) = delete;

  // -- Graph construction (Created or Stopped state only) --

  /// Register a named stage. Wraps handler in InstrumentedStage.
  /// Returns false if name is duplicate or state is invalid.
  bool addStage(const std::string& name,
                std::shared_ptr<IMediaHandler> handler);

  /// Register a named stage with format declarations for negotiation.
  /// When connecting stages with mismatched formats, conversion stages
  /// (resampler, channel mixer, format converter) are auto-inserted.
  bool addStage(const std::string& name,
                std::shared_ptr<IMediaHandler> handler,
                const StageFormat& inputFormat,
                const StageFormat& outputFormat);

  /// Connect source stage's output to dest stage's input.
  /// Returns false if either stage doesn't exist or state is invalid.
  bool connectStages(const std::string& sourceName,
                     const std::string& destName);

  /// Remove a named stage and its connections.
  /// Returns false if stage doesn't exist or state is invalid.
  bool removeStage(const std::string& name);

  /// Retrieve the underlying IMediaHandler by stage name.
  /// Returns nullptr if not found.
  std::shared_ptr<IMediaHandler> getStage(const std::string& name) const;

  // -- Metrics --

  /// Retrieve metrics snapshot for a named stage.
  StageMetricsSnapshot getMetrics(const std::string& name) const;

  /// Retrieve all stage metrics.
  std::vector<StageMetricsSnapshot> allMetrics() const;

  /// Number of registered stages.
  std::size_t stageCount() const noexcept;

  // -- Processing --

  /// Push a buffer into the pipeline's entry stage (incoming direction).
  /// Rejects if not in Running state.
  void incoming(std::shared_ptr<MediaBuffer> buffer);

  /// Push a buffer into the pipeline's exit stage (outgoing direction).
  /// Rejects if not in Running state.
  void outgoing(std::shared_ptr<MediaBuffer> buffer);

  // -- Codec hot-swap --

  /// Replace the codec in a TranscodingHandler stage while running.
  /// Drains in-flight frames, swaps codecs, and resumes.
  /// Returns error if stage is not a TranscodingHandler or not Running.
  SwapResult swapCodec(const std::string& stageName,
                       std::unique_ptr<ICodec> newDecoder,
                       std::unique_ptr<ICodec> newEncoder);

  // -- Topology validation --

  /// Check if the graph contains a cycle. Returns true if acyclic.
  bool validateAcyclic() const;

  // -- ILifecycleManaged --

  iora::common::LifecycleResult start() override;
  iora::common::LifecycleResult drain(std::uint32_t timeoutMs = 30000) override;
  iora::common::LifecycleResult stop() override;
  iora::common::LifecycleResult reset() override;
  iora::common::LifecycleState getState() const override;
  std::uint32_t getInFlightCount() const override;

private:
  struct StageEntry
  {
    std::shared_ptr<InstrumentedStage> instrumented;
    std::vector<std::string> outEdges;
    std::vector<std::string> inEdges;
    StageFormat inputFormat;
    StageFormat outputFormat;
    bool autoInserted = false;
  };

  bool isGraphMutable() const;
  bool wouldCreateCycle(const std::string& source,
                        const std::string& dest) const;
  void wireStageConnections(const std::string& sourceName);
  std::string findEntryStage() const;
  std::string findExitStage() const;
  bool negotiateFormats(const std::string& sourceName,
                       const std::string& destName);

  std::unordered_map<std::string, StageEntry> _stages;
  std::string _entryStage;
  std::string _exitStage;

  std::atomic<iora::common::LifecycleState> _state{
    iora::common::LifecycleState::Created};
  std::atomic<std::uint32_t> _inFlightCount{0};

  iora::core::ThreadPool* _threadPool = nullptr;
  iora::core::TimerService* _timerService = nullptr;
};

} // namespace codecs
} // namespace iora
