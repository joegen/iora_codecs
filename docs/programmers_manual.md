# iora_codecs Programmer's Manual

## 1. Introduction

**iora_codecs** is a general-purpose audio and video codec library for media encoding, decoding, and DSP, built on the Iora C++17 framework. It supports both plain RTP (SIP) and WebRTC use cases.

### Architecture

The library is organized into four layers, each depending only on layers below it:

```
┌─────────────────────────────────────────────┐
│  Level 3: Media Pipeline                    │
│  Orchestration — connects codecs, DSP       │
│  stages, and external sources/sinks         │
├─────────────────────────────────────────────┤
│  Level 2: Codec                             │
│  Encode/decode implementations and registry │
├─────────────────────────────────────────────┤
│  Level 1: DSP                               │
│  Resampling, gain, VAD, DTMF, WAV I/O      │
├─────────────────────────────────────────────┤
│  Level 0: Iora Core (external)              │
│  Threading, timers, logging, config,        │
│  network, lifecycle, crypto                 │
└─────────────────────────────────────────────┘
```

Transport, signaling, RTP framing, and device I/O are out of scope — they belong in companion libraries (`iora_rtp`, `iora_io_device`) that share iora_codecs core types.

### Build

```bash
# Quick build (default codecs)
git submodule update --init --recursive
cmake -S . -B build
cmake --build build -j$(nproc)

# Full build (including AV1)
cmake -S . -B build -DIORA_CODECS_ENABLE_AV1=ON
cmake --build build -j$(nproc)

# Minimal build (Opus + G.711 + resampler only)
cmake -S . -B build \
  -DIORA_CODECS_ENABLE_G722=OFF \
  -DIORA_CODECS_ENABLE_ILBC=OFF \
  -DIORA_CODECS_ENABLE_AMR=OFF \
  -DIORA_CODECS_ENABLE_H264=OFF \
  -DIORA_CODECS_ENABLE_VPX=OFF
cmake --build build -j$(nproc)

# Run tests
cd build && ctest --output-on-failure
```

### CMake Options

| Option | Default | Description |
|--------|---------|-------------|
| `IORA_CODECS_ENABLE_OPUS` | ON | Opus audio codec |
| `IORA_CODECS_ENABLE_G711` | ON | G.711 (built-in) |
| `IORA_CODECS_ENABLE_G722` | ON | G.722 wideband |
| `IORA_CODECS_ENABLE_ILBC` | ON | iLBC |
| `IORA_CODECS_ENABLE_AMR` | OFF | AMR-NB/WB (patent encumbered) |
| `IORA_CODECS_ENABLE_H264` | ON | H.264 (OpenH264) |
| `IORA_CODECS_ENABLE_VPX` | ON | VP8/VP9 |
| `IORA_CODECS_ENABLE_G729` | OFF | G.729 (GPL-3.0, opt-in) |
| `IORA_CODECS_ENABLE_AV1` | OFF | AV1 (needs meson + ninja) |
| `IORA_CODECS_BUILD_TESTS` | ON | Build test executables |
| `IORA_CODECS_BUILD_EXAMPLES` | OFF | Build example programs |

Minimum required dependencies: Iora framework, libopus, libspeexdsp, G.711 (built-in).

---

## 2. Core Types

### SampleFormat

**Header:** `include/iora/codecs/format/sample_format.hpp`

Audio sample format descriptors and conversion utilities. Little-endian only.

```cpp
enum class SampleFormat : std::uint8_t {
  S16,    // Signed 16-bit integer (2 bytes)
  S32,    // Signed 32-bit integer (4 bytes)
  F32,    // 32-bit IEEE float (4 bytes)
  U8,     // Unsigned 8-bit integer (1 byte)
  Mulaw,  // G.711 μ-law (1 byte)
  Alaw    // G.711 A-law (1 byte)
};
```

**Free functions:**

```cpp
constexpr std::size_t bytesPerSample(SampleFormat fmt) noexcept;
constexpr bool isFloat(SampleFormat fmt) noexcept;
constexpr bool isInteger(SampleFormat fmt) noexcept;
constexpr bool isSigned(SampleFormat fmt) noexcept;
const char* sampleFormatToString(SampleFormat fmt) noexcept;

// Convert between sample formats. Throws std::invalid_argument for unsupported pairs.
void convertSamples(const void* src, SampleFormat srcFmt,
                    void* dst, SampleFormat dstFmt, std::size_t sampleCount);
```

**Supported conversion matrix:**

| From \ To | S16 | S32 | F32 | U8 | Mulaw | Alaw |
|-----------|-----|-----|-----|----|-------|------|
| S16       | —   | Yes | Yes | Yes | Yes  | Yes  |
| S32       | Yes | —   |     |    |       |      |
| F32       | Yes |     | —   |    |       |      |
| U8        | Yes |     |     | —  |       |      |
| Mulaw     | Yes |     |     |    | —     |      |
| Alaw      | Yes |     |     |    |       | —    |

S16 is the hub format — all conversions go through S16. F32→S16 uses saturation clamping to [-32768, 32767].

G.711 encode/decode functions are in the `detail::` namespace:

```cpp
namespace detail {
  constexpr std::int16_t kMulawToS16[256]; // μ-law decode table
  constexpr std::int16_t kAlawToS16[256];  // A-law decode table
  std::uint8_t s16ToMulaw(std::int16_t sample) noexcept;
  std::uint8_t s16ToAlaw(std::int16_t sample) noexcept;
}
```

### PixelFormat

**Header:** `include/iora/codecs/format/pixel_format.hpp`

Video pixel format descriptors.

```cpp
enum class PixelFormat : std::uint8_t {
  None,   // No format / unset
  I420,   // Planar YUV 4:2:0 (most common)
  NV12,   // Semi-planar YUV 4:2:0
  NV21,   // Semi-planar YVU 4:2:0
  YUY2,   // Packed YUV 4:2:2
  UYVY,   // Packed YUV 4:2:2
  RGB24,  // Packed RGB (3 bytes)
  BGR24,  // Packed BGR (3 bytes)
  RGBA32, // Packed RGBA (4 bytes)
  BGRA32, // Packed BGRA (4 bytes)
  P010    // 10-bit 4:2:0 planar (HDR)
};

struct ChromaSubsampling {
  std::uint8_t horizontal;  // e.g. 2 for 4:2:0
  std::uint8_t vertical;    // e.g. 2 for 4:2:0
};
```

**Free functions:**

```cpp
constexpr bool isPlanar(PixelFormat fmt) noexcept;
const char* pixelFormatToString(PixelFormat fmt) noexcept;
constexpr ChromaSubsampling chromaSubsampling(PixelFormat fmt) noexcept;
constexpr std::size_t bytesPerPixel(PixelFormat fmt) noexcept;  // 0 for planar
constexpr std::size_t bytesPerFrame(PixelFormat fmt, std::size_t width,
                                     std::size_t height) noexcept;
```

### MediaBuffer

**Header:** `include/iora/codecs/core/media_buffer.hpp`

The fundamental data container for audio and video frames throughout the library.

```cpp
class MediaBuffer {
public:
  explicit MediaBuffer(std::size_t capacity);
  static std::shared_ptr<MediaBuffer> create(std::size_t capacity);

  // Move-only (copy deleted)
  MediaBuffer(MediaBuffer&&) noexcept = default;
  MediaBuffer& operator=(MediaBuffer&&) noexcept = default;

  // Data access
  std::uint8_t* data() noexcept;
  const std::uint8_t* data() const noexcept;
  std::size_t size() const noexcept;       // Used bytes
  std::size_t capacity() const noexcept;   // Allocated bytes
  void setSize(std::size_t n) noexcept;    // Clamped to capacity

  // RTP metadata
  std::uint32_t timestamp() const noexcept;
  void setTimestamp(std::uint32_t ts) noexcept;
  std::uint16_t sequenceNumber() const noexcept;
  void setSequenceNumber(std::uint16_t seq) noexcept;
  std::uint32_t ssrc() const noexcept;
  void setSsrc(std::uint32_t ssrc) noexcept;
  std::uint8_t payloadType() const noexcept;
  void setPayloadType(std::uint8_t pt) noexcept;
  bool marker() const noexcept;
  void setMarker(bool m) noexcept;
  std::chrono::steady_clock::time_point captureTime() const noexcept;
  void setCaptureTime(std::chrono::steady_clock::time_point t) noexcept;

  // Video metadata
  std::uint32_t width() const noexcept;
  void setWidth(std::uint32_t w) noexcept;
  std::uint32_t height() const noexcept;
  void setHeight(std::uint32_t h) noexcept;
  std::uint32_t stride(std::size_t plane) const noexcept;   // 3 planes
  void setStride(std::size_t plane, std::uint32_t s) noexcept;
  PixelFormat pixelFormat() const noexcept;
  void setPixelFormat(PixelFormat fmt) noexcept;

  // Utilities
  void copyMetadataFrom(const MediaBuffer& other) noexcept;
  std::shared_ptr<MediaBuffer> clone() const;  // Deep copy
};
```

**Usage:**

```cpp
// Create a buffer for 320 S16 samples (640 bytes)
auto buf = MediaBuffer::create(640);
// ... fill data ...
buf->setSize(640);
buf->setTimestamp(12345);
buf->setSsrc(0xDEADBEEF);

// Clone for fan-out (independent deep copy)
auto copy = buf->clone();
```

### MediaBufferPool

**Header:** `include/iora/codecs/core/media_buffer_pool.hpp`

Pre-allocated buffer pool to avoid per-frame heap allocations on the hot path.

```cpp
class MediaBufferPool : public std::enable_shared_from_this<MediaBufferPool> {
public:
  MediaBufferPool(std::size_t poolSize, std::size_t bufferCapacity);
  ~MediaBufferPool();  // Asserts all buffers returned

  static std::shared_ptr<MediaBufferPool> create(std::size_t poolSize,
                                                  std::size_t bufferCapacity);

  std::shared_ptr<MediaBuffer> acquire();  // nullptr when exhausted
  std::size_t availableCount() const;
  std::size_t bufferCapacity() const noexcept;
};
```

`acquire()` returns a `shared_ptr` with a custom deleter that automatically recycles the buffer back to the pool when the last reference is released. Returns `nullptr` when all buffers are in use (non-blocking).

The pool must outlive all acquired buffers. When the pool is managed by `shared_ptr`, the custom deleter captures a `weak_ptr` to prevent use-after-free.

```cpp
auto pool = MediaBufferPool::create(64, 1024);  // 64 buffers, 1024 bytes each
auto buf = pool->acquire();
if (!buf) { /* pool exhausted */ }
buf->setSize(320);
// buf is automatically returned to pool when last shared_ptr goes out of scope
```

### MediaClock

**Header:** `include/iora/codecs/core/media_clock.hpp`

Maps between wall-clock time and RTP media timestamps.

```cpp
class MediaClock {
public:
  explicit MediaClock(std::uint32_t clockRate,
                      std::uint32_t baseTimestamp = 0) noexcept;

  std::uint32_t clockRate() const noexcept;
  std::uint32_t now() const noexcept;

  std::uint32_t toMediaTimestamp(
    std::chrono::steady_clock::time_point tp) const noexcept;
  std::chrono::steady_clock::time_point toWallClock(
    std::uint32_t mediaTs) const noexcept;

  std::int64_t elapsedSamples(
    std::chrono::steady_clock::time_point start,
    std::chrono::steady_clock::time_point end) const noexcept;

  double driftPpm(const MediaClock& other) const noexcept;
};
```

Common clock rates: 8000 (G.711, G.722), 16000 (AMR-WB), 48000 (Opus), 90000 (H.264, VP8/VP9, AV1).

### CodecInfo

**Header:** `include/iora/codecs/codec/codec_info.hpp`

Descriptor for codec identity and capabilities.

```cpp
enum class CodecType : std::uint8_t { Audio, Video };
const char* codecTypeToString(CodecType type) noexcept;

enum class CodecFeatures : std::uint16_t {
  None = 0, Fec = 1<<0, Dtx = 1<<1, Vad = 1<<2,
  Plc = 1<<3, Vbr = 1<<4, Cbr = 1<<5, Svc = 1<<6
};
// Bitfield operators: operator|, operator&, operator|=
constexpr bool hasFeature(CodecFeatures set, CodecFeatures flag) noexcept;

struct CodecInfo {
  std::string name;
  CodecType type = CodecType::Audio;
  std::string mediaSubtype;            // SDP encoding name
  std::uint32_t clockRate = 0;
  std::uint8_t channels = 1;
  std::uint8_t defaultPayloadType = 0; // 0 for dynamic PT
  std::uint32_t defaultBitrate = 0;    // bps
  std::chrono::microseconds frameSize{0};
  CodecFeatures features = CodecFeatures::None;

  bool operator==(const CodecInfo& other) const noexcept;
  bool operator!=(const CodecInfo& other) const noexcept;
  bool matches(const CodecInfo& other) const noexcept;
  // Identity comparison: name + clockRate + channels
};
```

---

## 3. Codec Layer

### ICodec

**Header:** `include/iora/codecs/codec/i_codec.hpp`

Abstract interface for all codec implementations.

```cpp
class ICodec {
public:
  virtual ~ICodec() = default;

  virtual const CodecInfo& info() const = 0;
  virtual std::shared_ptr<MediaBuffer> encode(const MediaBuffer& input) = 0;
  virtual std::shared_ptr<MediaBuffer> decode(const MediaBuffer& input) = 0;
  virtual std::shared_ptr<MediaBuffer> plc(std::size_t frameSamples) = 0;
  virtual bool setParameter(const std::string& key, std::uint32_t value) = 0;
  virtual std::uint32_t getParameter(const std::string& key) const = 0;
};
```

- `encode()`/`decode()` are the hot-path methods — called via virtual dispatch (~1-2ns overhead).
- `plc()` is decoder-side only (packet loss concealment). Encoders return `nullptr`.
- Lifecycle: construction = ready, destruction = cleanup. No `init()`/`shutdown()`.

### ICodecFactory

**Header:** `include/iora/codecs/codec/i_codec_factory.hpp`

```cpp
class ICodecFactory {
public:
  virtual ~ICodecFactory() = default;

  virtual const CodecInfo& codecInfo() const = 0;
  virtual bool supports(const CodecInfo& info) const = 0;
  virtual std::unique_ptr<ICodec> createEncoder(const CodecInfo& params) = 0;
  virtual std::unique_ptr<ICodec> createDecoder(const CodecInfo& params) = 0;
};
```

One factory per codec implementation. The factory is shared across sessions; `ICodec` instances are per-session.

### CodecRegistry

**Header:** `include/iora/codecs/codec/codec_registry.hpp`

Central registry for all available codecs. Thread-safe (mutex-guarded).

```cpp
class CodecRegistry {
public:
  CodecRegistry() = default;  // Non-copyable, non-movable

  void registerFactory(std::shared_ptr<ICodecFactory> factory);  // throws on duplicate
  void unregisterFactory(const std::string& name);

  std::unique_ptr<ICodec> createEncoder(const CodecInfo& info);  // nullptr if not found
  std::unique_ptr<ICodec> createDecoder(const CodecInfo& info);

  std::vector<CodecInfo> enumerateCodecs() const;
  std::optional<CodecInfo> findByName(const std::string& name) const;
  std::optional<CodecInfo> findByPayloadType(std::uint8_t pt) const;
};
```

The registry is not a singleton — it lives inside the `mod_codec_registry` Iora plugin module. Load `mod_codec_registry` before any codec modules so they can auto-register their factories:

```cpp
// Host application startup — load registry first, then codecs
auto& svc = iora::IoraService::instanceRef();
svc.loadSingleModule("mod_codec_registry.so");   // exports "codecs.registry"
svc.loadSingleModule("mod_g711.so");       // auto-registers PCMU + PCMA
svc.loadSingleModule("mod_opus.so");       // auto-registers opus
// ... load additional codec modules as needed
```

Once loaded, the registry is accessible via the exported API:

```cpp
// Retrieve the registry from any plugin or host code
auto& registry = svc.callExportedApi<CodecRegistry&>("codecs.registry");

// Look up and create codecs
auto info = registry.findByName("opus");
auto encoder = registry.createEncoder(*info);
auto decoder = registry.createDecoder(*info);

// Enumerate all registered codecs
auto allCodecs = registry.enumerateCodecs();

// Convenience APIs exported directly by mod_codec_registry
auto codecs = svc.callExportedApi<std::vector<CodecInfo>>("codecs.registry.enumerate");
auto pcmu = svc.callExportedApi<std::optional<CodecInfo>>(
  "codecs.registry.findByName", std::string("PCMU"));
auto pt0 = svc.callExportedApi<std::optional<CodecInfo>>(
  "codecs.registry.findByPayloadType", static_cast<std::uint8_t>(0));
```

Each codec module's `onLoad()` calls `service->callExportedApi<CodecRegistry&>("codecs.registry")` and registers its factory automatically. If `mod_codec_registry` is not loaded, the codec module still works — its factory is accessible via its own exported API (e.g., `"g711.pcmu.factory"`, `"opus.factory"`), but it will not appear in the central registry.

### Codec Modules

| Module | SDP Name | PT | Clock Rate | License | Features | CMake Option |
|--------|----------|----|------------|---------|----------|-------------|
| Opus | opus | dynamic | 48000 | BSD-3 | FEC, DTX, VBR, CBR | `ENABLE_OPUS` |
| G.711 μ-law | PCMU | 0 | 8000 | Built-in | — | `ENABLE_G711` |
| G.711 A-law | PCMA | 8 | 8000 | Built-in | — | `ENABLE_G711` |
| G.722 | G722 | 9 | 8000* | BSD-2 | — | `ENABLE_G722` |
| iLBC | iLBC | dynamic | 8000 | BSD-3 | PLC | `ENABLE_ILBC` |
| G.729 | G729 | 18 | 8000 | GPL-3.0* | PLC, CBR | `ENABLE_G729` |
| AMR-NB | AMR | dynamic | 8000 | Apache-2.0 | DTX | `ENABLE_AMR` |
| AMR-WB | AMR-WB | dynamic | 16000 | Apache-2.0 | DTX | `ENABLE_AMR` |
| H.264 | H264 | dynamic | 90000 | BSD-2 | SVC | `ENABLE_H264` |
| VP8 | VP8 | dynamic | 90000 | BSD-3 | — | `ENABLE_VPX` |
| VP9 | VP9 | dynamic | 90000 | BSD-3 | SVC | `ENABLE_VPX` |
| AV1 | AV1 | dynamic | 90000 | BSD-2 | SVC | `ENABLE_AV1` |

*G.722 uses RTP clock rate 8000 per RFC 3551 despite 16kHz audio bandwidth.

*G.729 uses bcg729 (GPL-3.0). GPL scope confined to mod_g729.so via RTLD_LOCAL.

**Opus parameters:** `setParameter("bitrate", value)`, `setParameter("complexity", 0-10)`, `setParameter("fec", 0|1)`, `setParameter("dtx", 0|1)`.

**H.264/VP8/VP9/AV1 parameters:** `setParameter("bitrate", bps)`, `setParameter("speed", 0-10)`, `setParameter("keyframe", 1)` to request key frame.

---

## 4. IMediaHandler and Handler Basics

**Header:** `include/iora/codecs/pipeline/i_media_handler.hpp`

All processing nodes in the library implement `IMediaHandler`. This is the chain-of-responsibility base class that all DSP handlers, codec handlers, and pipeline stages inherit from.

```cpp
class IMediaHandler {
public:
  virtual ~IMediaHandler() = default;

  virtual void incoming(std::shared_ptr<MediaBuffer> buffer);  // default: forward
  virtual void outgoing(std::shared_ptr<MediaBuffer> buffer);  // default: forward

  void addToChain(std::shared_ptr<IMediaHandler> next);
  IMediaHandler& chainWith(std::shared_ptr<IMediaHandler> handler);  // fluent

protected:
  void forwardIncoming(std::shared_ptr<MediaBuffer> buffer);  // null-safe
  void forwardOutgoing(std::shared_ptr<MediaBuffer> buffer);
  std::shared_ptr<IMediaHandler> _next;
};
```

**Direction semantics:**
- `incoming()` — media arriving from an external source toward the application (e.g., received → decoded)
- `outgoing()` — media going from the application toward an external sink (e.g., raw → encoded)

**Chaining handlers:**

```cpp
auto gain = std::make_shared<GainHandler>(2.0f);
auto vad = std::make_shared<VadHandler>(VadParams{}, VadMode::DROP_SILENT);
auto sink = std::make_shared<CaptureHandler>();

// Method 1: addToChain
gain->addToChain(vad);
vad->addToChain(sink);

// Method 2: fluent chainWith
gain->chainWith(vad).chainWith(sink);

// Push audio through the chain
gain->incoming(buffer);  // gain → vad → sink
```

**Handler categories:**

| Category | Behavior | Examples |
|----------|----------|---------|
| Transforming | Modifies buffer in-place, forwards | `GainHandler` |
| Gating | Conditionally forwards or drops | `VadHandler` (DROP_SILENT) |
| Marking | Modifies metadata only, always forwards | `VadHandler` (MARK_ONLY) |
| Passive tap | Observes only, always forwards unchanged | `WavRecorderHandler`, `GoertzelHandler` |

---

## 5. DSP Layer — Resampler and AudioMixer

### Resampler

**Header:** `include/iora/codecs/dsp/resampler.hpp`

Audio sample rate converter wrapping libspeexdsp.

```cpp
class Resampler {
public:
  Resampler(std::uint32_t inputRate, std::uint32_t outputRate,
            std::uint32_t channels = 1, int quality = 3);
  ~Resampler();

  // Move-only
  Resampler(Resampler&& other) noexcept;
  Resampler& operator=(Resampler&& other) noexcept;

  // S16 processing — inLen/outLen are in/out per-channel sample counts
  bool process(const std::int16_t* in, std::uint32_t& inLen,
               std::int16_t* out, std::uint32_t& outLen);

  // F32 processing
  bool processFloat(const float* in, std::uint32_t& inLen,
                    float* out, std::uint32_t& outLen);

  void reset();
  void setQuality(int quality);           // 0 (fastest) to 10 (best)
  int getQuality() const;
  bool setRate(std::uint32_t inputRate, std::uint32_t outputRate);

  std::uint32_t inputRate() const noexcept;
  std::uint32_t outputRate() const noexcept;
  std::uint32_t channels() const noexcept;
  int inputLatency() const;
  int outputLatency() const;

  static std::uint32_t estimateOutputSamples(
    std::uint32_t inputSamples, std::uint32_t inputRate,
    std::uint32_t outputRate);
};
```

**Usage:**

```cpp
Resampler resampler(48000, 8000);  // Opus → G.711

std::int16_t input[960];   // 20ms at 48kHz
std::int16_t output[160];  // 20ms at 8kHz
std::uint32_t inLen = 960, outLen = 160;
resampler.process(input, inLen, output, outLen);
```

### AudioMixer

**Header:** `include/iora/codecs/dsp/audio_mixer.hpp`

N-way audio mixing with per-participant N-1 output (each participant hears everyone else).

```cpp
enum class MixAlgorithm { SampleAverage, SaturatingAdd, AgcNormalized };

struct MixParams {
  std::uint32_t targetSampleRate = 16000;
  std::uint32_t channels = 1;
  std::uint32_t maxParticipants = 32;
  MixAlgorithm algorithm = MixAlgorithm::SampleAverage;
  float agcTargetLevel = 0.7f;
  std::uint32_t agcWindowFrames = 25;
  double driftThresholdPpm = 50.0;
  bool enableVad = false;
  float vadSilenceThreshold = 100.0f;
  std::uint32_t maxActiveSpeakers = 0;  // 0 = no limit
};

class AudioMixer {
public:
  explicit AudioMixer(const MixParams& params);

  void addParticipant(std::uint32_t participantId);
  void addParticipant(std::uint32_t participantId,
                      std::uint32_t inputSampleRate);  // auto-Resampler
  void removeParticipant(std::uint32_t participantId);

  void pushAudio(std::uint32_t participantId,
                 const std::shared_ptr<MediaBuffer>& buffer);
  std::shared_ptr<MediaBuffer> mixFor(std::uint32_t participantId);
  void clearBuffers();

  std::size_t participantCount() const noexcept;
  bool hasAudio(std::uint32_t participantId) const;
  const MixParams& params() const noexcept;

  // Clock drift detection
  void setParticipantClock(std::uint32_t participantId,
                           std::unique_ptr<MediaClock> clock);
  double driftPpm(std::uint32_t participantId) const;

  // VAD / dominant speaker
  std::uint32_t dominantSpeaker() const noexcept;
  bool isSpeaking(std::uint32_t participantId) const;
  using VadCallback = std::function<void(std::uint32_t participantId,
                                          bool speaking)>;
  void setVadCallback(VadCallback cb);
};
```

**Mixing algorithms:**

- **SampleAverage** — Divides by N-1 source count. Prevents clipping.
- **SaturatingAdd** — Sums with int16_t saturation clamping. Louder but clips with many participants.
- **AgcNormalized** — Auto-adjusts gain to `agcTargetLevel` after summing. Consistent output level.

All algorithms use int32_t accumulators to prevent overflow.

**Usage:**

```cpp
AudioMixer mixer(MixParams{});
mixer.addParticipant(1);
mixer.addParticipant(2);
mixer.addParticipant(3);

mixer.pushAudio(1, bufferFrom1);
mixer.pushAudio(2, bufferFrom2);
mixer.pushAudio(3, bufferFrom3);

// Participant 1 hears 2+3, participant 2 hears 1+3, etc.
auto mix1 = mixer.mixFor(1);  // Contains audio from participants 2 and 3
auto mix2 = mixer.mixFor(2);  // Contains audio from participants 1 and 3
```

---

## 6. DSP Layer — Gain, VAD, DTMF

### Gain

**Header:** `include/iora/codecs/dsp/gain.hpp`

Volume control with saturation clamping, mute/unmute, and dB conversion.

```cpp
class Gain {
public:
  explicit Gain(float gainFactor = 1.0f);  // throws for negative/NaN/infinity

  void apply(std::int16_t* samples, std::size_t sampleCount);  // in-place, saturates
  void apply(MediaBuffer& buffer);  // rejects odd-byte buffers

  void setGain(float factor);   // throws for negative/NaN/infinity
  float gain() const noexcept;
  float gainDb() const noexcept;   // -infinity for zero gain
  void setGainDb(float db);

  void mute();
  void unmute();
  bool isMuted() const noexcept;
};
```

`setGain()` while muted updates the stored unmute value — the new gain takes effect on `unmute()`.

**GainHandler** — IMediaHandler wrapper applying Gain to both directions:

```cpp
class GainHandler : public IMediaHandler {
public:
  explicit GainHandler(float gainFactor = 1.0f);

  void incoming(std::shared_ptr<MediaBuffer> buffer) override;
  void outgoing(std::shared_ptr<MediaBuffer> buffer) override;

  Gain& gain() noexcept;
  const Gain& gain() const noexcept;
};
```

**Usage:**

```cpp
// Standalone
Gain gain(2.0f);
std::vector<std::int16_t> samples = {100, -100, 500};
gain.apply(samples.data(), samples.size());
// samples = {200, -200, 1000}

// In pipeline
auto handler = std::make_shared<GainHandler>(0.5f);
handler->addToChain(nextHandler);
handler->incoming(buffer);  // Halves all samples, forwards
```

### VAD (Voice Activity Detection)

**Header:** `include/iora/codecs/dsp/vad.hpp`

Energy-based voice activity detection with hangover logic.

```cpp
struct VadParams {
  float silenceThresholdRms = 100.0f;
  std::uint32_t hangoverFrames = 10;
  std::uint32_t sampleRate = 16000;
};

struct VadResult {
  bool isActive = false;
  float rmsEnergy = 0.0f;
  float peakAmplitude = 0.0f;
};

class Vad {
public:
  explicit Vad(VadParams params = {});

  VadResult process(const std::int16_t* samples, std::size_t sampleCount);
  VadResult process(const MediaBuffer& buffer);  // rejects odd-byte
  void reset();
  const VadParams& params() const noexcept;
};
```

**Hangover logic:** After speech is detected, VAD stays active for `hangoverFrames` additional silence frames before transitioning to inactive. This prevents choppy detection during natural speech pauses.

**VadHandler** — IMediaHandler with two operating modes:

```cpp
enum class VadMode { DROP_SILENT, MARK_ONLY };

class VadHandler : public IMediaHandler {
public:
  explicit VadHandler(VadParams params = {},
                      VadMode mode = VadMode::DROP_SILENT);

  void incoming(std::shared_ptr<MediaBuffer> buffer) override;
  void outgoing(std::shared_ptr<MediaBuffer> buffer) override;

  Vad& vadIncoming() noexcept;
  Vad& vadOutgoing() noexcept;
  VadMode mode() const noexcept;
};
```

- **DROP_SILENT** — Silent frames are not forwarded. Speech frames pass through.
- **MARK_ONLY** — All frames forwarded. Speech onset frames get the RFC 3550 §5.1 marker bit set.

Separate `Vad` instances are used per direction to prevent cross-direction state interference.

### ToneGenerator

**Header:** `include/iora/codecs/dsp/tone_generator.hpp`

DTMF tone synthesizer per ITU-T Q.23.

```cpp
class ToneGenerator {
public:
  explicit ToneGenerator(std::uint32_t sampleRate = 8000);

  std::shared_ptr<MediaBuffer> generate(
    char digit, std::uint32_t durationMs, float amplitude = 0.5f) const;
  std::shared_ptr<MediaBuffer> generateSilence(std::uint32_t durationMs) const;
  std::vector<std::shared_ptr<MediaBuffer>> generateSequence(
    const std::string& digits, std::uint32_t toneDurationMs,
    std::uint32_t gapDurationMs, float amplitude = 0.5f) const;

  std::uint32_t sampleRate() const noexcept;
  static std::pair<std::uint32_t, std::uint32_t> dtmfFrequencies(char digit);
};
```

- Returns `nullptr` for invalid digits or zero duration.
- `generateSequence("123", 100, 50)` produces 5 buffers: tone, gap, tone, gap, tone.
- Supports all 16 DTMF digits: `0-9`, `*`, `#`, `A`, `B`, `C`, `D`.

### GoertzelDetector

**Header:** `include/iora/codecs/dsp/goertzel_detector.hpp`

DTMF detector using the Goertzel algorithm with ITU-T Q.24 twist limits.

```cpp
struct GoertzelParams {
  float energyThreshold = 1000.0f;
  float normalTwistDb = 4.0f;   // Max dB high > low
  float reverseTwistDb = 8.0f;  // Max dB low > high
  std::uint32_t minDurationMs = 40;
};

struct DtmfResult {
  bool detected = false;
  char digit = '\0';
  float lowFreqMagnitude = 0.0f;
  float highFreqMagnitude = 0.0f;
};

class GoertzelDetector {
public:
  explicit GoertzelDetector(std::uint32_t sampleRate = 8000,
                            GoertzelParams params = {});

  DtmfResult detect(const std::int16_t* samples, std::size_t sampleCount);
  DtmfResult detect(const MediaBuffer& buffer);
  void reset();
  std::uint32_t sampleRate() const noexcept;
  const GoertzelParams& params() const noexcept;
};
```

The detector tracks duration across frames — a digit is only reported after `minDurationMs` of consecutive detection.

**GoertzelHandler** — IMediaHandler passive tap with callback:

```cpp
class GoertzelHandler : public IMediaHandler {
public:
  using DtmfCallback = std::function<void(char digit, std::uint32_t durationMs)>;

  explicit GoertzelHandler(std::uint32_t sampleRate = 8000,
                           GoertzelParams params = {},
                           DtmfCallback callback = nullptr);

  void incoming(std::shared_ptr<MediaBuffer> buffer) override;
  void outgoing(std::shared_ptr<MediaBuffer> buffer) override;

  GoertzelDetector& detectorIncoming() noexcept;
  GoertzelDetector& detectorOutgoing() noexcept;
  void setCallback(DtmfCallback cb);
};
```

Audio is forwarded unchanged — the handler only observes. Separate detector instances per direction.

**DTMF round-trip example:**

```cpp
ToneGenerator gen(8000);
GoertzelDetector det(8000, GoertzelParams{.minDurationMs = 0});

auto tone = gen.generate('5', 100, 0.5f);
auto result = det.detect(*tone);
// result.detected == true, result.digit == '5'
```

---

## 7. DSP Layer — WAV File I/O

### WavWriter

**Header:** `include/iora/codecs/dsp/wav_writer.hpp`

WAV file writer with streaming writes and deferred header finalization.

```cpp
struct WavParams {
  std::uint32_t sampleRate = 16000;
  std::uint16_t channels = 1;
  std::uint16_t bitsPerSample = 16;  // Writer: 16 only
};

class WavWriter {
public:
  explicit WavWriter(const WavParams& params = WavParams{});
  // throws for sampleRate=0, channels=0, bitsPerSample!=16
  ~WavWriter();  // calls close()

  // Move-only
  WavWriter(WavWriter&& other) noexcept;
  WavWriter& operator=(WavWriter&& other) noexcept;

  bool open(const std::string& filePath);
  bool write(const std::int16_t* samples, std::size_t sampleCount);
  bool write(const MediaBuffer& buf);  // rejects odd-byte buffers
  void close();  // Finalizes header with actual sizes

  bool isOpen() const noexcept;
  std::uint32_t samplesWritten() const noexcept;
  std::uint32_t bytesWritten() const noexcept;
  std::uint32_t durationMs() const noexcept;
  const WavParams& params() const noexcept;
};
```

- `open()` writes a 44-byte RIFF header with placeholder sizes.
- `close()` seeks back to offsets 4 and 40 to write actual file/data sizes.
- Data chunk limited to `UINT32_MAX` bytes (~4 GB). Writes exceeding this return `false`.
- If the process crashes without `close()`, most WAV players can still read the file.

### WavReader

**Header:** `include/iora/codecs/dsp/wav_reader.hpp`

WAV file reader supporting 8/16/24-bit PCM.

```cpp
struct WavFileInfo {
  std::uint32_t sampleRate = 0;
  std::uint16_t channels = 0;
  std::uint16_t bitsPerSample = 0;
  std::uint32_t totalSamples = 0;
  std::uint32_t durationMs = 0;
  std::uint32_t dataOffset = 0;
  std::uint32_t dataSize = 0;
};

class WavReader {
public:
  WavReader() = default;
  ~WavReader();

  // Move-only
  WavReader(WavReader&& other) noexcept;
  WavReader& operator=(WavReader&& other) noexcept;

  bool open(const std::string& filePath);
  std::shared_ptr<MediaBuffer> read(std::size_t sampleCount);  // nullptr at EOF
  std::shared_ptr<MediaBuffer> readAll();
  void close();

  bool isOpen() const noexcept;
  const WavFileInfo& info() const noexcept;
  bool seek(std::uint32_t sampleOffset);
  std::uint32_t remaining() const noexcept;
};
```

- Validates RIFF/WAVE chunk IDs, `AudioFormat=1` (PCM only), and `bitsPerSample` in {8, 16, 24}.
- Skips unknown chunks between `fmt` and `data` (with RIFF pad byte handling for odd-sized chunks).
- `read(N)` returns fewer samples at end of file. Returns `nullptr` at EOF.

**Round-trip example:**

```cpp
// Write
WavWriter writer(WavParams{8000, 1, 16});
writer.open("/tmp/recording.wav");
writer.write(samples.data(), samples.size());
writer.close();

// Read back
WavReader reader;
reader.open("/tmp/recording.wav");
auto info = reader.info();  // sampleRate=8000, channels=1, etc.
auto buf = reader.readAll();
auto readback = readS16(*buf);
// readback == samples (exact match)
```

### WavRecorderHandler

**Header:** `include/iora/codecs/dsp/wav_writer.hpp`

Pipeline recording handler — passive tap that writes audio to a WAV file and forwards buffers unchanged.

```cpp
enum class RecordDirection { INCOMING, OUTGOING, BOTH };

class WavRecorderHandler : public IMediaHandler {
public:
  // Opens file immediately
  WavRecorderHandler(const WavParams& params, const std::string& filePath,
                     RecordDirection direction = RecordDirection::INCOMING);
  // Deferred start (no file opened)
  WavRecorderHandler(const WavParams& params,
                     RecordDirection direction = RecordDirection::INCOMING);

  void incoming(std::shared_ptr<MediaBuffer> buffer) override;
  void outgoing(std::shared_ptr<MediaBuffer> buffer) override;

  bool startRecording(const std::string& filePath);
  void stopRecording();
  bool isRecording() const noexcept;
  std::uint32_t durationMs() const noexcept;

  WavWriter& writer() noexcept;
  const WavWriter& writer() const noexcept;
};
```

- **Passive tap:** Writes to disk but always forwards the buffer unchanged — invisible to downstream stages.
- **Direction filtering:** `INCOMING` records only `incoming()` calls, `OUTGOING` only `outgoing()`, `BOTH` records both directions interleaved into a single mono file.
- After `stopRecording()`, buffers are still forwarded but not recorded.

**Recording patterns:**

```cpp
// Record incoming audio in a pipeline
auto recorder = std::make_shared<WavRecorderHandler>(
  WavParams{8000, 1, 16}, "/tmp/call.wav", RecordDirection::INCOMING);
// Insert anywhere: source -> recorder -> gain -> sink

// Start/stop mid-call
recorder->stopRecording();
recorder->startRecording("/tmp/call_part2.wav");

// Record after mixer (captures mixed N-1 output)
// mixer -> recorder -> encoder -> sink

// Record individual participants before mixer
// participant_A -> recorder_A -> mixer
// participant_B -> recorder_B -> mixer
```

---

## 8. Pipeline Layer

### StageMetrics

**Header:** `include/iora/codecs/pipeline/stage_metrics.hpp`

Per-stage performance metrics with lock-free atomic counters.

```cpp
struct StageMetricsSnapshot {
  std::string stageName;
  std::uint64_t framesIn = 0;
  std::uint64_t framesOut = 0;
  std::uint64_t framesDropped = 0;
  std::uint64_t errorCount = 0;
  std::chrono::microseconds totalLatencyUs{0};
  std::chrono::microseconds maxLatencyUs{0};
  std::chrono::microseconds minLatencyUs{std::chrono::microseconds::max()};

  double averageLatencyUs() const;
};

class StageMetrics {
public:
  explicit StageMetrics(std::string name);

  void recordIncoming(std::chrono::microseconds latency) noexcept;
  void recordOutgoing(std::chrono::microseconds latency) noexcept;
  void recordDrop() noexcept;
  void recordError() noexcept;

  StageMetricsSnapshot snapshot() const;
  const std::string& stageName() const noexcept;
};
```

### InstrumentedStage

IMediaHandler decorator that wraps any handler with automatic latency timing.

```cpp
class InstrumentedStage : public IMediaHandler {
public:
  InstrumentedStage(std::string name, std::shared_ptr<IMediaHandler> wrapped);

  void incoming(std::shared_ptr<MediaBuffer> buffer) override;
  void outgoing(std::shared_ptr<MediaBuffer> buffer) override;

  StageMetricsSnapshot snapshot() const;
  const std::string& stageName() const noexcept;
  std::shared_ptr<IMediaHandler> wrappedHandler();
  const std::shared_ptr<IMediaHandler>& wrappedHandler() const;
  bool hasDownstream() const noexcept;
};
```

`MediaPipeline` automatically wraps each stage with `InstrumentedStage`.

### TranscodingHandler

**Header:** `include/iora/codecs/pipeline/transcoding_handler.hpp`

Decodes from one codec, optionally resamples, and re-encodes to another.

```cpp
class TranscodingHandler : public IMediaHandler {
public:
  TranscodingHandler(std::unique_ptr<ICodec> decoder,
                     std::unique_ptr<ICodec> encoder,
                     std::uint32_t channels = 1);
  // throws std::invalid_argument for null decoder/encoder

  void incoming(std::shared_ptr<MediaBuffer> buffer) override;
  // Pipeline: decode → resample (if needed) → encode

  const CodecInfo& decoderInfo() const;
  const CodecInfo& encoderInfo() const;
  void swapCodecs(std::unique_ptr<ICodec> decoder,
                  std::unique_ptr<ICodec> encoder);
  // NOT thread-safe — pause media flow before swapping
  bool hasResampler() const noexcept;
};
```

A `Resampler` is automatically inserted when the decoder and encoder have different clock rates (e.g., Opus 48kHz → G.711 8kHz).

Metadata preservation: `timestamp`, `ssrc`, `sequenceNumber`, and `marker` are copied from input; `payloadType` is updated to match the encoder's codec.

### AudioMixerHandler

**Header:** `include/iora/codecs/pipeline/audio_mixer_handler.hpp`

Fan-in/fan-out terminal node for conference mixing. Does **not** use `_next` chain.

```cpp
class AudioMixerHandler : public IMediaHandler {
public:
  explicit AudioMixerHandler(const MixParams& params,
                              std::shared_ptr<MediaBufferPool> bufferPool = nullptr);

  void addParticipant(std::uint32_t ssrc,
                      std::shared_ptr<IMediaHandler> outputHandler,
                      ICodec* decoder = nullptr);
  void removeParticipant(std::uint32_t ssrc);

  void incoming(std::shared_ptr<MediaBuffer> buffer) override;
  // Identifies participant by buffer's SSRC

  void mix();  // Timer-driven — call periodically (e.g., every 20ms)

  std::size_t participantCount() const noexcept;
  std::size_t bufferCount(std::uint32_t ssrc) const;
};
```

- `incoming()` identifies participants by `MediaBuffer::ssrc()`. Unknown SSRCs are dropped.
- Bounded frame queue per participant (max 3 frames). Oldest dropped on overflow.
- Optional PLC via `decoder` parameter — synthesizes frames for missing participants during `mix()`.
- Each participant's mixed output is forwarded to their `outputHandler->incoming()`.

### MediaPipeline

**Header:** `include/iora/codecs/pipeline/media_pipeline.hpp`

DAG-based processing graph orchestrator.

```cpp
struct SwapResult {
  bool success = false;
  std::string message;
  std::chrono::microseconds drainDuration{0};
};

struct StageFormat {
  std::optional<std::uint32_t> sampleRate;
  std::optional<SampleFormat> sampleFormat;
  std::optional<std::uint8_t> channels;
};

class MediaPipeline : public iora::common::ILifecycleManaged {
public:
  explicit MediaPipeline(iora::core::ThreadPool* threadPool = nullptr,
                          iora::core::TimerService* timerService = nullptr);

  // Graph construction (Created or Stopped state only)
  bool addStage(const std::string& name, std::shared_ptr<IMediaHandler> handler);
  bool addStage(const std::string& name, std::shared_ptr<IMediaHandler> handler,
                const StageFormat& inputFormat, const StageFormat& outputFormat);
  bool connectStages(const std::string& sourceName, const std::string& destName);
  bool removeStage(const std::string& name);
  std::shared_ptr<IMediaHandler> getStage(const std::string& name) const;

  // Processing (Running state only)
  void incoming(std::shared_ptr<MediaBuffer> buffer);   // → entry stage
  void outgoing(std::shared_ptr<MediaBuffer> buffer);   // → exit stage

  // Metrics
  StageMetricsSnapshot getMetrics(const std::string& name) const;
  std::vector<StageMetricsSnapshot> allMetrics() const;
  std::size_t stageCount() const noexcept;

  // Codec hot-swap
  SwapResult swapCodec(const std::string& stageName,
                       std::unique_ptr<ICodec> newDecoder,
                       std::unique_ptr<ICodec> newEncoder);

  // Topology validation
  bool validateAcyclic() const;

  // Lifecycle (ILifecycleManaged)
  iora::common::LifecycleResult start() override;
  iora::common::LifecycleResult drain(std::uint32_t timeoutMs = 30000) override;
  iora::common::LifecycleResult stop() override;
  iora::common::LifecycleResult reset() override;
  iora::common::LifecycleState getState() const override;
  std::uint32_t getInFlightCount() const override;
};
```

**Entry/exit stage auto-detection:** The stage with no incoming edges is the entry stage (receives `incoming()` calls). The stage with no outgoing edges is the exit stage (receives `outgoing()` calls).

**Lifecycle state machine:** `Created → Running → Draining → Stopped → Reset → Created`

**Usage:**

```cpp
MediaPipeline pipeline;

auto source = std::make_shared<IMediaHandler>();
auto gain = std::make_shared<GainHandler>(2.0f);
auto sink = std::make_shared<CaptureHandler>();

pipeline.addStage("source", source);
pipeline.addStage("gain", gain);
pipeline.addStage("sink", sink);
pipeline.connectStages("source", "gain");
pipeline.connectStages("gain", "sink");

auto result = pipeline.start();
assert(result.success);

pipeline.incoming(buffer);  // source → gain → sink

auto metrics = pipeline.getMetrics("gain");
// metrics.framesIn, metrics.framesOut, etc.
```

---

## 9. Integration Patterns and Recipes

### Pattern 1: Simple Audio Pipeline

Decode received audio, adjust gain, and forward to playback.

```cpp
MediaPipeline pipeline;

auto source = std::make_shared<IMediaHandler>();
auto gain = std::make_shared<GainHandler>(1.5f);
auto sink = std::make_shared<CaptureHandler>();

pipeline.addStage("source", source);
pipeline.addStage("gain", gain);
pipeline.addStage("sink", sink);
pipeline.connectStages("source", "gain");
pipeline.connectStages("gain", "sink");
pipeline.start();

pipeline.incoming(decodedAudio);
```

### Pattern 2: VAD-Gated Gain

Filter silence before applying gain — saves processing on silent frames.

```cpp
auto source = std::make_shared<IMediaHandler>();
auto vad = std::make_shared<VadHandler>(VadParams{}, VadMode::DROP_SILENT);
auto gain = std::make_shared<GainHandler>(2.0f);
auto sink = std::make_shared<CaptureHandler>();

pipeline.addStage("source", source);
pipeline.addStage("vad", vad);
pipeline.addStage("gain", gain);
pipeline.addStage("sink", sink);
pipeline.connectStages("source", "vad");
pipeline.connectStages("vad", "gain");
pipeline.connectStages("gain", "sink");
pipeline.start();

// Speech frames: vad passes → gain doubles → sink receives
// Silence frames: vad drops → gain and sink never see them
```

### Pattern 3: DTMF Detection in Pipeline

Detect DTMF tones without modifying the audio stream.

```cpp
std::vector<char> detectedDigits;
auto cb = [&](char digit, std::uint32_t) {
  detectedDigits.push_back(digit);
};

auto detector = std::make_shared<GoertzelHandler>(8000, GoertzelParams{}, cb);
// Insert in pipeline: source → detector → sink
// Detector invokes callback on detection, forwards audio unchanged
```

### Pattern 4: DTMF Round-Trip (Standalone)

Generate and detect DTMF without a pipeline.

```cpp
ToneGenerator gen(8000);
GoertzelParams params{.minDurationMs = 0};

for (char d : std::string("0123456789*#ABCD")) {
  GoertzelDetector det(8000, params);
  auto tone = gen.generate(d, 100, 0.5f);
  auto result = det.detect(*tone);
  assert(result.detected && result.digit == d);
}
```

### Pattern 5: Multi-Stage DSP Pipeline

Combine VAD filtering, gain adjustment, and DTMF detection.

```cpp
// source → VAD(DROP_SILENT) → Gain(1.0) → GoertzelDetector → sink
// DTMF tones pass VAD (they have energy), get detected
// Silence is dropped by VAD, never reaches detector
```

### Pattern 6: Call Recording with Passive Tap

Record audio without affecting the processing chain.

```cpp
auto recorder = std::make_shared<WavRecorderHandler>(
  WavParams{8000, 1, 16}, "/tmp/call.wav", RecordDirection::INCOMING);

// source → decoder → recorder → gain → sink
// Recorder writes to file AND forwards unchanged buffer to gain
```

### Pattern 7: Record Individual Participants Before Mixer

Capture each participant's isolated audio.

```cpp
auto recA = std::make_shared<WavRecorderHandler>(
  WavParams{16000, 1, 16}, "/tmp/participant_a.wav");
auto recB = std::make_shared<WavRecorderHandler>(
  WavParams{16000, 1, 16}, "/tmp/participant_b.wav");

// participant_A → recA → mixer
// participant_B → recB → mixer
```

### Pattern 8: Record Mixed Output

Capture what a participant hears (the N-1 mix).

```cpp
// mixer → recorder → encoder → sink
// Recorder captures the fully mixed audio
```

### Pattern 9: Opus-to-G.711 Transcoding

```cpp
auto decoder = registry.createDecoder(opusInfo);
auto encoder = registry.createEncoder(pcmuInfo);
auto transcoder = std::make_shared<TranscodingHandler>(
  std::move(decoder), std::move(encoder));

// Auto-inserts Resampler (48kHz → 8kHz)
assert(transcoder->hasResampler());

transcoder->incoming(opusPacket);
// Output: G.711 μ-law frame at 8kHz
```

### Pattern 10: Codec Hot-Swap Mid-Call

Switch codecs without tearing down the pipeline.

```cpp
pipeline.start();
// ... processing with Opus ...

auto newDecoder = registry.createDecoder(g722Info);
auto newEncoder = registry.createEncoder(g722Info);
auto result = pipeline.swapCodec("transcoder",
  std::move(newDecoder), std::move(newEncoder));
assert(result.success);
// Pipeline continues with G.722 — seamless transition
```

### Pattern 11: N-Way Conference

```cpp
AudioMixerHandler mixer(MixParams{});

// Per-participant output chains
auto encodeA = std::make_shared<TranscodingHandler>(...);
auto encodeB = std::make_shared<TranscodingHandler>(...);

mixer.addParticipant(ssrcA, encodeA);
mixer.addParticipant(ssrcB, encodeB);

// Incoming audio identified by SSRC
mixer.incoming(bufferFromA);  // SSRC matches participant A
mixer.incoming(bufferFromB);

// Timer-driven mix — each participant gets N-1 output
mixer.mix();
// encodeA receives audio from B
// encodeB receives audio from A
```

### Pattern 12: Pipeline Metrics Inspection

```cpp
pipeline.start();

for (int i = 0; i < 100; ++i) {
  pipeline.incoming(generateFrame());
}

for (auto& m : pipeline.allMetrics()) {
  std::cout << m.stageName << ": "
            << m.framesIn << " in, "
            << m.framesOut << " out, "
            << m.averageLatencyUs() << " μs avg\n";
}
```

### Pattern 13: DAG Topologies

Fan-out (one source, multiple sinks):

```cpp
pipeline.addStage("source", source);
pipeline.addStage("recorder", recorder);
pipeline.addStage("encoder", encoder);
pipeline.connectStages("source", "recorder");
pipeline.connectStages("source", "encoder");
```

Fan-in (multiple sources, one sink):

```cpp
pipeline.connectStages("mic", "mixer");
pipeline.connectStages("file", "mixer");
```

### Pattern 14: Format Negotiation

Auto-insert conversion stages between mismatched formats.

```cpp
StageFormat opusFmt{48000, SampleFormat::S16, 1};
StageFormat g711Fmt{8000, SampleFormat::Mulaw, 1};

pipeline.addStage("decoder", decoder, {}, opusFmt);
pipeline.addStage("encoder", encoder, g711Fmt, {});
pipeline.connectStages("decoder", "encoder");
// Pipeline auto-inserts resampler between decoder and encoder
```

### Pattern 15: Bidirectional Processing

```cpp
// incoming: source → gain → sink (remote audio to local playback)
// outgoing: sink → gain → source (local audio to remote)

pipeline.incoming(receivedAudio);   // Pushes to entry stage
pipeline.outgoing(capturedAudio);   // Pushes to exit stage
```

### Pattern 16: PLC Fallback

```cpp
auto transcoder = std::make_shared<TranscodingHandler>(
  std::move(decoder), std::move(encoder));

// On corrupt/missing frame, decoder's plc() generates synthetic frame
// TranscodingHandler handles this transparently
transcoder->incoming(corruptPacket);
// Falls back to plc(frameSamples) → resamples → encodes
```

### Pattern 17: SSRC-Based Participant Routing

```cpp
AudioMixerHandler mixer(MixParams{});
mixer.addParticipant(0x1234, outputA);
mixer.addParticipant(0x5678, outputB);

auto buf = MediaBuffer::create(640);
buf->setSsrc(0x1234);  // Identifies as participant A
mixer.incoming(buf);     // Routed to participant A's buffer

auto unknown = MediaBuffer::create(640);
unknown->setSsrc(0x9999);  // Unknown SSRC
mixer.incoming(unknown);    // Dropped silently
```

---

## 10. Writing Custom Handlers

### Basic Skeleton

```cpp
class MyHandler : public IMediaHandler {
public:
  void incoming(std::shared_ptr<MediaBuffer> buffer) override
  {
    if (buffer)
    {
      // Process buffer here
    }
    forwardIncoming(std::move(buffer));
  }

  void outgoing(std::shared_ptr<MediaBuffer> buffer) override
  {
    if (buffer)
    {
      // Process buffer here
    }
    forwardOutgoing(std::move(buffer));
  }
};
```

Always call `forwardIncoming()`/`forwardOutgoing()` — even if you don't process the buffer. This ensures the chain is not broken. The forward methods are null-safe (no-op if `_next` is null).

### Transforming Handler

Modifies the buffer in-place before forwarding.

```cpp
class InvertHandler : public IMediaHandler {
public:
  void incoming(std::shared_ptr<MediaBuffer> buffer) override
  {
    if (buffer && buffer->size() % sizeof(std::int16_t) == 0)
    {
      auto* samples = reinterpret_cast<std::int16_t*>(buffer->data());
      std::size_t count = buffer->size() / sizeof(std::int16_t);
      for (std::size_t i = 0; i < count; ++i)
      {
        samples[i] = static_cast<std::int16_t>(-samples[i]);
      }
    }
    forwardIncoming(std::move(buffer));
  }
};
```

### Gating Handler

Conditionally drops frames.

```cpp
class ThresholdGate : public IMediaHandler {
public:
  void incoming(std::shared_ptr<MediaBuffer> buffer) override
  {
    if (buffer && meetsThreshold(*buffer))
    {
      forwardIncoming(std::move(buffer));
    }
    // Dropped frames: forwardIncoming is NOT called
  }
};
```

### Passive Tap

Observes without modifying. Always forwards the original buffer.

```cpp
class PacketCounter : public IMediaHandler {
public:
  void incoming(std::shared_ptr<MediaBuffer> buffer) override
  {
    ++_count;
    forwardIncoming(std::move(buffer));  // Forward unchanged
  }
  std::uint64_t count() const { return _count; }
private:
  std::uint64_t _count = 0;
};
```

### Direction-Independent State

When a handler processes both directions, use separate state per direction to avoid interference:

```cpp
class BidirectionalProcessor : public IMediaHandler {
public:
  void incoming(std::shared_ptr<MediaBuffer> buffer) override
  {
    _processorIn.process(*buffer);   // Incoming state
    forwardIncoming(std::move(buffer));
  }
  void outgoing(std::shared_ptr<MediaBuffer> buffer) override
  {
    _processorOut.process(*buffer);  // Outgoing state (separate!)
    forwardOutgoing(std::move(buffer));
  }
private:
  Processor _processorIn;
  Processor _processorOut;
};
```

### Testing Custom Handlers

Use the `CaptureHandler` pattern demonstrated throughout the test suite:

```cpp
class CaptureHandler : public IMediaHandler {
public:
  void incoming(std::shared_ptr<MediaBuffer> buffer) override
  {
    buffers.push_back(std::move(buffer));
  }
  void outgoing(std::shared_ptr<MediaBuffer> buffer) override
  {
    outBuffers.push_back(std::move(buffer));
  }
  std::vector<std::shared_ptr<MediaBuffer>> buffers;
  std::vector<std::shared_ptr<MediaBuffer>> outBuffers;
};

// Test
auto handler = std::make_shared<MyHandler>();
auto capture = std::make_shared<CaptureHandler>();
handler->addToChain(capture);

handler->incoming(makeS16Buffer({100, -100, 500}));
REQUIRE(capture->buffers.size() == 1);
auto result = readS16(*capture->buffers[0]);
// Verify result
```

### Thread Safety

`incoming()` and `outgoing()` execute synchronously in the caller's thread — there is no hidden queuing or thread hops. This keeps latency predictable. If a handler needs async processing (e.g., video encode on a worker thread), it must explicitly dispatch to `ThreadPool` and return immediately.

---

## Appendices

### Appendix A: Header File Index

| Header | Description |
|--------|-------------|
| `format/sample_format.hpp` | Audio sample format enum, conversion utilities, G.711 tables |
| `format/pixel_format.hpp` | Video pixel format enum, chroma subsampling, frame size calculations |
| `core/media_buffer.hpp` | MediaBuffer — fundamental audio/video frame container |
| `core/media_buffer_pool.hpp` | MediaBufferPool — pre-allocated buffer pool with auto-recycle |
| `core/media_clock.hpp` | MediaClock — RTP timestamp / wall-clock mapping |
| `codec/codec_info.hpp` | CodecType, CodecFeatures, CodecInfo descriptor |
| `codec/i_codec.hpp` | ICodec — abstract encode/decode/PLC interface |
| `codec/i_codec_factory.hpp` | ICodecFactory — abstract codec creation interface |
| `codec/codec_registry.hpp` | CodecRegistry — central codec lookup and creation |
| `dsp/resampler.hpp` | Resampler — sample rate conversion (libspeexdsp) |
| `dsp/audio_mixer.hpp` | AudioMixer — N-way conference mixing with N-1 output |
| `dsp/gain.hpp` | Gain + GainHandler — volume control with saturation |
| `dsp/vad.hpp` | Vad + VadHandler — voice activity detection |
| `dsp/tone_generator.hpp` | ToneGenerator — DTMF tone synthesis (ITU-T Q.23) |
| `dsp/goertzel_detector.hpp` | GoertzelDetector + GoertzelHandler — DTMF detection (ITU-T Q.24) |
| `dsp/wav_writer.hpp` | WavWriter + WavRecorderHandler — WAV recording |
| `dsp/wav_reader.hpp` | WavReader — WAV file parsing and playback |
| `pipeline/i_media_handler.hpp` | IMediaHandler — chain-of-responsibility base class |
| `pipeline/stage_metrics.hpp` | StageMetrics + InstrumentedStage — per-stage performance metrics |
| `pipeline/transcoding_handler.hpp` | TranscodingHandler — decode → resample → encode |
| `pipeline/audio_mixer_handler.hpp` | AudioMixerHandler — conference mixing pipeline node |
| `pipeline/media_pipeline.hpp` | MediaPipeline — DAG-based processing graph orchestrator |

### Appendix B: SampleFormat Conversion Matrix

All conversions route through S16 as the hub format.

| Source | Destination | Supported |
|--------|------------|-----------|
| S16 | S32, F32, U8, Mulaw, Alaw | Yes |
| S32 | S16 | Yes |
| F32 | S16 | Yes (saturation clamped) |
| U8 | S16 | Yes |
| Mulaw | S16 | Yes (lookup table) |
| Alaw | S16 | Yes (lookup table) |

### Appendix C: DTMF Frequency Table (ITU-T Q.23)

|       | 1209 Hz | 1336 Hz | 1477 Hz | 1633 Hz |
|-------|---------|---------|---------|---------|
| **697 Hz** | 1 | 2 | 3 | A |
| **770 Hz** | 4 | 5 | 6 | B |
| **852 Hz** | 7 | 8 | 9 | C |
| **941 Hz** | * | 0 | # | D |

### Appendix D: WAV Format Reference

44-byte RIFF WAV header layout:

| Offset | Size | Field | Value |
|--------|------|-------|-------|
| 0 | 4 | ChunkID | "RIFF" |
| 4 | 4 | ChunkSize | 36 + DataSize |
| 8 | 4 | Format | "WAVE" |
| 12 | 4 | Subchunk1ID | "fmt " |
| 16 | 4 | Subchunk1Size | 16 (PCM) |
| 20 | 2 | AudioFormat | 1 (PCM) |
| 22 | 2 | NumChannels | 1 or 2 |
| 24 | 4 | SampleRate | e.g. 8000, 16000, 48000 |
| 28 | 4 | ByteRate | SampleRate * NumChannels * BitsPerSample/8 |
| 32 | 2 | BlockAlign | NumChannels * BitsPerSample/8 |
| 34 | 2 | BitsPerSample | 16 |
| 36 | 4 | Subchunk2ID | "data" |
| 40 | 4 | Subchunk2Size | NumSamples * NumChannels * BitsPerSample/8 |
| 44 | ... | Data | PCM samples (little-endian) |

### Appendix E: Codec Module Summary

| Module | License | Default PT | Clock Rate | Features | CMake Option | Default |
|--------|---------|-----------|------------|----------|--------------|---------|
| Opus | BSD-3 | dynamic | 48000 | FEC, DTX, VBR, CBR | `ENABLE_OPUS` | ON |
| G.711 μ-law | Built-in | 0 | 8000 | — | `ENABLE_G711` | ON |
| G.711 A-law | Built-in | 8 | 8000 | — | `ENABLE_G711` | ON |
| G.722 | BSD-2 | 9 | 8000 | — | `ENABLE_G722` | ON |
| iLBC | BSD-3 | dynamic | 8000 | PLC | `ENABLE_ILBC` | ON |
| AMR-NB | Apache-2.0 | dynamic | 8000 | DTX | `ENABLE_AMR` | OFF |
| AMR-WB | Apache-2.0 | dynamic | 16000 | DTX | `ENABLE_AMR` | OFF |
| G.729 | GPL-3.0* | 18 | 8000 | PLC, CBR | `ENABLE_G729` | OFF |
| H.264 | BSD-2 | dynamic | 90000 | SVC | `ENABLE_H264` | ON |
| VP8 | BSD-3 | dynamic | 90000 | — | `ENABLE_VPX` | ON |
| VP9 | BSD-3 | dynamic | 90000 | SVC | `ENABLE_VPX` | ON |
| AV1 | BSD-2 | dynamic | 90000 | SVC | `ENABLE_AV1` | OFF |

*G.729 uses bcg729 (GPL-3.0). GPL scope confined to mod_g729.so via RTLD_LOCAL.

### Appendix F: Future Features (Not Yet Implemented)

These features are described in the architecture document but not yet available:

- **Static plugin linking** (`IORA_CODECS_STATIC_PLUGINS`) — link codec modules statically for single-binary deployment
- **Dual-file recording** — `RecordDirection::BOTH` writing separate files per direction
- **Full DAG topology** — named ports and dynamic routing in MediaPipeline
- **AEC** — acoustic echo cancellation (libspeexdsp or WebRTC AEC3)
- **Noise suppression** — RNNoise or libspeexdsp
- **Standalone AGC** — automatic gain control (mixer AGC via `AgcNormalized` is available)
- **Hardware codec acceleration** — VAAPI, NVENC, V4L2 M2M
- **Simulcast / SVC layer selection** — for video streams
- **EVS codec** — Enhanced Voice Services
- **Bandwidth estimation** — TWCC-based adaptive bitrate (requires iora_rtp)
