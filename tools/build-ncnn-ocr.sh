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
# Use the 4.14 armlinux build: it contains a genuine aarch64-linux-gnu target
# with old-glibc symbols. 5.1.0 only ships x86 (ubuntu-2404) or too-new-glibc
# (debian-trixie) builds, neither links with the aarch64 cross compiler.
OCV_ZIP="opencv-mobile-4.14.0-armlinux.zip"
OCV_URL="https://github.com/nihui/opencv-mobile/releases/download/v38/${OCV_ZIP}"

OUT="$ROOT/payload/bin"
mkdir -p "$OUT"
# opencv4 umbrella header needs the 4.x API (official ppocrv5 example targets 4.x)

echo "== [1/3] Check ncnn cross-built library =="
test -f "$NCNN_BUILD/src/libncnn.a" || { echo "libncnn.a missing; build ncnn first"; exit 1; }

echo "== [2/3] Fetch opencv-mobile (aarch64) =="
OCV="$ROOT/third_party/opencv-mobile"
ARCHIVE_DIR="opencv-mobile-4.14.0-armlinux"
rm -rf "$ROOT/third_party/$ARCHIVE_DIR" "$OCV"
mkdir -p "$ROOT/third_party"
curl -fL --retry 3 -o "$ROOT/third_party/$OCV_ZIP" "$OCV_URL"   # -f: fail on HTTP error
unzip -q "$ROOT/third_party/$OCV_ZIP" -d "$ROOT/third_party"
# The armlinux archive holds several ABI dirs; pick the aarch64 one.
test -d "$ROOT/third_party/$ARCHIVE_DIR/aarch64-linux-gnu" || { echo "opencv aarch64 dir missing"; ls -la "$ROOT/third_party/$ARCHIVE_DIR"; exit 1; }
mv "$ROOT/third_party/$ARCHIVE_DIR/aarch64-linux-gnu" "$OCV"
echo "  opencv headers: $(ls "$OCV/include/opencv4/opencv2/core/core.hpp")"

echo "== [3/3] Cross-compile seesay_ocr =="
# kleidicv (imgproc SIMD backend) lives in lib/opencv4/3rdparty, NOT lib/ root.
# Fail early with a clear listing if the archive layout ever changes again.
KLEIDICV_DIR="$OCV/lib/opencv4/3rdparty"
test -f "$KLEIDICV_DIR/libkleidicv.a" || {
  echo "kleidicv not found at $KLEIDICV_DIR"; echo "lib tree:"; ls -R "$OCV/lib" | head -60; exit 1;
}
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
  -I"$OCV/include/opencv4" \
  -DNCNN_VULKAN=0 \
  "$ROOT/tools/ocr/seesay_ocr.cpp" \
  -o "$OUT/seesay_ocr" \
  "$NCNN_BUILD/src/libncnn.a" \
  -Wl,--start-group \
  "$OCV/lib/libopencv_core.a" \
  "$OCV/lib/libopencv_imgproc.a" \
  "$KLEIDICV_DIR/libkleidicv.a" \
  "$KLEIDICV_DIR/libkleidicv_hal.a" \
  "$KLEIDICV_DIR/libkleidicv_thread.a" \
  -Wl,--end-group \
  -lpthread -ldl
# NOTE: stb_image is self-contained (JPEG/PNG decoders compiled in), so no
# -ljpeg/-lpng needed. No -lz either: the cross sysroot lacks arm64 zlib dev,
# and opencv-mobile's static libs already bundle what they need.

aarch64-linux-gnu-strip "$OUT/seesay_ocr"
ls -lh "$OUT/seesay_ocr"
echo "== seesay_ocr built at $OUT/seesay_ocr =="

# ---- Stage-2: cross-compile seesay_detect (ncnn NanoDet object detection) ----
# Same toolchain / opencv / ncnn backend as OCR; only the source and output differ.
echo "== [4/4] Cross-compile seesay_detect (stage-2 object detection) =="
$CXX $CXXFLAGS -O2 -std=c++17 -fopenmp \
  -I"$NCNN_DIR/src" \
  -I"$NCNN_BUILD/src" \
  -I"$OCV/include/opencv4" \
  -DNCNN_VULKAN=0 \
  "$ROOT/tools/detect/seesay_detect.cpp" \
  -o "$OUT/seesay_detect" \
  "$NCNN_BUILD/src/libncnn.a" \
  -Wl,--start-group \
  "$OCV/lib/libopencv_core.a" \
  "$OCV/lib/libopencv_imgproc.a" \
  "$KLEIDICV_DIR/libkleidicv.a" \
  "$KLEIDICV_DIR/libkleidicv_hal.a" \
  "$KLEIDICV_DIR/libkleidicv_thread.a" \
  -Wl,--end-group \
  -lpthread -ldl

aarch64-linux-gnu-strip "$OUT/seesay_detect"
ls -lh "$OUT/seesay_detect"
echo "== seesay_detect built at $OUT/seesay_detect =="
