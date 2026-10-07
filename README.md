# lynt_db

<p align="center">
  <img src="docs/lynt-logo.svg" alt="lynt" width="160" />
</p>

![CI](https://github.com/cdieckhoff/lynt_db/actions/workflows/ci.yml/badge.svg)

An embeddable storage engine targeting near-O(1) reads and writes via
memory-mapped sparse files, with a row-oriented page format that
supports adding and removing columns without rebuilding tables.

## Status

| Library | Purpose | Verified |
|---------|---------|----------|
| lib_m | Universal OS layer: file I/O, memory mapping, sparse files, aligned allocation | Yes |
| lib_simd | Portable SIMD with runtime dispatch to scalar, SSE4.2, AVX2, and NEON | Yes |
| lib_lynt | Public database API | In progress |
| lynt_db | Storage engine | In progress |

## Verification matrix

Every push builds and tests on all six OS and architecture combinations:

| Platform | Architecture | SIMD backend | test_m | test_simd |
|----------|--------------|--------------|--------|-----------|
| Linux | x86-64 | AVX2 | 16627 / 0 | 1733 / 0 |
| Linux | ARM64 | NEON | 16627 / 0 | 1733 / 0 |
| Windows | x86-64 | AVX2 | 16627 / 0 | 1733 / 0 |
| Windows | ARM64 | NEON | 16627 / 0 | 1733 / 0 |
| macOS | ARM64 (Apple Silicon) | NEON | 16627 / 0 | 1733 / 0 |
| macOS | x86-64 (Intel) | AVX2 | 16627 / 0 | 1733 / 0 |

Debug and Release builds both run. Every SIMD backend produces
byte-identical results to the scalar reference.

## The libraries

### lib_m — universal OS layer

Presents POSIX file and memory-mapping semantics on every platform.
Hides the Windows 64 KiB allocation granularity behind POSIX-shaped
offsets, so callers map at any page-aligned offset regardless of the
underlying OS.

Key features:

- File lifecycle, sequential and positional I/O
- Memory mapping with granularity hiding on Windows
- Sparse file support: hole punching, hole detection, allocated size
- Aligned allocation
- Thread-local error messages, compile-time togglable

### lib_simd — portable SIMD with runtime dispatch

One API, four backends, chosen at runtime by CPU feature detection.
The caller writes one vocabulary and gets the best available speed on
any host.

- scalar: portable C, correct on any CPU
- SSE4.2: x86-64 baseline (2008 and later)
- AVX2: x86-64 optimized (2013 and later)
- NEON: ARM64 (Apple Silicon, Windows on ARM, Linux ARM)

Operations: memory primitives, bitmask-returning search and compare,
bitmap operations, hashing, and portable mask helpers.

## Building

From the project root:

    cmake --preset debug
    cmake --build --preset debug
    ctest --preset debug

The presets write to build/(preset)/(hostSystemName), so WSL, Windows,
and macOS build trees coexist without colliding.

Visual Studio opens the folder directly and picks up the presets for
F5 debugging.

## Requirements

- CMake 3.20 or later
- Ninja
- A C11 compiler: GCC, Clang, or MSVC 2019 and later

## License

TBD
