# iora_codecs

General-purpose audio and video codec library for media encoding, decoding, and DSP processing. Built on the [Iora](https://github.com/joegen/iora) C++17 framework. Supports both plain RTP (SIP) and WebRTC use cases.

## Features

### Audio Codecs

| Codec | Sample Rates | Bitrate | License | Default |
|-------|-------------|---------|---------|---------|
| Opus | 8-48 kHz | 6-510 kbps | BSD-3 | ON |
| G.711 u-law/A-law | 8 kHz | 64 kbps | Built-in | ON |
| G.722 | 16 kHz | 48/56/64 kbps | BSD-3 | ON |
| iLBC | 8 kHz | 13.3/15.2 kbps | BSD-3 | ON |
| AMR-NB/WB | 8/16 kHz | 4.75-23.85 kbps | Apache-2.0 | OFF |

### Video Codecs

| Codec | Description | License | Default |
|-------|-------------|---------|---------|
| H.264 | OpenH264 (Cisco), Baseline/CB profiles | BSD-2 | ON |
| VP8/VP9 | libvpx, patent-free | BSD-3 | ON |
| AV1 | libaom encoder + dav1d decoder | BSD-2 | OFF |

### DSP Processing

- **Resampling** -- arbitrary sample-rate conversion via libspeexdsp
- **Audio mixing** -- N-way conference mixing with N-1 semantics, three algorithms (average, saturating, AGC)
- **Gain/volume** -- linear and dB control with saturation clamping
- **Voice activity detection** -- energy-based VAD with hangover logic
- **DTMF generation** -- ITU-T Q.23 dual-tone synthesis (16 digits)
- **DTMF detection** -- Goertzel algorithm with ITU-T Q.24 twist limits
- **WAV file I/O** -- read/write standard RIFF WAV files, pipeline recording tap

### Pipeline Orchestration

- DAG-based media processing graph with named stages
- Codec hot-swap during active sessions
- Per-stage latency and frame metrics
- Format negotiation with auto-inserted conversion stages
- Lifecycle management (Created -> Running -> Draining -> Stopped -> Reset)

## Architecture

```
Layer 3  Pipeline     MediaPipeline, TranscodingHandler, AudioMixerHandler
Layer 2  Codec        ICodec, ICodecFactory, CodecRegistry, plugin modules
Layer 1  DSP          Resampler, AudioMixer, Gain, VAD, ToneGenerator, Goertzel, WAV
Layer 0  Core         MediaBuffer, MediaBufferPool, MediaClock, SampleFormat, PixelFormat
         (Iora)       Threading, timers, logging, config, lifecycle, crypto
```

## Building

### Prerequisites

- C++17 compiler (GCC 9+, Clang 10+, MSVC 2019+)
- CMake 3.20+
- Git (for submodules)

### Quick Build

```bash
git submodule update --init --recursive
cmake -S . -B build
cmake --build build -j$(nproc)
```

### Full Build (including AV1)

```bash
cmake -S . -B build -DIORA_CODECS_ENABLE_AV1=ON
cmake --build build -j$(nproc)
```

### Minimal Build (Opus + G.711 only)

```bash
cmake -S . -B build \
  -DIORA_CODECS_ENABLE_G722=OFF \
  -DIORA_CODECS_ENABLE_ILBC=OFF \
  -DIORA_CODECS_ENABLE_AMR=OFF \
  -DIORA_CODECS_ENABLE_H264=OFF \
  -DIORA_CODECS_ENABLE_VPX=OFF
cmake --build build -j$(nproc)
```

### CMake Options

| Option | Default | Description |
|--------|---------|-------------|
| `IORA_CODECS_ENABLE_OPUS` | ON | Opus audio codec |
| `IORA_CODECS_ENABLE_G711` | ON | G.711 mu-law/A-law (built-in) |
| `IORA_CODECS_ENABLE_G722` | ON | G.722 wideband audio |
| `IORA_CODECS_ENABLE_ILBC` | ON | iLBC low bitrate codec |
| `IORA_CODECS_ENABLE_AMR` | OFF | AMR-NB/WB (patent encumbered) |
| `IORA_CODECS_ENABLE_H264` | ON | H.264 via OpenH264 |
| `IORA_CODECS_ENABLE_VPX` | ON | VP8/VP9 via libvpx |
| `IORA_CODECS_ENABLE_AV1` | OFF | AV1 (requires meson + ninja) |

## Testing

```bash
# Run smoke test
./build/tests/build_smoke_test

# Run all tests via CTest
cd build && ctest --output-on-failure
```

## Project Structure

```
iora_codecs/
  include/iora/codecs/
    core/           MediaBuffer, MediaBufferPool, MediaClock
    format/         SampleFormat, PixelFormat
    codec/          ICodec, ICodecFactory, CodecRegistry, CodecInfo
    dsp/            Resampler, AudioMixer, Gain, VAD, ToneGenerator, Goertzel, WAV
    pipeline/       IMediaHandler, TranscodingHandler, AudioMixerHandler, MediaPipeline
  src/              Implementation files
  modules/          Registry and codec plugins (registry, opus, g711, g722, ilbc, amr, h264, vpx, av1)
  tests/            Unit and integration tests
  libs/             Third-party submodules
  docs/             Architecture document and programmer's manual
```

## Quick Start

```cpp
#include <iora/iora.hpp>
#include <iora/codecs/codec/codec_registry.hpp>

// Initialize Iora and load plugins
auto& svc = iora::IoraService::instanceRef();
svc.loadSingleModule("mod_codec_registry.so");  // load registry first
svc.loadSingleModule("mod_opus.so");      // auto-registers with registry
svc.loadSingleModule("mod_g711.so");      // auto-registers PCMU + PCMA

// Create codecs via the registry
auto& registry = svc.callExportedApi<iora::codecs::CodecRegistry&>("codecs.registry");
auto opusInfo = registry.findByName("opus");
auto encoder = registry.createEncoder(*opusInfo);
auto decoder = registry.createDecoder(*opusInfo);
```

See the [Programmer's Manual](docs/programmers_manual.md) for pipeline construction, DSP processing, and integration patterns.

## Documentation

- **[Programmer's Manual](docs/programmers_manual.md)** -- complete API reference with integration patterns
- **[Architecture](docs/architecture.json)** -- design document with layer descriptions and data flows
- **[Dependency Guide](docs/submodule-dependencies.md)** -- submodule integration details

## License

This project is licensed under the [Mozilla Public License 2.0](LICENSE).

Third-party dependencies use permissive licenses (BSD-2-Clause, BSD-3-Clause, Apache-2.0). See [NOTICE](NOTICE) for full attribution.
