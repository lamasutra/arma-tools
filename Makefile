.PHONY: all gui debug release test clean configure configure-release package package-regular package-creator package-windows-preflight package-windows-docker-image package-windows-configure package-windows-installer package-windows-zip

BUILD_DEBUG   = build/debug
BUILD_RELEASE = build/release
BUILD_PACKAGE = build/package
BUILD_PACKAGE_REGULAR = build/package-regular
BUILD_PACKAGE_CREATOR = build/package-creator
BUILD_PACKAGE_WINDOWS = build/package-windows
BUILD_PACKAGE_WINDOWS_DOCKER = build/package-windows-docker
WINDOWS_CMAKE_GENERATOR ?= Ninja
WINDOWS_BUILD_GUI ?= OFF
WINDOWS_CMAKE_ARGS ?= -DCMAKE_TOOLCHAIN_FILE=$(CURDIR)/cmake/toolchains/mingw64.cmake
WINDOWS_TOOLCHAIN_PREFIX ?= x86_64-w64-mingw32
WINDOWS_USE_DOCKER ?= ON
WINDOWS_DOCKER_BASE_IMAGE ?= debian:trixie-slim
WINDOWS_DOCKER_IMAGE ?= arma-tools/windows-builder:latest
WINDOWS_DOCKER_WORKDIR ?= /work
WINDOWS_DOCKER_BUILD ?= ON
WINDOWS_DOCKER_AS_HOST_USER ?= ON
WINDOWS_DOCKER_USER = $(if $(filter ON,$(WINDOWS_DOCKER_AS_HOST_USER)),--user $$(id -u):$$(id -g),)

all: debug

# --- Configure ---

configure:
	cmake --preset linux-debug -DBUILD_GUI=ON

configure-release:
	cmake --preset linux-release -DBUILD_GUI=ON

# --- Build ---

debug: configure
	cmake --build $(BUILD_DEBUG)

release: configure-release
	cmake --build $(BUILD_RELEASE)

gui: configure
	cmake --build $(BUILD_DEBUG) --target arma-tools

# --- Test ---

test: debug
	ctest --test-dir $(BUILD_DEBUG) --output-on-failure

# --- Run ---

run: gui
	./$(BUILD_DEBUG)/gui/arma-tools

# --- Package ---

package:
	cmake --preset linux-package
	cmake --build $(BUILD_PACKAGE) -j$$(nproc)
	cd $(BUILD_PACKAGE) && CPACK_THREADS=0 cpack

package-regular:
	cmake -S . -B $(BUILD_PACKAGE_REGULAR) -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DBUILD_GUI=ON -DWRP2PROJECT_WITH_TV4L=OFF
	cmake --build $(BUILD_PACKAGE_REGULAR) -j$$(nproc)
	cd $(BUILD_PACKAGE_REGULAR) && CPACK_THREADS=0 cpack

package-creator:
	cmake -S . -B $(BUILD_PACKAGE_CREATOR) -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DBUILD_GUI=ON -DWRP2PROJECT_WITH_TV4L=ON
	cmake --build $(BUILD_PACKAGE_CREATOR) -j$$(nproc)
	cd $(BUILD_PACKAGE_CREATOR) && CPACK_THREADS=0 cpack

package-windows-preflight:
	@if [ "$(WINDOWS_USE_DOCKER)" = "ON" ]; then \
		if ! command -v docker >/dev/null 2>&1; then \
			echo "Error: missing required tool 'docker' for WINDOWS_USE_DOCKER=ON."; \
			exit 1; \
		fi; \
		if [ "$(WINDOWS_DOCKER_BUILD)" = "OFF" ] && ! docker image inspect "$(WINDOWS_DOCKER_IMAGE)" >/dev/null 2>&1; then \
			echo "Error: Docker image '$(WINDOWS_DOCKER_IMAGE)' not found and WINDOWS_DOCKER_BUILD=OFF."; \
			echo "Run 'make package-windows-docker-image' or set WINDOWS_DOCKER_BUILD=ON."; \
			exit 1; \
		fi; \
		if [ "$(WINDOWS_BUILD_GUI)" = "ON" ] && [ "$(WINDOWS_DOCKER_BUILD)" = "OFF" ]; then \
			if docker image inspect "$(WINDOWS_DOCKER_IMAGE)" >/dev/null 2>&1; then \
				docker run --rm -t \
					$(WINDOWS_DOCKER_USER) \
					-v "$(CURDIR):$(WINDOWS_DOCKER_WORKDIR)" \
					-w "$(WINDOWS_DOCKER_WORKDIR)" \
					"$(WINDOWS_DOCKER_IMAGE)" \
					bash -lc "set -e; \
						if [ -d /ucrt64/bin ]; then \
							export MSYSTEM=UCRT64; \
						elif [ -d /mingw64/bin ]; then \
							export MSYSTEM=MINGW64; \
						fi; \
						export CHERE_INVOKING=1; \
						if [ -f /etc/profile ]; then source /etc/profile; fi; \
						export PATH="/usr/bin:/usr/local/bin:$$PATH"; \
						export PKG_CONFIG_PATH="/ucrt64/lib/pkgconfig:/ucrt64/share/pkgconfig:$$PKG_CONFIG_PATH"; \
						if ! command -v pkg-config >/dev/null 2>&1; then \
							echo 'Error: pkg-config not found in Docker image PATH.'; \
							exit 1; \
						fi; \
						for m in gtkmm-4.0 epoxy vorbisfile libpanel-1 libadwaita-1; do \
							if ! pkg-config --exists \$$m; then \
								echo \"Error: missing Docker GUI dependency '\$$m'.\"; \
								exit 1; \
							fi; \
						done"; \
			else \
				echo "Info: skipping Docker GUI preflight because image '$(WINDOWS_DOCKER_IMAGE)' is not built yet."; \
			fi; \
		elif [ "$(WINDOWS_BUILD_GUI)" = "ON" ] && [ "$(WINDOWS_DOCKER_BUILD)" = "ON" ]; then \
			echo "Info: skipping strict Docker GUI preflight because WINDOWS_DOCKER_BUILD=ON (image will be rebuilt)."; \
		fi; \
	else \
		for t in $(WINDOWS_TOOLCHAIN_PREFIX)-gcc $(WINDOWS_TOOLCHAIN_PREFIX)-g++ $(WINDOWS_TOOLCHAIN_PREFIX)-windres cmake cpack ninja; do \
			if ! command -v $$t >/dev/null 2>&1; then \
				echo "Error: missing required tool '$$t'."; \
				exit 1; \
			fi; \
		done; \
		if [ "$(WINDOWS_BUILD_GUI)" = "ON" ]; then \
			if ! command -v $(WINDOWS_TOOLCHAIN_PREFIX)-pkg-config >/dev/null 2>&1; then \
				echo "Error: missing '$(WINDOWS_TOOLCHAIN_PREFIX)-pkg-config' required for Windows GUI dependency checks."; \
				exit 1; \
			fi; \
			for m in gtkmm-4.0 epoxy vorbisfile libpanel-1 libadwaita-1; do \
				if ! $(WINDOWS_TOOLCHAIN_PREFIX)-pkg-config --exists $$m; then \
					echo "Error: missing Windows GUI dependency '$$m' for $(WINDOWS_TOOLCHAIN_PREFIX)-pkg-config."; \
					echo "Install MinGW pkg-config packages for $$m or set WINDOWS_BUILD_GUI=OFF."; \
					exit 1; \
				fi; \
			done; \
		fi; \
	fi

package-windows-docker-image: package-windows-preflight
	@if [ "$(WINDOWS_USE_DOCKER)" != "ON" ]; then \
		exit 0; \
	fi
	docker build \
		--build-arg BASE_IMAGE="$(WINDOWS_DOCKER_BASE_IMAGE)" \
		-f docker/windows-builder.Dockerfile \
		-t "$(WINDOWS_DOCKER_IMAGE)" \
		.

package-windows-configure: package-windows-preflight
	@if [ "$(WINDOWS_USE_DOCKER)" = "ON" ] && [ "$(WINDOWS_DOCKER_BUILD)" = "ON" ]; then \
		$(MAKE) package-windows-docker-image WINDOWS_USE_DOCKER=ON; \
	fi
	@if [ "$(WINDOWS_USE_DOCKER)" = "ON" ]; then \
		docker run --rm -t \
			$(WINDOWS_DOCKER_USER) \
			-v "$(CURDIR):$(WINDOWS_DOCKER_WORKDIR)" \
			-w "$(WINDOWS_DOCKER_WORKDIR)" \
			"$(WINDOWS_DOCKER_IMAGE)" \
			bash -lc "set -e; \
					if [ -d /ucrt64/bin ]; then \
						export MSYSTEM=UCRT64; \
					elif [ -d /mingw64/bin ]; then \
						export MSYSTEM=MINGW64; \
					fi; \
					export CHERE_INVOKING=1; \
					if [ -f /etc/profile ]; then source /etc/profile; fi; \
					export PATH="/usr/bin:/usr/local/bin:$$PATH"; \
					export PKG_CONFIG_PATH="/ucrt64/lib/pkgconfig:/ucrt64/share/pkgconfig:$$PKG_CONFIG_PATH"; \
					if [ "$(WINDOWS_BUILD_GUI)" = "ON" ]; then \
						CC_CAND="x86_64-w64-mingw32ucrt-gcc"; \
						CXX_CAND="x86_64-w64-mingw32ucrt-g++"; \
						TOOLCHAIN_FILE="cmake/toolchains/mingw64-ucrt.cmake"; \
					else \
						CC_CAND="x86_64-w64-mingw32-gcc"; \
						CXX_CAND="x86_64-w64-mingw32-g++"; \
						TOOLCHAIN_FILE="cmake/toolchains/mingw64.cmake"; \
					fi; \
					if ! command -v $$CXX_CAND >/dev/null 2>&1 && \
					   ! command -v $$CC_CAND >/dev/null 2>&1; then \
						echo 'Error: no x86_64 MinGW compiler found in Docker image PATH.'; \
						exit 1; \
					fi; \
					if ! command -v cmake >/dev/null 2>&1; then \
						echo 'Error: cmake not found in Docker image PATH.'; \
						exit 1; \
					fi; \
					if [ "$(WINDOWS_BUILD_GUI)" = "ON" ]; then \
						if ! command -v pkg-config >/dev/null 2>&1; then \
							echo 'Error: pkg-config not found in Docker image PATH.'; \
							exit 1; \
						fi; \
						for m in gtkmm-4.0 epoxy vorbisfile libpanel-1 libadwaita-1; do \
							if ! pkg-config --exists \$$m; then \
								echo \"Error: missing Docker GUI dependency '\$$m'.\"; \
								echo 'Hint: this Debian-based builder currently lacks MinGW gtkmm/epoxy/vorbis cross packages; use WINDOWS_BUILD_GUI=OFF or provide a custom image with these modules.'; \
								exit 1; \
							fi; \
						done; \
					fi; \
					cmake --fresh -S . -B $(BUILD_PACKAGE_WINDOWS_DOCKER) -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DBUILD_GUI=$(WINDOWS_BUILD_GUI) -DCMAKE_TOOLCHAIN_FILE=\$$TOOLCHAIN_FILE"; \
			exit $$?; \
	fi
	@if [ "$(WINDOWS_USE_DOCKER)" != "ON" ]; then \
		if ! echo "$(WINDOWS_CMAKE_ARGS)" | grep -Eq 'CMAKE_TOOLCHAIN_FILE=|CMAKE_SYSTEM_NAME=Windows'; then \
			echo "Error: Windows toolchain not configured."; \
			echo "Set WINDOWS_CMAKE_ARGS with -DCMAKE_TOOLCHAIN_FILE=... or -DCMAKE_SYSTEM_NAME=Windows."; \
			exit 1; \
		fi; \
		cmake --fresh -S . -B $(BUILD_PACKAGE_WINDOWS) -G "$(WINDOWS_CMAKE_GENERATOR)" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DBUILD_GUI=$(WINDOWS_BUILD_GUI) $(WINDOWS_CMAKE_ARGS); \
	fi

package-windows-installer: package-windows-configure
	@if [ "$(WINDOWS_USE_DOCKER)" = "ON" ]; then \
		docker run --rm -t \
			$(WINDOWS_DOCKER_USER) \
			-v "$(CURDIR):$(WINDOWS_DOCKER_WORKDIR)" \
			-w "$(WINDOWS_DOCKER_WORKDIR)" \
			"$(WINDOWS_DOCKER_IMAGE)" \
			bash -lc "set -e; \
					if [ -d /ucrt64/bin ]; then \
						export MSYSTEM=UCRT64; \
					elif [ -d /mingw64/bin ]; then \
						export MSYSTEM=MINGW64; \
					fi; \
					export CHERE_INVOKING=1; \
					if [ -f /etc/profile ]; then source /etc/profile; fi; \
						export PATH="/usr/bin:/usr/local/bin:$$PATH"; \
						export PKG_CONFIG_PATH="/ucrt64/lib/pkgconfig:/ucrt64/share/pkgconfig:$$PKG_CONFIG_PATH"; \
						if [ "$(WINDOWS_BUILD_GUI)" = "ON" ]; then \
							CC_CAND="x86_64-w64-mingw32ucrt-gcc"; \
							CXX_CAND="x86_64-w64-mingw32ucrt-g++"; \
						else \
							CC_CAND="x86_64-w64-mingw32-gcc"; \
							CXX_CAND="x86_64-w64-mingw32-g++"; \
						fi; \
						if ! command -v $$CXX_CAND >/dev/null 2>&1 && \
						   ! command -v $$CC_CAND >/dev/null 2>&1; then \
							echo 'Error: no x86_64 MinGW compiler found in Docker image PATH.'; \
							exit 1; \
						fi; \
						if ! command -v makensis >/dev/null 2>&1; then \
							if ! command -v makensis.exe >/dev/null 2>&1; then \
								echo 'Error: makensis not found in Docker image PATH.'; \
								exit 1; \
							fi; \
						fi; \
						if ! command -v cmake >/dev/null 2>&1 || ! command -v cpack >/dev/null 2>&1; then \
							echo 'Error: cmake/cpack not found in Docker image PATH.'; \
							exit 1; \
						fi; \
						cmake --build $(BUILD_PACKAGE_WINDOWS_DOCKER) -j\$$(nproc); \
						cd $(BUILD_PACKAGE_WINDOWS_DOCKER) && CPACK_THREADS=0 cpack -G NSIS"; \
		exit $$?; \
	fi
	@if [ "$(WINDOWS_USE_DOCKER)" != "ON" ]; then \
		if ! command -v makensis >/dev/null 2>&1; then \
			echo "Error: missing required tool 'makensis' for NSIS installer packaging."; \
			exit 1; \
		fi; \
		cmake --build $(BUILD_PACKAGE_WINDOWS) -j$$(nproc); \
		cd $(BUILD_PACKAGE_WINDOWS) && CPACK_THREADS=0 cpack -G NSIS; \
	fi

package-windows-zip: package-windows-configure
	@if [ "$(WINDOWS_USE_DOCKER)" = "ON" ]; then \
		docker run --rm -t \
			$(WINDOWS_DOCKER_USER) \
			-v "$(CURDIR):$(WINDOWS_DOCKER_WORKDIR)" \
			-w "$(WINDOWS_DOCKER_WORKDIR)" \
			"$(WINDOWS_DOCKER_IMAGE)" \
			bash -lc "set -e; \
					if [ -d /ucrt64/bin ]; then \
						export MSYSTEM=UCRT64; \
					elif [ -d /mingw64/bin ]; then \
						export MSYSTEM=MINGW64; \
					fi; \
					export CHERE_INVOKING=1; \
					if [ -f /etc/profile ]; then source /etc/profile; fi; \
					export PATH="/usr/bin:/usr/local/bin:$$PATH"; \
						export PKG_CONFIG_PATH="/ucrt64/lib/pkgconfig:/ucrt64/share/pkgconfig:$$PKG_CONFIG_PATH"; \
						if [ "$(WINDOWS_BUILD_GUI)" = "ON" ]; then \
							CC_CAND="x86_64-w64-mingw32ucrt-gcc"; \
							CXX_CAND="x86_64-w64-mingw32ucrt-g++"; \
						else \
							CC_CAND="x86_64-w64-mingw32-gcc"; \
							CXX_CAND="x86_64-w64-mingw32-g++"; \
						fi; \
						if ! command -v $$CXX_CAND >/dev/null 2>&1 && \
						   ! command -v $$CC_CAND >/dev/null 2>&1; then \
							echo 'Error: no x86_64 MinGW compiler found in Docker image PATH.'; \
							exit 1; \
						fi; \
						if ! command -v cmake >/dev/null 2>&1 || ! command -v cpack >/dev/null 2>&1; then \
							echo 'Error: cmake/cpack not found in Docker image PATH.'; \
							exit 1; \
						fi; \
						cmake --build $(BUILD_PACKAGE_WINDOWS_DOCKER) -j\$$(nproc); \
						cd $(BUILD_PACKAGE_WINDOWS_DOCKER) && CPACK_THREADS=0 cpack -G ZIP"; \
			exit $$?; \
	fi
	@if [ "$(WINDOWS_USE_DOCKER)" != "ON" ]; then \
		cmake --build $(BUILD_PACKAGE_WINDOWS) -j$$(nproc); \
		cd $(BUILD_PACKAGE_WINDOWS) && CPACK_THREADS=0 cpack -G ZIP; \
	fi
# --- Clean ---

clean:
	@set -eu; \
	for d in "$(BUILD_DEBUG)" "$(BUILD_RELEASE)" "$(BUILD_PACKAGE)" "$(BUILD_PACKAGE_REGULAR)" "$(BUILD_PACKAGE_CREATOR)" "$(BUILD_PACKAGE_WINDOWS)" "$(BUILD_PACKAGE_WINDOWS_DOCKER)"; do \
		if [ -z "$$d" ] || [ "$$d" = "/" ] || [ "$$d" = "." ]; then \
			echo "Error: refusing to remove unsafe path '$$d'"; \
			exit 1; \
		fi; \
	done; \
	rm -rf -- "$(BUILD_DEBUG)" "$(BUILD_RELEASE)" "$(BUILD_PACKAGE)" "$(BUILD_PACKAGE_REGULAR)" "$(BUILD_PACKAGE_CREATOR)" "$(BUILD_PACKAGE_WINDOWS)" "$(BUILD_PACKAGE_WINDOWS_DOCKER)"
