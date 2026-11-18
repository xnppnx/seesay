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
OCV_ZIP="opencv-mobile-${OCV_VERSION}-debian-trixie-aarch64.zip"
OCV_URL="https://github.com/nihui/opencv-mobile/releases/download/v38/${OCV_ZIP}"

OUT="$ROOT/payload/bin"
mkdir -p "$OUT"

echo "== [1/3] Check ncnn cross-built library =="
test -f "$NCNN_BUILD/src/libncnn.a" || { echo "libncnn.a missing; build ncnn first"; exit 1; }

echo "== [2/3] Fetch opencv-mobile (aarch64) =="
if [ ! -d "$ROOT/third_party/opencv-mobile" ]; then
  mkdir -p "$ROOT/third_party"
  curl -sL --retry 3 -o "$ROOT/third_party/$OCV_ZIP" "$OCV_URL"
  unzip -q "$ROOT/third_party/$OCV_ZIP" -d "$ROOT/third_party/opencv-mobile-tmp"
  # The archive extracts to a version-named dir; rename it to a stable name.
  OCV_DIR=$(find "$ROOT/third_party/opencv-mobile-tmp" -maxdepth 1 -type d -name 'opencv-mobile-*' | head -1)
  mv "$OCV_DIR" "$ROOT/third_party/opencv-mobile"
  rm -rf "$ROOT/third_party/opencv-mobile-tmp"
fi
OCV="$ROOT/third_party/opencv-mobile"

echo "== [3/3] Cross-compile seesay_ocr =="
aarch64-linux-gnu-g++ -O2 -std=c++17 \
  -I"$NCNN_DIR/src" \
  -I"$NCNN_BUILD/src" \
  -I"$OCV/include" \
  -DNCNN_VULKAN=0 \
  "$ROOT/tools/ocr/seesay_ocr.cpp" \
  -o "$OUT/seesay_ocr" \
  "$NCNN_BUILD/src/libncnn.a" \
  "$OCV/lib/libopencv_core.a" \
  "$OCV/lib/libopencv_imgproc.a" \
  "$OCV/lib/libopencv_imgcodecs.a" \
  -lpthread -ldl -lz

aarch64-linux-gnu-strip "$OUT/seesay_ocr"
ls -lh "$OUT/seesay_ocr"
echo "== seesay_ocr built at $OUT/seesay_ocr =="
