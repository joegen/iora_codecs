# Submodule Dependencies

This document covers the proper installation and initialization of all git submodule
dependencies required to build iora_codecs.

## Prerequisites

The following tools must be installed before cloning and building:

### Required

| Tool | Minimum Version | Purpose |
|------|----------------|---------|
| cmake | 3.14 | Build system |
| g++ or clang++ | GCC 7+ / Clang 5+ | C++17 compiler |
| gcc or clang | same | C compiler (codec libraries) |
| git | 2.x | Submodule management |
| make | any | Build backend for CMake and ExternalProject targets |

### Optional (needed by specific codecs)

| Tool | Minimum Version | Required By | Fallback |
|------|----------------|-------------|----------|
| nasm | 2.13+ | OpenH264, libvpx, libaom, dav1d (asm optimizations) | Builds without asm — slower runtime |
| autoconf | 2.69 | opencore-amr, vo-amrwbenc | Cannot build AMR codecs |
| automake | 1.15 | opencore-amr, vo-amrwbenc | Cannot build AMR codecs |
| libtool | 2.x | opencore-amr, vo-amrwbenc | Cannot build AMR codecs |
| meson | 0.49+ | dav1d | Cannot build AV1 fast decoder |
| ninja | 1.7+ | dav1d (Meson default backend) | Cannot build dav1d |

### Verifying Prerequisites

```bash
# Required
cmake --version        # >= 3.14
g++ --version          # C++17 support
git --version          # >= 2.x

# Optional — check which are available
nasm --version         # for asm-optimized video codecs
autoconf --version     # for AMR codecs
automake --version
libtool --version
meson --version        # for dav1d (AV1)
ninja --version        # for dav1d
```

### Installing Prerequisites (Debian/Ubuntu)

```bash
# Required
sudo apt-get install -y cmake g++ git make

# For AMR codecs (opencore-amr, vo-amrwbenc)
sudo apt-get install -y autoconf automake libtool

# For asm-optimized video codecs
sudo apt-get install -y nasm

# For dav1d (AV1 decoder)
sudo apt-get install -y meson ninja-build
```

### Installing Prerequisites (RHEL/CentOS/Fedora)

```bash
sudo dnf install -y cmake gcc-c++ git make
sudo dnf install -y autoconf automake libtool
sudo dnf install -y nasm
sudo dnf install -y meson ninja-build
```

## Cloning the Repository

```bash
# Clone with all submodules in one step
git clone --recursive https://github.com/joegen/iora_codecs.git
cd iora_codecs

# Or clone first, then initialize submodules
git clone https://github.com/joegen/iora_codecs.git
cd iora_codecs
git submodule update --init --recursive
```

The `--recursive` flag is important because libilbc contains a nested abseil-cpp
submodule that must also be initialized.

## Submodule Overview

All external dependencies live under `libs/`:

```
libs/
  iora/          # Iora framework (header-only)
  opus/          # Opus audio codec
  speexdsp/      # SpeexDSP resampler
  libg722/       # G.722 wideband audio
  libilbc/       # iLBC audio codec
  opencore-amr/  # AMR-NB/WB decoder + NB encoder
  vo-amrwbenc/   # AMR-WB encoder
  openh264/      # H.264 video codec
  libvpx/        # VP8/VP9 video codec
  libaom/        # AV1 encoder/decoder
  bcg729/        # G.729 audio codec
  dav1d/         # AV1 fast decoder
```

### Submodule Sources and Pinned Versions

| Submodule | Source | Pinned Tag | License |
|-----------|--------|-----------|---------|
| iora | github.com/joegen/iora | main | MPL-2.0 |
| opus | github.com/xiph/opus | v1.5.2 | BSD-3-Clause |
| speexdsp | github.com/xiph/speexdsp | SpeexDSP-1.2.1 | BSD-3-Clause |
| libg722 | github.com/sippy/libg722 | v1.2.5 | Public domain |
| libilbc | github.com/TimothyGu/libilbc | main | BSD-3-Clause |
| opencore-amr | github.com/BelledonneCommunications/opencore-amr | master | Apache-2.0 |
| vo-amrwbenc | github.com/BelledonneCommunications/vo-amrwbenc | master | Apache-2.0 |
| openh264 | github.com/cisco/openh264 | v2.5.0 | BSD-2-Clause |
| libvpx | chromium.googlesource.com/webm/libvpx | v1.14.1 | BSD-3-Clause |
| libaom | aomedia.googlesource.com/aom | v3.10.0 | BSD-2-Clause |
| bcg729 | github.com/BelledonneCommunications/bcg729 | 1.1.1 | GPL-3.0* |
| dav1d | code.videolan.org/videolan/dav1d | 1.5.0 | BSD-2-Clause |

## Build Integration Methods

Each submodule is integrated into the CMake build using one of three methods:

### 1. CMake add_subdirectory (CMake-native libraries)

These submodules have their own `CMakeLists.txt` and integrate directly:

- **libs/opus** — cache variables suppress tests/programs before `add_subdirectory`
- **libs/bcg729** — cache variables suppress tests/strict before `add_subdirectory`
- **libs/libilbc** — requires abseil-cpp nested submodule

### 2. Direct source compilation (trivial libraries)

These submodules have few source files compiled directly into static library targets:

- **libs/speexdsp** — only `libspeexdsp/resample.c` is compiled; a generated
  `speexdsp_config_types.h` provides platform type definitions
- **libs/libg722** — `g722_encode.c` and `g722_decode.c` compiled directly

### 3. ExternalProject_Add (non-CMake build systems)

These submodules use autotools, Meson, or plain Makefile and are built in isolation
via CMake's `ExternalProject_Add`. Artifacts are installed to `build/external/`:

- **libs/opencore-amr** — autotools (`autoreconf -if && ./configure && make`)
- **libs/vo-amrwbenc** — autotools
- **libs/openh264** — plain Makefile (`make PREFIX=... install-static`)
- **libs/libvpx** — custom configure/make (`./configure && make`)
- **libs/libaom** — CMake, but run as ExternalProject to avoid polluting global
  `CMAKE_C_FLAGS` (libaom adds `-Werror` flags that break other submodules)
- **libs/dav1d** — Meson (`meson setup && meson compile && meson install`);
  requires both `meson` and `ninja`

## Building

### Default build (all codecs ON except AV1)

```bash
cmake -S . -B build
cmake --build build -j$(nproc)
```

### Full build (including AV1)

```bash
cmake -S . -B build -DIORA_CODECS_ENABLE_AV1=ON
cmake --build build -j$(nproc)
```

### Minimal build (Opus + G.711 + resampler only)

```bash
cmake -S . -B build \
  -DIORA_CODECS_ENABLE_G722=OFF \
  -DIORA_CODECS_ENABLE_ILBC=OFF \
  -DIORA_CODECS_ENABLE_AMR=OFF \
  -DIORA_CODECS_ENABLE_H264=OFF \
  -DIORA_CODECS_ENABLE_VPX=OFF
cmake --build build -j$(nproc)
```

### All CMake Options

| Option | Default | Description |
|--------|---------|-------------|
| `IORA_CODECS_ENABLE_OPUS` | ON | Opus audio codec |
| `IORA_CODECS_ENABLE_G711` | ON | G.711 mu-law/A-law (built-in, no submodule) |
| `IORA_CODECS_ENABLE_G722` | ON | G.722 wideband audio |
| `IORA_CODECS_ENABLE_ILBC` | ON | iLBC audio codec |
| `IORA_CODECS_ENABLE_AMR` | OFF | AMR-NB/WB (patent encumbered, requires autotools) |
| `IORA_CODECS_ENABLE_H264` | ON | H.264 via OpenH264 |
| `IORA_CODECS_ENABLE_VPX` | ON | VP8/VP9 via libvpx |
| `IORA_CODECS_ENABLE_G729` | OFF | G.729 via bcg729 (GPL-3.0, isolated via RTLD_LOCAL) |
| `IORA_CODECS_ENABLE_AV1` | OFF | AV1 via libaom + dav1d (requires meson + ninja) |
| `IORA_CODECS_BUILD_TESTS` | ON | Build test executables |
| `IORA_CODECS_BUILD_EXAMPLES` | OFF | Build example programs |

## Verifying the Build

Run the smoke test to verify all enabled codecs link and initialize correctly:

```bash
# Via CTest
cd build && ctest --output-on-failure

# Or directly
./build/tests/build_smoke_test
```

Expected output (default build, AMR disabled):

```
[PASS] iora: header-only library included
[PASS] speexdsp: resampler init/destroy OK
[PASS] opus: libopus 1.5.2
[PASS] g722: encoder new/destroy OK
[PASS] ilbc: encoder create/free OK
[PASS] openh264: version 2.5.0
[PASS] libvpx: v1.14.1
[PASS] g711: built-in (no external dependency)

=== Smoke test: 8/8 passed ===
```

With AMR enabled (`-DIORA_CODECS_ENABLE_AMR=ON`), two additional tests appear:

```
[PASS] opencore-amr: NB encoder init/exit OK
[PASS] vo-amrwbenc: WB encoder init/exit OK
```

## Troubleshooting

### Submodules not initialized

```
CMake Warning: IORA_CODECS_ENABLE_OPUS=ON but libs/opus not found.
```

Fix: `git submodule update --init --recursive`

### libilbc fails to compile (missing abseil)

```
fatal error: absl/types/span.h: No such file or directory
```

libilbc depends on abseil-cpp as a nested submodule. Fix:

```bash
cd libs/libilbc
git submodule update --init --recursive
```

### AMR codecs fail to configure

```
autoreconf: command not found
```

Install autotools: `sudo apt-get install -y autoconf automake libtool`

### dav1d fails to build

```
meson: command not found
```

Install Meson and Ninja: `sudo apt-get install -y meson ninja-build`

### OpenH264/libvpx/libaom slow without NASM

These libraries benefit from assembly optimizations that require NASM. Without it,
they build with C-only fallbacks that are functional but slower at runtime.

```bash
sudo apt-get install -y nasm
```

### Updating a submodule to a newer version

```bash
cd libs/<submodule>
git fetch --tags
git checkout <new-tag>
cd ../..
git add libs/<submodule>
git commit -m "Update <submodule> to <new-tag>"
```

## License Considerations

- **G.729 (bcg729)** is now available as an opt-in GPL-isolated plugin. bcg729 (GPL-3.0) is statically linked into mod_g729.so, which is loaded with `RTLD_LOCAL` to confine GPL symbols. The host application and all other modules remain permissively licensed. `IORA_CODECS_ENABLE_G729=OFF` by default — distributors shipping mod_g729.so must comply with GPL-3.0 for that binary only. G.729 patents expired worldwide by January 2017.
- **opencore-amr / vo-amrwbenc (AMR)** are Apache-2.0 but the AMR codec itself
  is patent-encumbered. Commercial deployment may require a patent license from
  Via Licensing.
- **OpenH264** is BSD-2-Clause but H.264 is patent-encumbered. Cisco's prebuilt
  binaries are royalty-free. Compiling from source may require a separate MPEG-LA
  license for commercial use.
- All other submodules use permissive licenses (BSD, ISC, Apache-2.0, Public domain).
