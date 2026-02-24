# arma-tools

License: Free for non-commercial community use.
Commercial use requires permission from the author.

Third-party licenses track in `licenses/`; consult `LEGALD.md` for the overall legal notice and interoperability commitments before redistributing binaries.

Build and packaging helpers are exposed via `Makefile`.

## Packaging Examples

### Linux package (default)

```bash
make package
```

### Linux regular package (wrp2project without TV4L)

```bash
make package-regular
```

### Linux creator package (wrp2project with TV4L)

```bash
make package-creator
```

## Windows Cross-Packaging

Windows targets use Docker by default and now build a derived image with dependencies preinstalled.

### Preflight checks only

```bash
make package-windows-preflight
```

### Build/update derived Windows builder image

```bash
make package-windows-docker-image
```

### Configure Windows cross-build directory

```bash
make package-windows-configure
```

### Build portable Windows ZIP

```bash
make package-windows-zip
```

### Build Windows NSIS installer

```bash
make package-windows-installer
```

### Use host MinGW toolchain instead of Docker

```bash
make package-windows-zip WINDOWS_USE_DOCKER=OFF
```

### Native Linux cross-build requirements (without Docker)

Required tools:

- `cmake`
- `cpack`
- `ninja`
- `x86_64-w64-mingw32-gcc`
- `x86_64-w64-mingw32-g++`
- `x86_64-w64-mingw32-windres`

For ZIP packaging only (`WINDOWS_BUILD_GUI=OFF`), the list above is enough.

For NSIS installer packaging (`make package-windows-installer WINDOWS_USE_DOCKER=OFF`), also install:

- `makensis`

For GUI builds in host mode (`WINDOWS_BUILD_GUI=ON`), also install:

- `x86_64-w64-mingw32-pkg-config`
- MinGW pkg-config modules: `gtkmm-4.0`, `epoxy`, `vorbisfile`, `libpanel-1`, `libadwaita-1`

Quick preflight check in host mode:

```bash
make package-windows-preflight WINDOWS_USE_DOCKER=OFF
```

## Windows Options

### Build Windows GUI too (default is OFF)

```bash
make package-windows-zip WINDOWS_BUILD_GUI=ON
```

Note: Docker GUI builds require MinGW pkg-config modules `gtkmm-4.0`, `epoxy`, `vorbisfile`, `libpanel-1`, and `libadwaita-1`.

### Override Docker images

```bash
make package-windows-zip \
  WINDOWS_DOCKER_BASE_IMAGE=debian:trixie-slim \
  WINDOWS_DOCKER_IMAGE=arma-tools/windows-builder:latest
```

### Skip auto-rebuilding derived Docker image

```bash
make package-windows-zip WINDOWS_DOCKER_BUILD=OFF
```

### Override toolchain/generator (host mode only)

```bash
make package-windows-zip \
  WINDOWS_USE_DOCKER=OFF \
  WINDOWS_CMAKE_GENERATOR=Ninja \
  WINDOWS_CMAKE_ARGS='-DCMAKE_TOOLCHAIN_FILE=/abs/path/to/toolchain.cmake'
```

Defaults:

- `WINDOWS_TOOLCHAIN_PREFIX=x86_64-w64-mingw32`
- `WINDOWS_CMAKE_GENERATOR=Ninja`
- `WINDOWS_BUILD_GUI=OFF`
- `WINDOWS_USE_DOCKER=ON`
- `WINDOWS_DOCKER_BASE_IMAGE=debian:trixie-slim`
- `WINDOWS_DOCKER_IMAGE=arma-tools/windows-builder:latest`
- `WINDOWS_DOCKER_BUILD=ON`
- `WINDOWS_DOCKER_AS_HOST_USER=ON`
- `WINDOWS_CMAKE_ARGS=-DCMAKE_TOOLCHAIN_FILE=$(CURDIR)/cmake/toolchains/mingw64.cmake` (used when `WINDOWS_USE_DOCKER=OFF`)

## Clean

```bash
make clean
```

## GUI Material Preview

RVMat and model material preview behavior is documented in:

- `docs/gui-material-preview.md`
