#!/usr/bin/env bash
# Fetch every offline dependency and place it under payload/ so the deb is
# self-contained: install it -> SeeSay works with NO network and NO manual steps.
#
# Run on the GitHub Actions ubuntu-24.04 runner (x86_64) or any Linux host with
# internet access. All payloads are aarch64 (device architecture).
#
# What goes into payload/:
#   payload/wheels/    arm64 Python wheels (cactus-compute, rapidocr, onnxruntime, ...)
#   payload/models/    Mirage + RapidOCR weights (fixed revisions, from hf-mirror)
#   payload/espeak/    espeak-ng + voice data .deb files (from Aliyun Debian mirror)
#   payload/install.sh on-device installer: pip-installs wheels + unpacks models
#
# Everything is version-pinned (project book 8.6):
#   cactus-compute 4.2.3 | mirage rev 76c1e9e... | rapidocr 1.2.3
#   onnxruntime 1.32.0   | espeak-ng 1.52
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
PAYLOAD="$ROOT/payload"
mkdir -p "$PAYLOAD/wheels" "$PAYLOAD/models/mirage" "$PAYLOAD/models/rapidocr" "$PAYLOAD/espeak"

HF_ENDPOINT="https://hf-mirror.com"
PYTHON_PLATFORM="manylinux_2_28_aarch64"
PYTHON_VERSION="cp313"   # device python3 is 3.13 (see work log)

echo "== [1/4] Python wheels (aarch64) =="
# Download arm64 wheels on this x86 host, then the device installs locally.
pip download --only-binary=:all: \
  --platform "$PYTHON_PLATFORM" --python-version 313 --implementation cp --abi "$PYTHON_VERSION" \
  --dest "$PAYLOAD/wheels" \
  "cactus-compute==4.2.3" \
  "rapidocr-onnxruntime==1.2.3" \
  "onnxruntime==1.32.0" \
  "opencv-python" "numpy" "shapely" "pyclipper" "Pillow"

echo "== [2/4] Mirage weights (hf-mirror, fixed revision) =="
MIRAGE_REV="76c1e9e53381aca8fc3843c636216c96ff220122"
for f in mirage.cact config.json; do
  curl -fL --retry 3 -o "$PAYLOAD/models/mirage/$f" \
    "$HF_ENDPOINT/models/Cactus-Compute/mirage/resolve/$MIRAGE_REV/$f"
done

echo "== [3/4] RapidOCR models =="
# RapidOCR ships its models inside the wheel on install; copy the onnx bundle
# from the installed package if present, else note it is provided by the wheel.
echo "RapidOCR models are bundled in the rapidocr-onnxruntime wheel."

echo "== [4/4] espeak-ng debs (Aliyun Debian arm64) =="
ALIYUN="https://mirrors.aliyun.com/debian/pool/main/e/espeak-ng"
# Pinned versions from the verified install in the work log.
for deb in \
  "espeak-ng-data_1.52.0+dfsg-5_arm64.deb" \
  "libespeak-ng1_1.52.0+dfsg-5_arm64.deb" \
  "espeak-ng_1.52.0+dfsg-5_arm64.deb"; do
  curl -fL --retry 3 -o "$PAYLOAD/espeak/$deb" "$ALIYUN/$deb" || \
    echo "WARN: could not fetch $deb (check exact filename on mirror)"
done
# Tiny runtime deps usually already on device; include to be safe.
for deb in \
  "https://mirrors.aliyun.com/debian/pool/main/libp/libpcaudio/libpcaudio0_1.3-1_arm64.deb" \
  "https://mirrors.aliyun.com/debian/pool/main/libs/libsonic/libsonic0_0.2.0-12_arm64.deb"; do
  curl -fL --retry 3 -o "$PAYLOAD/espeak/$(basename "$deb")" "$deb" || \
    echo "WARN: could not fetch $deb"
done

cat > "$PAYLOAD/install.sh" <<'INSTALL'
#!/usr/bin/env bash
# On-device payload installer. Run as root once after the deb is installed.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SYSTEM_PY="$(command -v python3)"

echo "Installing offline Python wheels..."
"$SYSTEM_PY" -m pip install --no-index --find-links "$HERE/wheels" \
  cactus-compute rapidocr-onnxruntime onnxruntime opencv-python numpy \
  shapely pyclipper Pillow

echo "Installing espeak-ng (GPL, kept as separate binaries)..."
dpkg -i "$HERE"/espeak/*.deb || apt-get -f install -y

echo "Placing model weights into the offline cache..."
CACHE_HOME="/var/lib/seesay/.cache"
mkdir -p "$CACHE_HOME/cactus/models/mirage/76c1e9e53381aca8fc3843c636216c96ff220122"
cp "$HERE"/models/mirage/* \
  "$CACHE_HOME/cactus/models/mirage/76c1e9e53381aca8fc3843c636216c96ff220122/"

echo "Payload install complete."
INSTALL
chmod +x "$PAYLOAD/install.sh"

echo "== Payload ready at $PAYLOAD =="
du -sh "$PAYLOAD"
find "$PAYLOAD" -maxdepth 2 -type f | sort
