# Legend2 MVP

![Benchmark](https://github.com/your-username/my-game/actions/workflows/benchmark.yml/badge.svg)

A Legend of Mir 2 style MMORPG game implementation in C++20.

## Requirements

- CMake 3.16+ (3.19+ for presets)
- C++20 compatible compiler
- vcpkg (recommended for dependency management)
- flatc on PATH when building schemas

### Dependencies (from vcpkg.json)

Client:
- SDL2
- SDL2_image
- SDL2_ttf
- SDL2_mixer

Shared/server:
- asio (standalone, header-only)
- flatbuffers
- entt
- spdlog
- yaml-cpp
- nlohmann-json
- sqlite3
- libpqxx
- hiredis

## Building with vcpkg (Recommended)

### 1. Install vcpkg

```bash
# Clone vcpkg
git clone https://github.com/microsoft/vcpkg.git
cd vcpkg

# Bootstrap (Windows)
.\bootstrap-vcpkg.bat

# Bootstrap (Linux/macOS)
./bootstrap-vcpkg.sh

# Set environment variable
# Windows (PowerShell)
$env:VCPKG_ROOT = "C:\path\to\vcpkg"

# Linux/macOS
export VCPKG_ROOT=/path/to/vcpkg
```

### 2. Configure and build with presets

```bash
# Configure (uses vcpkg toolchain automatically)
cmake --preset vcpkg-debug

# Build
cmake --build --preset vcpkg-debug
```

Optional release build:

```bash
cmake --preset vcpkg-release
cmake --build --preset vcpkg-release
```

Build output lands in `build/<preset>/bin`. Client assets from `Data/`, `Map/`, `Wav/`, and `MUSIC/` are copied into the same bin directory by CMake.

## Building without vcpkg

```bash
cmake -S . -B build
cmake --build build
```

Make sure dependencies are installed in your environment and visible via `CMAKE_PREFIX_PATH` or module-specific hints like `ASIO_INCLUDE_DIR`.

### Build Options

- `BUILD_TESTS` - Build tests (default: ON)
- `BUILD_SERVER` - Build server binaries (default: ON)
- `BUILD_TOOLS` - Build tools (default: ON)
- `BUILD_BENCHMARKS` - Build benchmarks (default: OFF)
- `ASIO_INCLUDE_DIR` - Path to Asio headers if not in system path

## Project Structure

```
├── src/
│   ├── client/     # Game client
│   ├── server/     # Game server
│   └── common/     # Shared code
├── tests/          # Unit and property tests
├── Data/           # Game resources (WIL/WIX)
├── Map/            # Map files
├── Wav/            # Sound effects
└── MUSIC/          # Background music
```

## Running

After building:

```bash
# vcpkg preset (Debug)
build/vcpkg-debug/bin/mir2_gateway
build/vcpkg-debug/bin/mir2_world
build/vcpkg-debug/bin/mir2_db
build/vcpkg-debug/bin/mir2_game
build/vcpkg-debug/bin/legend2_client

# Manual build
build/bin/mir2_gateway
build/bin/mir2_world
build/bin/mir2_db
build/bin/mir2_game
build/bin/legend2_client
```

## Tests

```bash
ctest --test-dir build/vcpkg-debug --output-on-failure
```

## Performance Benchmarks

Performance targets:
- Damage calculation: < 100 ns
- Range check: < 50 ns

Run benchmarks with vcpkg presets:

```bash
cmake --preset vcpkg-benchmark
cmake --build --preset vcpkg-benchmark --target combat_core_benchmark
build/vcpkg-benchmark/bin/Release/combat_core_benchmark --benchmark_format=json --benchmark_out=benchmark_results.json --benchmark_min_time=1s
python scripts/check_benchmark.py benchmark_results.json
```

## License

Private project - All rights reserved.
