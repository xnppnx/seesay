#!/usr/bin/env bash
# Cross-build the standalone C++ OCR subprocess (seesay_ocr) for aarch64.
# Runs inside the GitHub Actions ubuntu-24.04 runner (x86 host, arm64 target).
# Depends on artifacts produced by build-ncnn.yml job:
#   ncnn-20261020 source + build/src/libncnn.a  (already cross-compiled)
# Downloads opencv-mobile prebuilt for debian-trixie-aarch64.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"

# ncnn source tree is expected at repo root (fetched by the CI job).
NCNN_DIR="${NCNN_DIR:-$ROOT/ncnn-20261020}"
NCNN_BUILD="${NCNN_BUILD:-$ROOT/ncnn-build}"
# opencv-mobile matching the device OS (Debian 13 trixie, aarch64).
OCV_VERSION="5.1.0"
# Use the ubuntu-2404 build: its glibc (2.39) matches the 24.04 cross toolchain.
# The debian-trixie build needs newer glibc symbols (__isoc23_strtol) that the
# cross compiler cannot link.
OCV_ZIP="opencv-mobile-${OCV_VERSION}-ubuntu-2404.zip"
OCV_URL="https://github.com/nihui/opencv-mobile/releases/download/v38/${OCV_ZIP}"

OUT="$ROOT/payload/bin"
mkdir -p "$OUT"

echo "== [1/3] Check ncnn cross-built library =="
test -f "$NCNN_BUILD/src/libncnn.a" || { echo "libncnn.a missing; build ncnn first"; exit 1; }

echo "== [2/3] Fetch opencv-mobile (aarch64) =="
OCV="$ROOT/third_party/opencv-mobile"
# Always (re)fetch to a KNOWN dir; archive extracts to this exact folder name.
EXPECTED_DIR="opencv-mobile-5.1.0-ubuntu-2404"
rm -rf "$ROOT/third_party/$EXPECTED_DIR" "$ROOT/third_party/opencv-mobile"
mkdir -p "$ROOT/third_party"
curl -fL --retry 3 -o "$ROOT/third_party/$OCV_ZIP" "$OCV_URL"   # -f: fail on HTTP error
unzip -q "$ROOT/third_party/$OCV_ZIP" -d "$ROOT/third_party"
test -d "$ROOT/third_party/$EXPECTED_DIR" || { echo "opencv extract failed"; ls -la "$ROOT/third_party"; exit 1; }
mv "$ROOT/third_party/$EXPECTED_DIR" "$OCV"
echo "  opencv headers: $(ls "$OCV/include/opencv5/opencv2/core/core.hpp")"

echo "== [3/3] Cross-compile seesay_ocr =="
# Pick compiler: prefer system cross-g++, fall back to zig (no apt needed).
if command -v aarch64-linux-gnu-g++ >/dev/null 2>&1; then
  CXX=aarch64-linux-gnu-g++
  CXXFLAGS=""
  echo "  using aarch64-linux-gnu-g++"
elif command -v zig >/dev/null 2>&1; then
  CXX="zig c++"
  CXXFLAGS="--target=aarch64-linux-gnu"
  echo "  using zig cross compiler"
else
  echo "no aarch64 cross compiler (need apt crossbuild-essential-arm64 or pip ziglang)"; exit 1
fi

# opencv-mobile 5.1.0 headers live under include/opencv5/ (opencv4 was include/)
$CXX $CXXFLAGS -O2 -std=c++17 -fopenmp \
  -I"$NCNN_DIR/src" \
  -I"$NCNN_BUILD/src" \
  -I"$OCV/include/opencv5" \
  -DNCNN_VULKAN=0 \
  "$ROOT/tools/ocr/seesay_ocr.cpp" \
  -o "$OUT/seesay_ocr" \
  "$NCNN_BUILD/src/libncnn.a" \
  -Wl,--start-group \
  "$OCV/lib/libopencv_core.a" \
  "$OCV/lib/libopencv_imgproc.a" \
  "$OCV/lib/libopencv_geometry.a" \
  -Wl,--end-group \
  -lpthread -ldl
# NOTE: stb_image is self-contained (JPEG/PNG decoders compiled in), so no
# -ljpeg/-lpng needed. No -lz either: the cross sysroot lacks arm64 zlib dev,
# and opencv-mobile's static libs already bundle what they need.

aarch64-linux-gnu-strip "$OUT/seesay_ocr"
ls -lh "$OUT/seesay_ocr"
echo "== seesay_ocr built at $OUT/seesay_ocr =="
