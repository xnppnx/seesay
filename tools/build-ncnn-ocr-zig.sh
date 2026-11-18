#!/usr/bin/env bash
# Fallback cross-build using zig (installed via pip, no apt needed).
# Used when GitHub runner's apt sources are unreachable.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"

NCNN_DIR="${NCNN_DIR:-$ROOT/ncnn-20261020}"
NCNN_BUILD="${NCNN_BUILD:-$ROOT/ncnn-build}"
OCV="$ROOT/third_party/opencv-mobile"
OUT="$ROOT/payload/bin"
mkdir -p "$OUT"

echo "== Check prerequisites =="
command -v zig || { echo "zig not installed"; exit 1; }
test -f "$NCNN_BUILD/src/libncnn.a" || { echo "libncnn.a missing"; exit 1; }
test -d "$OCV/include" || { echo "opencv-mobile missing; run build-ncnn-ocr.sh fetch first"; exit 1; }

echo "== Cross-compile seesay_ocr with zig =="
# zig's c++ driver cross-compiles to aarch64-linux-gnu and bundles its own libc.
zig c++ -target aarch64-linux-gnu -O2 -std=c++17 \
  -I"$NCNN_DIR/src" -I"$NCNN_BUILD/src" -I"$OCV/include/opencv5" \
  -DNCNN_VULKAN=0 \
  "$ROOT/tools/ocr/seesay_ocr.cpp" \
  -o "$OUT/seesay_ocr" \
  "$NCNN_BUILD/src/libncnn.a" \
  "$OCV/lib/libopencv_core.a" \
  "$OCV/lib/libopencv_imgproc.a" \
  -lpthread -ldl

ls -lh "$OUT/seesay_ocr"
echo "== seesay_ocr built with zig =="
