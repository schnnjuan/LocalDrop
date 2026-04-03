# Development

## Prerequisites

LocalDrop is currently maintained as a C11 project built with CMake.

Ubuntu/Debian packages:

```bash
sudo apt-get update
sudo apt-get install -y \
  build-essential \
  cmake \
  curl \
  libcurl4-openssl-dev \
  libavahi-client-dev \
  libavahi-common-dev \
  libmicrohttpd-dev \
  libqrencode-dev \
  libssl-dev \
  pkg-config
```

## Build

```bash
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --parallel
```

## Run

```bash
./build/localdrop
```

## Test

Default:

```bash
ctest --test-dir build --output-on-failure
```

Hardening profile:

```bash
cmake -S . -B build-hardening -DBUILD_TESTING=ON -DLOCALDROP_ENABLE_HARDENING=ON
cmake --build build-hardening --parallel
ctest --test-dir build-hardening --output-on-failure
```

AddressSanitizer:

```bash
cmake -S . -B build-asan -DBUILD_TESTING=ON -DLOCALDROP_ENABLE_HARDENING=OFF -DLOCALDROP_ENABLE_ASAN=ON
cmake --build build-asan --parallel
ctest --test-dir build-asan --output-on-failure
```

UndefinedBehaviorSanitizer:

```bash
cmake -S . -B build-ubsan -DBUILD_TESTING=ON -DLOCALDROP_ENABLE_HARDENING=OFF -DLOCALDROP_ENABLE_UBSAN=ON
cmake --build build-ubsan --parallel
ctest --test-dir build-ubsan --output-on-failure
```

ThreadSanitizer:

```bash
cmake -S . -B build-tsan -DBUILD_TESTING=ON -DLOCALDROP_ENABLE_HARDENING=OFF -DLOCALDROP_ENABLE_TSAN=ON
cmake --build build-tsan --parallel
ctest --test-dir build-tsan --output-on-failure -L unit
```

## Repository layout

```text
.
|-- .github/workflows/ci.yml
|-- CMakeLists.txt
|-- src/
|-- tests/
|-- docs/
|-- assets/
|-- downloads/
`-- staging/
```

## Development expectations

- Keep behavior aligned with the documented API and security model.
- Prefer small, reviewable commits.
- Add or update tests when changing request validation, token handling, storage, or transfer behavior.
- Do not reintroduce direct HTML injection or unsafe JSON serialization patterns.
- Preserve the staging-to-commit storage flow for uploads.

## CI

GitHub Actions runs a matrix with:

- hardening
- ASan
- UBSan
- TSan unit coverage

Before opening a PR, run the most relevant local variant for your change set.
