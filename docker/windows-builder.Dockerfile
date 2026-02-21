ARG BASE_IMAGE=debian:trixie-slim
FROM ${BASE_IMAGE}

ARG DEBIAN_FRONTEND=noninteractive
SHELL ["/bin/bash", "-lc"]

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    ninja-build \
    ccache \
    git \
    pkg-config \
    glib2.0-bin \
    libglib2.0-dev-bin \
    python3 \
    ca-certificates \
    curl \
    zip \
    unzip \
    zstd \
    nsis \
    mingw-w64 \
    gcc-mingw-w64-ucrt64 \
    g++-mingw-w64-ucrt64 \
    && rm -rf /var/lib/apt/lists/*

RUN set -euo pipefail; \
    mkdir -p /opt/msys2-pkgs /opt/msys2; \
    curl -fsSL https://repo.msys2.org/mingw/ucrt64/ucrt64.db -o /opt/msys2-pkgs/ucrt64.db.zst; \
    zstd -d -c /opt/msys2-pkgs/ucrt64.db.zst | tar -xf - -C /opt/msys2-pkgs

RUN set -euo pipefail; \
    python3 - <<'PY' > /opt/msys2-pkgs/needed.txt
import os
import re
from pathlib import Path

db_root = Path("/opt/msys2-pkgs")
targets = {
    "mingw-w64-ucrt-x86_64-gtkmm-4.0",
    "mingw-w64-ucrt-x86_64-libepoxy",
    "mingw-w64-ucrt-x86_64-libvorbis",
    "mingw-w64-ucrt-x86_64-libadwaita",
    "mingw-w64-ucrt-x86_64-libpanel",
    "mingw-w64-ucrt-x86_64-pkgconf",
    "mingw-w64-ucrt-x86_64-zlib",
}

pkgs = {}
for desc in db_root.glob("mingw-w64-ucrt-x86_64-*/desc"):
    text = desc.read_text(encoding="utf-8", errors="ignore")
    section = None
    fields = {}
    for line in text.splitlines():
        if line.startswith("%") and line.endswith("%"):
            section = line.strip("%")
            fields.setdefault(section, [])
            continue
        if section:
            fields[section].append(line)
    name = (fields.get("NAME") or [""])[0].strip()
    if not name:
        continue
    filename = (fields.get("FILENAME") or [""])[0].strip()
    deps = [d.strip() for d in fields.get("DEPENDS", []) if d.strip()]
    pkgs[name] = {"filename": filename, "deps": deps}

missing = [t for t in sorted(targets) if t not in pkgs]
if missing:
    raise SystemExit(f"Missing target packages in MSYS2 ucrt repo: {missing}")

dep_name_re = re.compile(r"^[^<>=: ]+")
needed = set()
queue = list(targets)
while queue:
    name = queue.pop()
    if name in needed:
        continue
    if name not in pkgs:
        # Skip deps provided by toolchain/system layers.
        continue
    needed.add(name)
    for dep in pkgs[name]["deps"]:
        m = dep_name_re.match(dep)
        if not m:
            continue
        dname = m.group(0)
        if dname.startswith("mingw-w64-ucrt-x86_64-"):
            queue.append(dname)

for name in sorted(needed):
    fn = pkgs[name]["filename"]
    if fn:
        print(fn)
PY

RUN set -euo pipefail; \
    while read -r pkg; do \
        [ -n "$pkg" ] || continue; \
        curl -fsSL "https://repo.msys2.org/mingw/ucrt64/${pkg}" -o "/opt/msys2-pkgs/${pkg}"; \
        tar --zstd -xf "/opt/msys2-pkgs/${pkg}" -C /opt/msys2; \
    done < /opt/msys2-pkgs/needed.txt; \
    ln -sfn /opt/msys2/ucrt64 /ucrt64

RUN set -euo pipefail; \
    command -v cmake >/dev/null 2>&1; \
    command -v cpack >/dev/null 2>&1; \
    command -v x86_64-w64-mingw32-gcc >/dev/null 2>&1; \
    command -v x86_64-w64-mingw32-g++ >/dev/null 2>&1; \
    command -v x86_64-w64-mingw32ucrt-gcc >/dev/null 2>&1; \
    command -v x86_64-w64-mingw32ucrt-g++ >/dev/null 2>&1; \
    test -d /ucrt64/lib/pkgconfig; \
    test -d /ucrt64/share/pkgconfig
