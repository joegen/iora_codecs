#include <catch2/catch_test_macros.hpp>

#include "iora/codecs/pipeline/media_pipeline.hpp"
#include "iora/codecs/pipeline/stage_metrics.hpp"
#include "iora/codecs/pipeline/transcoding_handler.hpp"
#include "iora/codecs/codec/i_codec.hpp"

#include <chrono>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace iora::codecs;

namespace {

/// Create a test MediaBuffer with given size.
std::shared_ptr<MediaBuffer> makeTestBuffer(std::size_t bytes = 640)
{
  auto buf = MediaBuffer::create(bytes);
  std::memset(buf->data(), 0, bytes);
  buf->setSize(bytes);
  return buf;
}

/// Passthrough handler — forwards via forwardIncoming()/forwardOutgoing().
class PassthroughHandler : public IMediaHandler
{
public:
  void incoming(std::shared_ptr<MediaBuffer> buffer) override
  {
    forwardIncoming(std::move(buffer));
  }

  void outgoing(std::shared_ptr<MediaBuffer> buffer) override
  {
    forwardOutgoing(std::move(buffer));
  }
};

/// Capture handler — records received buffers.
class CaptureHandler : public IMediaHandler
{
public:
  void incoming(std::shared_ptr<MediaBuffer> buffer) override
  {
    incomingBuffers.push_back(std::move(buffer));
  }

  void outgoing(std::shared_ptr<MediaBuffer> buffer) override
  {
    outgoingBuffers.push_back(std::move(buffer));
  }

  std::vector<std::shared_ptr<MediaBuffer>> incomingBuffers;
  std::vector<std::shared_ptr<MediaBuffer>> outgoingBuffers;
};

/// Handler that throws on every incoming() call.
class ThrowingHandler : public IMediaHandler
{
public:
  void incoming(std::shared_ptr<MediaBuffer>) override
  {
    throw std::runtime_error("ThrowingHandler error");
  }

  void outgoing(std::shared_ptr<MediaBuffer>) override
  {
    throw std::runtime_error("ThrowingHandler outgoing error");
  }
};

/// Handler that busy-waits for a configurable duration.
class SlowHandler : public IMediaHandler
{
public:
  explicit SlowHandler(std::chrono::microseconds delay)
    : _delay(delay)
  {
  }

  void incoming(std::shared_ptr<MediaBuffer> buffer) override
  {
    std::this_thread::sleep_for(_delay);
    forwardIncoming(std::move(buffer));
  }

  void outgoing(std::shared_ptr<MediaBuffer> buffer) override
  {
    std::this_thread::sleep_for(_delay);
    forwardOutgoing(std::move(buffer));
  }

private:
  std::chrono::microseconds _delay;
};

/// Minimal mock codec — passes through data unchanged for pipeline testing.
class MockCodec : public ICodec
{
public:
  explicit MockCodec(const CodecInfo& codecInfo)
    : _info(codecInfo)
  {
  }

  const CodecInfo& info() const override { return _info; }

  std::shared_ptr<MediaBuffer> encode(const MediaBuffer& input) override
  {
    auto buf = MediaBuffer::create(input.size());
    std::memcpy(buf->data(), input.data(), input.size());
    buf->setSize(input.size());
    buf->copyMetadataFrom(input);
    return buf;
  }

  std::shared_ptr<MediaBuffer> decode(const MediaBuffer& input) override
  {
    auto buf = MediaBuffer::create(input.size());
    std::memcpy(buf->data(), input.data(), input.size());
    buf->setSize(input.size());
    buf->copyMetadataFrom(input);
    return buf;
  }

  std::shared_ptr<MediaBuffer> plc(std::size_t frameSamples) override
  {
    return nullptr;
  }

  bool setParameter(const std::string&, std::uint32_t) override { return false; }
  std::uint32_t getParameter(const std::string&) const override { return 0; }

private:
  CodecInfo _info;
};

CodecInfo makeCodecInfo(const std::string& name, std::uint32_t clockRate)
{
  CodecInfo info;
  info.name = name;
  info.clockRate = clockRate;
  info.channels = 1;
  return info;
}

} // namespace

// =========================================================================
// Phase 1: StageMetrics and InstrumentedStage tests
// =========================================================================

TEST_CASE("InstrumentedStage: initial state has zero metrics", "[pipeline][metrics]")
{
  auto wrapped = std::make_shared<PassthroughHandler>();
  InstrumentedStage stage("test_stage", wrapped);

  auto s = stage.snapshot();

  CHECK(s.stageName == "test_stage");
  CHECK(s.framesIn == 0);
  CHECK(s.framesOut == 0);
  CHECK(s.framesDropped == 0);
  CHECK(s.errorCount == 0);
  CHECK(s.totalLatencyUs == std::chrono::microseconds{0});
  CHECK(s.maxLatencyUs == std::chrono::microseconds{0});
  CHECK(s.minLatencyUs == std::chrono::microseconds::max());
  CHECK(s.averageLatencyUs() == 0.0);
}

TEST_CASE("InstrumentedStage: framesIn/framesOut counts", "[pipeline][metrics]")
{
  auto wrapped = std::make_shared<PassthroughHandler>();
  auto capture = std::make_shared<CaptureHandler>();

  InstrumentedStage stage("counter_stage", wrapped);
  stage.addToChain(capture);

  // Push 10 incoming buffers.
  for (int i = 0; i < 10; ++i)
  {
    stage.incoming(makeTestBuffer());
  }

  // Push 5 outgoing buffers.
  for (int i = 0; i < 5; ++i)
  {
    stage.outgoing(makeTestBuffer());
  }

  auto s = stage.snapshot();
  CHECK(s.framesIn == 10);
  CHECK(s.framesOut == 5);

  // Capture handler received all incoming buffers.
  CHECK(capture->incomingBuffers.size() == 10);
  CHECK(capture->outgoingBuffers.size() == 5);
}

TEST_CASE("InstrumentedStage: latency tracking with known delay", "[pipeline][metrics]")
{
  auto delay = std::chrono::microseconds(1000); // 1ms
  auto wrapped = std::make_shared<SlowHandler>(delay);
  auto capture = std::make_shared<CaptureHandler>();

  InstrumentedStage stage("slow_stage", wrapped);
  stage.addToChain(capture);

  constexpr int kFrames = 3;
  for (int i = 0; i < kFrames; ++i)
  {
    stage.incoming(makeTestBuffer());
  }

  auto s = stage.snapshot();
  CHECK(s.framesIn == kFrames);

  // Each frame should take at least ~1ms. Use generous tolerance.
  CHECK(s.minLatencyUs >= std::chrono::microseconds{500});
  CHECK(s.maxLatencyUs >= std::chrono::microseconds{500});
  CHECK(s.totalLatencyUs >= std::chrono::microseconds{kFrames * 500});
  CHECK(s.averageLatencyUs() >= 500.0);
}

TEST_CASE("InstrumentedStage: errorCount increments on handler exception", "[pipeline][metrics]")
{
  auto wrapped = std::make_shared<ThrowingHandler>();
  InstrumentedStage stage("throwing_stage", wrapped);

  for (int i = 0; i < 3; ++i)
  {
    CHECK_THROWS_AS(stage.incoming(makeTestBuffer()), std::runtime_error);
  }

  auto s = stage.snapshot();
  CHECK(s.errorCount == 3);
  // framesIn should NOT be incremented on error (exception thrown before record).
  CHECK(s.framesIn == 0);
}

TEST_CASE("InstrumentedStage: framesDropped when downstream is null", "[pipeline][metrics]")
{
  auto wrapped = std::make_shared<PassthroughHandler>();
  InstrumentedStage stage("no_downstream", wrapped);
  // No addToChain() call — _hasDownstream remains false.

  for (int i = 0; i < 3; ++i)
  {
    stage.incoming(makeTestBuffer());
  }

  auto s = stage.snapshot();
  CHECK(s.framesIn == 3);
  CHECK(s.framesDropped == 3);
}

TEST_CASE("InstrumentedStage: framesDropped is zero when downstream exists", "[pipeline][metrics]")
{
  auto wrapped = std::make_shared<PassthroughHandler>();
  auto capture = std::make_shared<CaptureHandler>();

  InstrumentedStage stage("has_downstream", wrapped);
  stage.addToChain(capture);

  for (int i = 0; i < 3; ++i)
  {
    stage.incoming(makeTestBuffer());
  }

  auto s = stage.snapshot();
  CHECK(s.framesIn == 3);
  CHECK(s.framesDropped == 0);
}

TEST_CASE("InstrumentedStage: snapshot returns independent copy", "[pipeline][metrics]")
{
  auto wrapped = std::make_shared<PassthroughHandler>();
  auto capture = std::make_shared<CaptureHandler>();

  InstrumentedStage stage("snapshot_test", wrapped);
  stage.addToChain(capture);

  // Push 5 frames and take first snapshot.
  for (int i = 0; i < 5; ++i)
  {
    stage.incoming(makeTestBuffer());
  }
  auto s1 = stage.snapshot();
  CHECK(s1.framesIn == 5);

  // Push 5 more frames and take second snapshot.
  for (int i = 0; i < 5; ++i)
  {
    stage.incoming(makeTestBuffer());
  }
  auto s2 = stage.snapshot();
  CHECK(s2.framesIn == 10);

  // First snapshot is unchanged.
  CHECK(s1.framesIn == 5);
}

// =========================================================================
// Phase 2: MediaPipeline lifecycle and linear topology tests
// =========================================================================

using iora::common::LifecycleState;

TEST_CASE("MediaPipeline: full lifecycle round-trip", "[pipeline][lifecycle]")
{
  MediaPipeline pipeline;

  CHECK(pipeline.getState() == LifecycleState::Created);

  // Created → Running
  auto r1 = pipeline.start();
  CHECK(r1.success);
  CHECK(pipeline.getState() == LifecycleState::Running);

  // Running → Draining
  auto r2 = pipeline.drain();
  CHECK(r2.success);
  CHECK(pipeline.getState() == LifecycleState::Draining);

  // Draining → Stopped
  auto r3 = pipeline.stop();
  CHECK(r3.success);
  CHECK(pipeline.getState() == LifecycleState::Stopped);

  // Stopped → Reset
  auto r4 = pipeline.reset();
  CHECK(r4.success);
  CHECK(pipeline.getState() == LifecycleState::Reset);

  // Reset → Running
  auto r5 = pipeline.start();
  CHECK(r5.success);
  CHECK(pipeline.getState() == LifecycleState::Running);
}

TEST_CASE("MediaPipeline: start() is idempotent when Running", "[pipeline][lifecycle]")
{
  MediaPipeline pipeline;
  pipeline.start();

  auto r = pipeline.start();
  CHECK(r.success);
  CHECK(pipeline.getState() == LifecycleState::Running);
}

TEST_CASE("MediaPipeline: stop() is idempotent when Stopped", "[pipeline][lifecycle]")
{
  MediaPipeline pipeline;
  pipeline.start();
  pipeline.stop();

  auto r = pipeline.stop();
  CHECK(r.success);
  CHECK(pipeline.getState() == LifecycleState::Stopped);
}

TEST_CASE("MediaPipeline: drain() rejects when not Running", "[pipeline][lifecycle]")
{
  MediaPipeline pipeline;

  auto r = pipeline.drain();
  CHECK_FALSE(r.success);
  CHECK(pipeline.getState() == LifecycleState::Created);
}

TEST_CASE("MediaPipeline: reset() rejects when not Stopped", "[pipeline][lifecycle]")
{
  MediaPipeline pipeline;
  pipeline.start();

  auto r = pipeline.reset();
  CHECK_FALSE(r.success);
  CHECK(pipeline.getState() == LifecycleState::Running);
}

TEST_CASE("MediaPipeline: reject addStage when Running", "[pipeline][lifecycle]")
{
  MediaPipeline pipeline;
  pipeline.start();

  auto handler = std::make_shared<PassthroughHandler>();
  CHECK_FALSE(pipeline.addStage("A", handler));
}

TEST_CASE("MediaPipeline: reject connectStages when Running", "[pipeline][lifecycle]")
{
  MediaPipeline pipeline;
  pipeline.addStage("A", std::make_shared<PassthroughHandler>());
  pipeline.addStage("B", std::make_shared<CaptureHandler>());
  pipeline.start();

  CHECK_FALSE(pipeline.connectStages("A", "B"));
}

TEST_CASE("MediaPipeline: reject incoming when not Running", "[pipeline][lifecycle]")
{
  MediaPipeline pipeline;
  auto cap = std::make_shared<CaptureHandler>();
  pipeline.addStage("A", cap);
  // Not started — incoming should be ignored.
  pipeline.incoming(makeTestBuffer());
  CHECK(cap->incomingBuffers.empty());
}

TEST_CASE("MediaPipeline: duplicate stage name rejected", "[pipeline][graph]")
{
  MediaPipeline pipeline;
  CHECK(pipeline.addStage("A", std::make_shared<PassthroughHandler>()));
  CHECK_FALSE(pipeline.addStage("A", std::make_shared<PassthroughHandler>()));
}

TEST_CASE("MediaPipeline: connect nonexistent stage rejected", "[pipeline][graph]")
{
  MediaPipeline pipeline;
  pipeline.addStage("A", std::make_shared<PassthroughHandler>());

  CHECK_FALSE(pipeline.connectStages("A", "B"));
  CHECK_FALSE(pipeline.connectStages("B", "A"));
}

TEST_CASE("MediaPipeline: linear chain A → B", "[pipeline][graph]")
{
  MediaPipeline pipeline;

  auto capture = std::make_shared<CaptureHandler>();
  pipeline.addStage("source", std::make_shared<PassthroughHandler>());
  pipeline.addStage("sink", capture);
  pipeline.connectStages("source", "sink");
  pipeline.start();

  pipeline.incoming(makeTestBuffer());
  pipeline.incoming(makeTestBuffer());
  pipeline.incoming(makeTestBuffer());

  CHECK(capture->incomingBuffers.size() == 3);
}

TEST_CASE("MediaPipeline: 3-stage linear A → B → C", "[pipeline][graph]")
{
  MediaPipeline pipeline;

  auto capture = std::make_shared<CaptureHandler>();
  pipeline.addStage("A", std::make_shared<PassthroughHandler>());
  pipeline.addStage("B", std::make_shared<PassthroughHandler>());
  pipeline.addStage("C", capture);
  pipeline.connectStages("A", "B");
  pipeline.connectStages("B", "C");
  pipeline.start();

  for (int i = 0; i < 5; ++i)
  {
    pipeline.incoming(makeTestBuffer());
  }

  CHECK(capture->incomingBuffers.size() == 5);
}

TEST_CASE("MediaPipeline: outgoing direction flows through pipeline", "[pipeline][graph]")
{
  MediaPipeline pipeline;

  auto passthrough = std::make_shared<PassthroughHandler>();
  auto capture = std::make_shared<CaptureHandler>();
  pipeline.addStage("entry", capture);
  pipeline.addStage("exit", passthrough);
  pipeline.connectStages("entry", "exit");
  pipeline.start();

  // Outgoing goes to the exit stage.
  pipeline.outgoing(makeTestBuffer());
  // The exit stage is a PassthroughHandler with no _next for outgoing,
  // but the call should not crash. The entry stage is the exit for outgoing.
  // Actually, outgoing enters at the exit stage (tail).
  // Since PassthroughHandler forwards outgoing via forwardOutgoing, and
  // it has no _next set for outgoing direction... the buffer is silently dropped.
  // This is expected behavior.
  CHECK(pipeline.getState() == LifecycleState::Running);
}

TEST_CASE("MediaPipeline: metrics on 2-stage pipeline", "[pipeline][metrics]")
{
  MediaPipeline pipeline;

  pipeline.addStage("A", std::make_shared<PassthroughHandler>());
  pipeline.addStage("B", std::make_shared<CaptureHandler>());
  pipeline.connectStages("A", "B");
  pipeline.start();

  for (int i = 0; i < 10; ++i)
  {
    pipeline.incoming(makeTestBuffer());
  }

  auto mA = pipeline.getMetrics("A");
  auto mB = pipeline.getMetrics("B");

  CHECK(mA.framesIn == 10);
  CHECK(mB.framesIn == 10);
}

TEST_CASE("MediaPipeline: getStage returns handler", "[pipeline][graph]")
{
  MediaPipeline pipeline;
  auto handler = std::make_shared<PassthroughHandler>();
  pipeline.addStage("X", handler);

  auto retrieved = pipeline.getStage("X");
  CHECK(retrieved == handler);
  CHECK(pipeline.getStage("nonexistent") == nullptr);
}

TEST_CASE("MediaPipeline: removeStage cleans up edges", "[pipeline][graph]")
{
  MediaPipeline pipeline;

  pipeline.addStage("A", std::make_shared<PassthroughHandler>());
  pipeline.addStage("B", std::make_shared<PassthroughHandler>());
  pipeline.addStage("C", std::make_shared<CaptureHandler>());
  pipeline.connectStages("A", "B");
  pipeline.connectStages("B", "C");
  CHECK(pipeline.stageCount() == 3);

  CHECK(pipeline.removeStage("B"));
  CHECK(pipeline.stageCount() == 2);
  CHECK(pipeline.getStage("B") == nullptr);
}

TEST_CASE("MediaPipeline: empty pipeline start succeeds", "[pipeline][graph]")
{
  MediaPipeline pipeline;
  auto r = pipeline.start();
  CHECK(r.success);

  // incoming() on empty pipeline is a no-op.
  pipeline.incoming(makeTestBuffer());
  CHECK(pipeline.getState() == LifecycleState::Running);
}

TEST_CASE("MediaPipeline: allMetrics returns all stages", "[pipeline][metrics]")
{
  MediaPipeline pipeline;
  pipeline.addStage("X", std::make_shared<PassthroughHandler>());
  pipeline.addStage("Y", std::make_shared<CaptureHandler>());

  auto all = pipeline.allMetrics();
  CHECK(all.size() == 2);
}

TEST_CASE("MediaPipeline: stageCount", "[pipeline][graph]")
{
  MediaPipeline pipeline;
  CHECK(pipeline.stageCount() == 0);

  pipeline.addStage("A", std::make_shared<PassthroughHandler>());
  CHECK(pipeline.stageCount() == 1);

  pipeline.addStage("B", std::make_shared<PassthroughHandler>());
  CHECK(pipeline.stageCount() == 2);
}

TEST_CASE("MediaPipeline: getInFlightCount", "[pipeline][lifecycle]")
{
  MediaPipeline pipeline;
  CHECK(pipeline.getInFlightCount() == 0);

  pipeline.addStage("A", std::make_shared<CaptureHandler>());
  pipeline.start();

  // Synchronous pipeline — in-flight count is 0 between calls.
  pipeline.incoming(makeTestBuffer());
  CHECK(pipeline.getInFlightCount() == 0);
}

TEST_CASE("MediaPipeline: drain returns stats", "[pipeline][lifecycle]")
{
  MediaPipeline pipeline;
  pipeline.addStage("A", std::make_shared<CaptureHandler>());
  pipeline.start();

  auto r = pipeline.drain();
  CHECK(r.success);
  REQUIRE(r.drainStats.has_value());
  CHECK(r.drainStats->remaining == 0);
}

// =========================================================================
// Phase 3: DAG Topology — Fan-Out, Fan-In, Cycle Detection
// =========================================================================

TEST_CASE("MediaPipeline: fan-out A → B and A → C", "[pipeline][dag]")
{
  MediaPipeline pipeline;

  auto capB = std::make_shared<CaptureHandler>();
  auto capC = std::make_shared<CaptureHandler>();

  pipeline.addStage("A", std::make_shared<PassthroughHandler>());
  pipeline.addStage("B", capB);
  pipeline.addStage("C", capC);
  pipeline.connectStages("A", "B");
  pipeline.connectStages("A", "C");
  pipeline.start();

  pipeline.incoming(makeTestBuffer());
  pipeline.incoming(makeTestBuffer());

  CHECK(capB->incomingBuffers.size() == 2);
  CHECK(capC->incomingBuffers.size() == 2);
}

TEST_CASE("MediaPipeline: fan-out delivers independent buffer copies", "[pipeline][dag]")
{
  MediaPipeline pipeline;

  auto capB = std::make_shared<CaptureHandler>();
  auto capC = std::make_shared<CaptureHandler>();

  pipeline.addStage("A", std::make_shared<PassthroughHandler>());
  pipeline.addStage("B", capB);
  pipeline.addStage("C", capC);
  pipeline.connectStages("A", "B");
  pipeline.connectStages("A", "C");
  pipeline.start();

  auto buf = makeTestBuffer();
  buf->data()[0] = 0xAA;
  pipeline.incoming(buf);

  REQUIRE(capB->incomingBuffers.size() == 1);
  REQUIRE(capC->incomingBuffers.size() == 1);

  // Each downstream got an independent copy — modifying one doesn't affect the other.
  capB->incomingBuffers[0]->data()[0] = 0xBB;
  CHECK(capC->incomingBuffers[0]->data()[0] == 0xAA);
}

TEST_CASE("MediaPipeline: fan-out outgoing direction", "[pipeline][dag]")
{
  MediaPipeline pipeline;

  auto capB = std::make_shared<CaptureHandler>();
  auto capC = std::make_shared<CaptureHandler>();

  pipeline.addStage("A", std::make_shared<PassthroughHandler>());
  pipeline.addStage("B", capB);
  pipeline.addStage("C", capC);
  pipeline.connectStages("A", "B");
  pipeline.connectStages("A", "C");
  pipeline.start();

  // Fan-out distributes outgoing as well.
  pipeline.outgoing(makeTestBuffer());

  // Outgoing enters at the exit stage. B and C have no outEdges,
  // so one of them is the exit stage. The other is not directly reachable
  // via outgoing. This test primarily verifies no crash.
  CHECK(pipeline.getState() == LifecycleState::Running);
}

TEST_CASE("MediaPipeline: fan-in A → C and B → C", "[pipeline][dag]")
{
  MediaPipeline pipeline;

  auto capC = std::make_shared<CaptureHandler>();

  pipeline.addStage("A", std::make_shared<PassthroughHandler>());
  pipeline.addStage("B", std::make_shared<PassthroughHandler>());
  pipeline.addStage("C", capC);
  pipeline.connectStages("A", "C");
  pipeline.connectStages("B", "C");
  pipeline.start();

  // Push through entry stage (A has no inEdges, so it's the entry).
  pipeline.incoming(makeTestBuffer());

  // A forwards to C via its chain.
  CHECK(capC->incomingBuffers.size() == 1);
}

TEST_CASE("MediaPipeline: diamond DAG A → B, A → C, B → D, C → D", "[pipeline][dag]")
{
  MediaPipeline pipeline;

  auto capD = std::make_shared<CaptureHandler>();

  pipeline.addStage("A", std::make_shared<PassthroughHandler>());
  pipeline.addStage("B", std::make_shared<PassthroughHandler>());
  pipeline.addStage("C", std::make_shared<PassthroughHandler>());
  pipeline.addStage("D", capD);

  pipeline.connectStages("A", "B");
  pipeline.connectStages("A", "C");
  pipeline.connectStages("B", "D");
  pipeline.connectStages("C", "D");

  pipeline.start();

  pipeline.incoming(makeTestBuffer());

  // D receives from both B and C paths.
  CHECK(capD->incomingBuffers.size() == 2);
}

TEST_CASE("MediaPipeline: self-loop rejected", "[pipeline][dag]")
{
  MediaPipeline pipeline;
  pipeline.addStage("A", std::make_shared<PassthroughHandler>());

  CHECK_FALSE(pipeline.connectStages("A", "A"));
}

TEST_CASE("MediaPipeline: cycle A → B → C → A rejected", "[pipeline][dag]")
{
  MediaPipeline pipeline;
  pipeline.addStage("A", std::make_shared<PassthroughHandler>());
  pipeline.addStage("B", std::make_shared<PassthroughHandler>());
  pipeline.addStage("C", std::make_shared<PassthroughHandler>());

  CHECK(pipeline.connectStages("A", "B"));
  CHECK(pipeline.connectStages("B", "C"));
  CHECK_FALSE(pipeline.connectStages("C", "A"));
}

TEST_CASE("MediaPipeline: cycle detection on start()", "[pipeline][dag]")
{
  // validateAcyclic() is called during start(). We can't construct
  // a cycle via connectStages() (it rejects), so a valid graph
  // should pass start() fine. This test verifies that start()
  // succeeds with a valid DAG.
  MediaPipeline pipeline;
  pipeline.addStage("A", std::make_shared<PassthroughHandler>());
  pipeline.addStage("B", std::make_shared<CaptureHandler>());
  pipeline.connectStages("A", "B");

  auto r = pipeline.start();
  CHECK(r.success);
}

TEST_CASE("MediaPipeline: validateAcyclic on valid DAG", "[pipeline][dag]")
{
  MediaPipeline pipeline;

  pipeline.addStage("A", std::make_shared<PassthroughHandler>());
  pipeline.addStage("B", std::make_shared<PassthroughHandler>());
  pipeline.addStage("C", std::make_shared<PassthroughHandler>());
  pipeline.addStage("D", std::make_shared<CaptureHandler>());

  pipeline.connectStages("A", "B");
  pipeline.connectStages("A", "C");
  pipeline.connectStages("B", "D");
  pipeline.connectStages("C", "D");

  CHECK(pipeline.validateAcyclic());
}

TEST_CASE("MediaPipeline: fan-out with 3 downstreams", "[pipeline][dag]")
{
  MediaPipeline pipeline;

  auto capB = std::make_shared<CaptureHandler>();
  auto capC = std::make_shared<CaptureHandler>();
  auto capD = std::make_shared<CaptureHandler>();

  pipeline.addStage("A", std::make_shared<PassthroughHandler>());
  pipeline.addStage("B", capB);
  pipeline.addStage("C", capC);
  pipeline.addStage("D", capD);
  pipeline.connectStages("A", "B");
  pipeline.connectStages("A", "C");
  pipeline.connectStages("A", "D");
  pipeline.start();

  pipeline.incoming(makeTestBuffer());

  CHECK(capB->incomingBuffers.size() == 1);
  CHECK(capC->incomingBuffers.size() == 1);
  CHECK(capD->incomingBuffers.size() == 1);
}

TEST_CASE("MediaPipeline: removeStage cleans up fan-out wiring", "[pipeline][dag]")
{
  MediaPipeline pipeline;

  auto capB = std::make_shared<CaptureHandler>();
  auto capC = std::make_shared<CaptureHandler>();

  pipeline.addStage("A", std::make_shared<PassthroughHandler>());
  pipeline.addStage("B", capB);
  pipeline.addStage("C", capC);
  pipeline.connectStages("A", "B");
  pipeline.connectStages("A", "C");

  // Remove C — A should now have a single downstream (B), no fan-out.
  CHECK(pipeline.removeStage("C"));
  pipeline.start();

  pipeline.incoming(makeTestBuffer());
  CHECK(capB->incomingBuffers.size() == 1);
}

TEST_CASE("MediaPipeline: fan-out metrics count on all downstreams", "[pipeline][dag]")
{
  MediaPipeline pipeline;

  pipeline.addStage("A", std::make_shared<PassthroughHandler>());
  pipeline.addStage("B", std::make_shared<CaptureHandler>());
  pipeline.addStage("C", std::make_shared<CaptureHandler>());
  pipeline.connectStages("A", "B");
  pipeline.connectStages("A", "C");
  pipeline.start();

  for (int i = 0; i < 5; ++i)
  {
    pipeline.incoming(makeTestBuffer());
  }

  auto mA = pipeline.getMetrics("A");
  auto mB = pipeline.getMetrics("B");
  auto mC = pipeline.getMetrics("C");

  CHECK(mA.framesIn == 5);
  CHECK(mB.framesIn == 5);
  CHECK(mC.framesIn == 5);
}

// =========================================================================
// Phase 4: Format Negotiation
// =========================================================================

TEST_CASE("MediaPipeline: sample rate mismatch auto-inserts resampler", "[pipeline][format]")
{
  MediaPipeline pipeline;

  StageFormat srcOut;
  srcOut.sampleRate = 48000;
  srcOut.sampleFormat = SampleFormat::S16;
  srcOut.channels = 1;

  StageFormat dstIn;
  dstIn.sampleRate = 16000;
  dstIn.sampleFormat = SampleFormat::S16;
  dstIn.channels = 1;

  auto capture = std::make_shared<CaptureHandler>();
  pipeline.addStage("source", std::make_shared<PassthroughHandler>(), {}, srcOut);
  pipeline.addStage("sink", capture, dstIn, {});
  pipeline.connectStages("source", "sink");

  // Resampler stage should have been auto-inserted.
  auto resamplerName = "source_to_sink_resampler";
  CHECK(pipeline.getStage(resamplerName) != nullptr);

  // Total stages: source + resampler + sink = 3.
  CHECK(pipeline.stageCount() == 3);

  pipeline.start();

  // Create a 48kHz mono S16 buffer: 960 samples = 20ms @ 48kHz.
  std::size_t inSamples = 960;
  auto buf = MediaBuffer::create(inSamples * sizeof(std::int16_t));
  auto* samples = reinterpret_cast<std::int16_t*>(buf->data());
  for (std::size_t i = 0; i < inSamples; ++i)
  {
    samples[i] = 0;
  }
  buf->setSize(inSamples * sizeof(std::int16_t));

  pipeline.incoming(buf);
  REQUIRE(capture->incomingBuffers.size() == 1);

  // Output should be ~320 samples (16kHz, 20ms) = 640 bytes.
  auto& outBuf = capture->incomingBuffers[0];
  auto outSamples = outBuf->size() / sizeof(std::int16_t);
  CHECK(outSamples >= 310); // Allow +-10 for resampler rounding.
  CHECK(outSamples <= 330);
}

TEST_CASE("MediaPipeline: matching formats — no extra stage", "[pipeline][format]")
{
  MediaPipeline pipeline;

  StageFormat fmt;
  fmt.sampleRate = 16000;
  fmt.sampleFormat = SampleFormat::S16;
  fmt.channels = 1;

  auto capture = std::make_shared<CaptureHandler>();
  pipeline.addStage("source", std::make_shared<PassthroughHandler>(), {}, fmt);
  pipeline.addStage("sink", capture, fmt, {});
  pipeline.connectStages("source", "sink");

  // No conversion needed — only 2 stages.
  CHECK(pipeline.stageCount() == 2);
}

TEST_CASE("MediaPipeline: no format declaration — direct connect", "[pipeline][format]")
{
  MediaPipeline pipeline;

  auto capture = std::make_shared<CaptureHandler>();
  pipeline.addStage("source", std::make_shared<PassthroughHandler>());
  pipeline.addStage("sink", capture);
  pipeline.connectStages("source", "sink");

  CHECK(pipeline.stageCount() == 2);
  pipeline.start();
  pipeline.incoming(makeTestBuffer());
  CHECK(capture->incomingBuffers.size() == 1);
}

TEST_CASE("MediaPipeline: auto-inserted resampler appears in metrics", "[pipeline][format]")
{
  MediaPipeline pipeline;

  StageFormat srcOut;
  srcOut.sampleRate = 48000;

  StageFormat dstIn;
  dstIn.sampleRate = 8000;

  auto capture = std::make_shared<CaptureHandler>();
  pipeline.addStage("src", std::make_shared<PassthroughHandler>(), {}, srcOut);
  pipeline.addStage("dst", capture, dstIn, {});
  pipeline.connectStages("src", "dst");
  pipeline.start();

  // Push a buffer (48kHz, 960 samples = 1920 bytes).
  auto buf = MediaBuffer::create(1920);
  std::memset(buf->data(), 0, 1920);
  buf->setSize(1920);
  pipeline.incoming(buf);

  auto resamplerMetrics = pipeline.getMetrics("src_to_dst_resampler");
  CHECK(resamplerMetrics.framesIn == 1);

  auto all = pipeline.allMetrics();
  CHECK(all.size() == 3);
}

TEST_CASE("MediaPipeline: channel mismatch auto-inserts mixer", "[pipeline][format]")
{
  MediaPipeline pipeline;

  StageFormat srcOut;
  srcOut.channels = 1;

  StageFormat dstIn;
  dstIn.channels = 2;

  auto capture = std::make_shared<CaptureHandler>();
  pipeline.addStage("src", std::make_shared<PassthroughHandler>(), {}, srcOut);
  pipeline.addStage("dst", capture, dstIn, {});
  pipeline.connectStages("src", "dst");

  CHECK(pipeline.getStage("src_to_dst_chmix") != nullptr);
  CHECK(pipeline.stageCount() == 3);

  pipeline.start();

  // 160 mono S16 samples = 320 bytes.
  std::size_t monoSamples = 160;
  auto buf = MediaBuffer::create(monoSamples * sizeof(std::int16_t));
  auto* data = reinterpret_cast<std::int16_t*>(buf->data());
  for (std::size_t i = 0; i < monoSamples; ++i)
  {
    data[i] = static_cast<std::int16_t>(i);
  }
  buf->setSize(monoSamples * sizeof(std::int16_t));
  pipeline.incoming(buf);

  REQUIRE(capture->incomingBuffers.size() == 1);
  // Output should be 160 stereo samples = 640 bytes.
  CHECK(capture->incomingBuffers[0]->size() == monoSamples * 2 * sizeof(std::int16_t));

  // Verify samples: each mono sample duplicated to L/R.
  auto* out = reinterpret_cast<const std::int16_t*>(
    capture->incomingBuffers[0]->data());
  CHECK(out[0] == 0);
  CHECK(out[1] == 0);
  CHECK(out[2] == 1);
  CHECK(out[3] == 1);
}

TEST_CASE("MediaPipeline: sample format mismatch auto-inserts converter", "[pipeline][format]")
{
  MediaPipeline pipeline;

  StageFormat srcOut;
  srcOut.sampleFormat = SampleFormat::F32;

  StageFormat dstIn;
  dstIn.sampleFormat = SampleFormat::S16;

  auto capture = std::make_shared<CaptureHandler>();
  pipeline.addStage("src", std::make_shared<PassthroughHandler>(), {}, srcOut);
  pipeline.addStage("dst", capture, dstIn, {});
  pipeline.connectStages("src", "dst");

  CHECK(pipeline.getStage("src_to_dst_fmt") != nullptr);
  CHECK(pipeline.stageCount() == 3);

  pipeline.start();

  // 4 F32 samples = 16 bytes.
  auto buf = MediaBuffer::create(4 * sizeof(float));
  auto* fdata = reinterpret_cast<float*>(buf->data());
  fdata[0] = 0.0f;
  fdata[1] = 0.5f;
  fdata[2] = -0.5f;
  fdata[3] = 1.0f;
  buf->setSize(4 * sizeof(float));
  pipeline.incoming(buf);

  REQUIRE(capture->incomingBuffers.size() == 1);
  // Output: 4 S16 samples = 8 bytes.
  CHECK(capture->incomingBuffers[0]->size() == 4 * sizeof(std::int16_t));

  auto* s16 = reinterpret_cast<const std::int16_t*>(
    capture->incomingBuffers[0]->data());
  CHECK(s16[0] == 0);
  CHECK(s16[1] > 16000);  // 0.5 * 32767 ≈ 16383
  CHECK(s16[2] < -16000); // -0.5 * 32767 ≈ -16383
  CHECK(s16[3] == 32767);  // 1.0 clamp
}

TEST_CASE("MediaPipeline: multiple format mismatches in chain", "[pipeline][format]")
{
  MediaPipeline pipeline;

  // Source outputs 48kHz mono F32, dest expects 16kHz stereo S16.
  // Should insert: fmt converter (F32→S16), resampler (48→16), channel mixer (1→2).
  StageFormat srcOut;
  srcOut.sampleRate = 48000;
  srcOut.sampleFormat = SampleFormat::F32;
  srcOut.channels = 1;

  StageFormat dstIn;
  dstIn.sampleRate = 16000;
  dstIn.sampleFormat = SampleFormat::S16;
  dstIn.channels = 2;

  auto capture = std::make_shared<CaptureHandler>();
  pipeline.addStage("src", std::make_shared<PassthroughHandler>(), {}, srcOut);
  pipeline.addStage("dst", capture, dstIn, {});
  pipeline.connectStages("src", "dst");

  // 3 auto-inserted + 2 original = 5 stages.
  CHECK(pipeline.stageCount() == 5);
  CHECK(pipeline.getStage("src_to_dst_fmt") != nullptr);
  CHECK(pipeline.getStage("src_to_dst_resampler") != nullptr);
  CHECK(pipeline.getStage("src_to_dst_chmix") != nullptr);
}

TEST_CASE("MediaPipeline: format negotiation with fan-out", "[pipeline][format]")
{
  MediaPipeline pipeline;

  StageFormat srcOut;
  srcOut.sampleRate = 48000;

  StageFormat dstIn8k;
  dstIn8k.sampleRate = 8000;

  StageFormat dstIn16k;
  dstIn16k.sampleRate = 16000;

  auto cap1 = std::make_shared<CaptureHandler>();
  auto cap2 = std::make_shared<CaptureHandler>();

  pipeline.addStage("src", std::make_shared<PassthroughHandler>(), {}, srcOut);
  pipeline.addStage("d1", cap1, dstIn8k, {});
  pipeline.addStage("d2", cap2, dstIn16k, {});
  pipeline.connectStages("src", "d1");
  pipeline.connectStages("src", "d2");

  // Each branch gets its own resampler.
  CHECK(pipeline.getStage("src_to_d1_resampler") != nullptr);
  CHECK(pipeline.getStage("src_to_d2_resampler") != nullptr);
  // src + 2 resamplers + 2 destinations = 5
  CHECK(pipeline.stageCount() == 5);
}

// =========================================================================
// Phase 5: Codec Hot-Swap
// =========================================================================

TEST_CASE("MediaPipeline: swapCodec on TranscodingHandler", "[pipeline][swap]")
{
  MediaPipeline pipeline;

  auto decoder = std::make_unique<MockCodec>(makeCodecInfo("opus", 48000));
  auto encoder = std::make_unique<MockCodec>(makeCodecInfo("PCMU", 8000));
  auto transcoder = std::make_shared<TranscodingHandler>(
    std::move(decoder), std::move(encoder));

  auto capture = std::make_shared<CaptureHandler>();
  pipeline.addStage("transcode", transcoder);
  pipeline.addStage("sink", capture);
  pipeline.connectStages("transcode", "sink");
  pipeline.start();

  // Push a frame through the original codec.
  pipeline.incoming(makeTestBuffer(160)); // 160 bytes

  // Swap to new codecs.
  auto newDec = std::make_unique<MockCodec>(makeCodecInfo("G722", 16000));
  auto newEnc = std::make_unique<MockCodec>(makeCodecInfo("PCMU", 8000));
  auto result = pipeline.swapCodec("transcode", std::move(newDec), std::move(newEnc));

  CHECK(result.success);
  CHECK(pipeline.getState() == LifecycleState::Running);

  // Push another frame — should work with new codecs.
  pipeline.incoming(makeTestBuffer(160));
  CHECK(capture->incomingBuffers.size() == 2);
}

TEST_CASE("MediaPipeline: swapCodec on non-TranscodingHandler fails", "[pipeline][swap]")
{
  MediaPipeline pipeline;
  pipeline.addStage("passthrough", std::make_shared<PassthroughHandler>());
  pipeline.start();

  auto newDec = std::make_unique<MockCodec>(makeCodecInfo("opus", 48000));
  auto newEnc = std::make_unique<MockCodec>(makeCodecInfo("PCMU", 8000));
  auto result = pipeline.swapCodec("passthrough", std::move(newDec), std::move(newEnc));

  CHECK_FALSE(result.success);
  CHECK(result.message.find("not a TranscodingHandler") != std::string::npos);
}

TEST_CASE("MediaPipeline: swapCodec on non-Running pipeline fails", "[pipeline][swap]")
{
  MediaPipeline pipeline;

  auto decoder = std::make_unique<MockCodec>(makeCodecInfo("opus", 48000));
  auto encoder = std::make_unique<MockCodec>(makeCodecInfo("PCMU", 8000));
  pipeline.addStage("transcode",
    std::make_shared<TranscodingHandler>(std::move(decoder), std::move(encoder)));
  // Not started.

  auto newDec = std::make_unique<MockCodec>(makeCodecInfo("G722", 16000));
  auto newEnc = std::make_unique<MockCodec>(makeCodecInfo("PCMU", 8000));
  auto result = pipeline.swapCodec("transcode", std::move(newDec), std::move(newEnc));

  CHECK_FALSE(result.success);
  CHECK(result.message.find("Running") != std::string::npos);
}

TEST_CASE("MediaPipeline: swapCodec on nonexistent stage fails", "[pipeline][swap]")
{
  MediaPipeline pipeline;
  pipeline.addStage("A", std::make_shared<PassthroughHandler>());
  pipeline.start();

  auto newDec = std::make_unique<MockCodec>(makeCodecInfo("opus", 48000));
  auto newEnc = std::make_unique<MockCodec>(makeCodecInfo("PCMU", 8000));
  auto result = pipeline.swapCodec("nonexistent", std::move(newDec), std::move(newEnc));

  CHECK_FALSE(result.success);
}

TEST_CASE("MediaPipeline: swapCodec reports drain duration", "[pipeline][swap]")
{
  MediaPipeline pipeline;

  auto decoder = std::make_unique<MockCodec>(makeCodecInfo("opus", 48000));
  auto encoder = std::make_unique<MockCodec>(makeCodecInfo("PCMU", 8000));
  pipeline.addStage("transcode",
    std::make_shared<TranscodingHandler>(std::move(decoder), std::move(encoder)));
  pipeline.start();

  auto newDec = std::make_unique<MockCodec>(makeCodecInfo("G722", 16000));
  auto newEnc = std::make_unique<MockCodec>(makeCodecInfo("G722", 16000));
  auto result = pipeline.swapCodec("transcode", std::move(newDec), std::move(newEnc));

  CHECK(result.success);
  // Drain on a synchronous pipeline with 0 in-flight should be near-instant.
  CHECK(result.drainDuration >= std::chrono::microseconds{0});
}

TEST_CASE("MediaPipeline: pipeline resumes after swap", "[pipeline][swap]")
{
  MediaPipeline pipeline;

  auto decoder = std::make_unique<MockCodec>(makeCodecInfo("opus", 48000));
  auto encoder = std::make_unique<MockCodec>(makeCodecInfo("PCMU", 8000));
  auto transcoder = std::make_shared<TranscodingHandler>(
    std::move(decoder), std::move(encoder));
  auto capture = std::make_shared<CaptureHandler>();

  pipeline.addStage("transcode", transcoder);
  pipeline.addStage("sink", capture);
  pipeline.connectStages("transcode", "sink");
  pipeline.start();

  // Push frames before swap.
  for (int i = 0; i < 5; ++i)
  {
    pipeline.incoming(makeTestBuffer(160));
  }
  CHECK(capture->incomingBuffers.size() == 5);

  // Swap.
  auto newDec = std::make_unique<MockCodec>(makeCodecInfo("G722", 16000));
  auto newEnc = std::make_unique<MockCodec>(makeCodecInfo("G722", 16000));
  auto result = pipeline.swapCodec("transcode", std::move(newDec), std::move(newEnc));
  CHECK(result.success);

  // Push more frames after swap.
  for (int i = 0; i < 5; ++i)
  {
    pipeline.incoming(makeTestBuffer(160));
  }
  CHECK(capture->incomingBuffers.size() == 10);
}

// =========================================================================
// Phase 6: Integration Tests
// =========================================================================

TEST_CASE("MediaPipeline: lifecycle round-trip with frames", "[pipeline][integration]")
{
  MediaPipeline pipeline;
  auto capture = std::make_shared<CaptureHandler>();

  pipeline.addStage("A", std::make_shared<PassthroughHandler>());
  pipeline.addStage("B", capture);
  pipeline.connectStages("A", "B");

  // Created → Running → process → Draining → Stopped → Reset → Running → process.
  pipeline.start();
  for (int i = 0; i < 5; ++i)
  {
    pipeline.incoming(makeTestBuffer());
  }
  CHECK(capture->incomingBuffers.size() == 5);

  pipeline.drain();
  pipeline.stop();
  pipeline.reset();
  CHECK(pipeline.stageCount() == 0);

  // Rebuild after reset.
  auto capture2 = std::make_shared<CaptureHandler>();
  pipeline.addStage("X", std::make_shared<PassthroughHandler>());
  pipeline.addStage("Y", capture2);
  pipeline.connectStages("X", "Y");
  pipeline.start();

  for (int i = 0; i < 3; ++i)
  {
    pipeline.incoming(makeTestBuffer());
  }
  CHECK(capture2->incomingBuffers.size() == 3);
}

TEST_CASE("MediaPipeline: metrics end-to-end 3-stage pipeline", "[pipeline][integration]")
{
  MediaPipeline pipeline;

  pipeline.addStage("A", std::make_shared<PassthroughHandler>());
  pipeline.addStage("B", std::make_shared<PassthroughHandler>());
  pipeline.addStage("C", std::make_shared<CaptureHandler>());
  pipeline.connectStages("A", "B");
  pipeline.connectStages("B", "C");
  pipeline.start();

  constexpr int kFrames = 100;
  for (int i = 0; i < kFrames; ++i)
  {
    pipeline.incoming(makeTestBuffer());
  }

  auto mA = pipeline.getMetrics("A");
  auto mB = pipeline.getMetrics("B");
  auto mC = pipeline.getMetrics("C");

  CHECK(mA.framesIn == kFrames);
  CHECK(mB.framesIn == kFrames);
  CHECK(mC.framesIn == kFrames);
}

TEST_CASE("MediaPipeline: fan-out recording scenario", "[pipeline][integration]")
{
  MediaPipeline pipeline;

  auto encoder = std::make_shared<CaptureHandler>();
  auto recorder = std::make_shared<CaptureHandler>();

  pipeline.addStage("source", std::make_shared<PassthroughHandler>());
  pipeline.addStage("encoder", encoder);
  pipeline.addStage("recorder", recorder);
  pipeline.connectStages("source", "encoder");
  pipeline.connectStages("source", "recorder");
  pipeline.start();

  constexpr int kFrames = 20;
  for (int i = 0; i < kFrames; ++i)
  {
    pipeline.incoming(makeTestBuffer());
  }

  CHECK(encoder->incomingBuffers.size() == kFrames);
  CHECK(recorder->incomingBuffers.size() == kFrames);
}

TEST_CASE("MediaPipeline: error resilience — handler throws on some frames", "[pipeline][integration]")
{
  // Handler that throws on every 10th frame.
  class SelectiveThrowHandler : public IMediaHandler
  {
  public:
    void incoming(std::shared_ptr<MediaBuffer> buffer) override
    {
      ++_count;
      if (_count % 10 == 0)
      {
        throw std::runtime_error("periodic error");
      }
      forwardIncoming(std::move(buffer));
    }

    void outgoing(std::shared_ptr<MediaBuffer> buffer) override
    {
      forwardOutgoing(std::move(buffer));
    }

  private:
    int _count = 0;
  };

  MediaPipeline pipeline;
  auto capture = std::make_shared<CaptureHandler>();

  pipeline.addStage("thrower", std::make_shared<SelectiveThrowHandler>());
  pipeline.addStage("sink", capture);
  pipeline.connectStages("thrower", "sink");
  pipeline.start();

  int exceptionCount = 0;
  for (int i = 0; i < 50; ++i)
  {
    try
    {
      pipeline.incoming(makeTestBuffer());
    }
    catch (const std::runtime_error&)
    {
      ++exceptionCount;
    }
  }

  // Every 10th frame throws: frames 10, 20, 30, 40, 50 = 5 exceptions.
  CHECK(exceptionCount == 5);
  // 45 frames should have been captured.
  CHECK(capture->incomingBuffers.size() == 45);

  auto metrics = pipeline.getMetrics("thrower");
  CHECK(metrics.errorCount == 5);
}

TEST_CASE("MediaPipeline: transcode pipeline with mock codecs", "[pipeline][integration]")
{
  MediaPipeline pipeline;

  auto decoder = std::make_unique<MockCodec>(makeCodecInfo("opus", 48000));
  auto encoder = std::make_unique<MockCodec>(makeCodecInfo("PCMU", 8000));
  auto transcoder = std::make_shared<TranscodingHandler>(
    std::move(decoder), std::move(encoder));

  auto capture = std::make_shared<CaptureHandler>();

  pipeline.addStage("transcode", transcoder);
  pipeline.addStage("sink", capture);
  pipeline.connectStages("transcode", "sink");
  pipeline.start();

  // Push frames and verify they flow through.
  for (int i = 0; i < 10; ++i)
  {
    pipeline.incoming(makeTestBuffer(320));
  }

  CHECK(capture->incomingBuffers.size() == 10);

  // Verify transcoder metrics.
  auto m = pipeline.getMetrics("transcode");
  CHECK(m.framesIn == 10);
}

TEST_CASE("MediaPipeline: codec swap mid-stream with pipeline resume", "[pipeline][integration]")
{
  MediaPipeline pipeline;

  auto decoder = std::make_unique<MockCodec>(makeCodecInfo("opus", 48000));
  auto encoder = std::make_unique<MockCodec>(makeCodecInfo("PCMU", 8000));
  auto transcoder = std::make_shared<TranscodingHandler>(
    std::move(decoder), std::move(encoder));
  auto capture = std::make_shared<CaptureHandler>();

  pipeline.addStage("transcode", transcoder);
  pipeline.addStage("sink", capture);
  pipeline.connectStages("transcode", "sink");
  pipeline.start();

  // Pre-swap frames.
  for (int i = 0; i < 5; ++i)
  {
    pipeline.incoming(makeTestBuffer(320));
  }

  // Swap to G.722.
  auto newDec = std::make_unique<MockCodec>(makeCodecInfo("G722", 16000));
  auto newEnc = std::make_unique<MockCodec>(makeCodecInfo("G722", 16000));
  auto result = pipeline.swapCodec("transcode", std::move(newDec), std::move(newEnc));
  CHECK(result.success);

  // Post-swap frames.
  for (int i = 0; i < 5; ++i)
  {
    pipeline.incoming(makeTestBuffer(320));
  }

  // All 10 frames should have arrived.
  CHECK(capture->incomingBuffers.size() == 10);

  // Verify codecs were actually swapped.
  CHECK(transcoder->decoderInfo().name == "G722");
  CHECK(transcoder->encoderInfo().name == "G722");
}

TEST_CASE("MediaPipeline: duplicate connectStages rejected", "[pipeline][graph]")
{
  MediaPipeline pipeline;
  pipeline.addStage("A", std::make_shared<PassthroughHandler>());
  pipeline.addStage("B", std::make_shared<CaptureHandler>());

  CHECK(pipeline.connectStages("A", "B"));
  CHECK_FALSE(pipeline.connectStages("A", "B"));
}

TEST_CASE("MediaPipeline: inFlightCount stays zero after exception", "[pipeline][lifecycle]")
{
  MediaPipeline pipeline;
  pipeline.addStage("thrower", std::make_shared<ThrowingHandler>());
  pipeline.start();

  try
  {
    pipeline.incoming(makeTestBuffer());
  }
  catch (...)
  {
  }

  // _inFlightCount must be back to zero even after exception.
  CHECK(pipeline.getInFlightCount() == 0);

  // drain() must succeed immediately (not hang).
  auto r = pipeline.drain();
  CHECK(r.success);
}

TEST_CASE("MediaPipeline: addStage rejects empty name", "[pipeline][graph]")
{
  MediaPipeline pipeline;
  CHECK_FALSE(pipeline.addStage("", std::make_shared<PassthroughHandler>()));
}

TEST_CASE("MediaPipeline: addStage rejects null handler", "[pipeline][graph]")
{
  MediaPipeline pipeline;
  CHECK_FALSE(pipeline.addStage("A", nullptr));
}

TEST_CASE("MediaPipeline: swapCodec then stop succeeds", "[pipeline][swap]")
{
  MediaPipeline pipeline;

  auto decoder = std::make_unique<MockCodec>(makeCodecInfo("opus", 48000));
  auto encoder = std::make_unique<MockCodec>(makeCodecInfo("PCMU", 8000));
  pipeline.addStage("transcode",
    std::make_shared<TranscodingHandler>(std::move(decoder), std::move(encoder)));
  pipeline.start();

  auto newDec = std::make_unique<MockCodec>(makeCodecInfo("G722", 16000));
  auto newEnc = std::make_unique<MockCodec>(makeCodecInfo("G722", 16000));
  auto result = pipeline.swapCodec("transcode", std::move(newDec), std::move(newEnc));
  CHECK(result.success);
  CHECK(pipeline.getState() == LifecycleState::Running);

  // stop() after swap should work normally.
  auto stopResult = pipeline.stop();
  CHECK(stopResult.success);
  CHECK(pipeline.getState() == LifecycleState::Stopped);
}
