# SeeSay

**Point, shoot, hear what's around you.**

SeeSay is a completely offline AI visual assistant for blind and low-vision users. Press one button, take a photo, and hear what is in front of you plus any text you are facing. No account, no internet, no recording.

> Status: Phase 1 (development-device voice loop). The device is a development prototype; the long-term target is a wearable assistant such as smart glasses.

## How it works

1. Press **SPACE** to take a photo.
2. A small on-device vision model describes the scene; an OCR model reads any text.
3. Text is spoken aloud through the speaker.

Everything runs locally. The app never connects to the network.

## Keys

| Key | Action |
| --- | --- |
| **SPACE** | Take a photo, describe it, and speak the result |
| **OK / Enter** | Replay the last announcement |
| **ESC** (short) | No action (reserved) |
| **ESC** (long) | Return to HOME (handled by the system) |
| **fn + H** | Show / hide key help |
| **fn + S / D / A** | Volume down / up / mute |
| **C** | Clear the current result |

## Project layout

- `app-builder.json` — store / upload metadata (name, version, permissions, assets)
- `CMakeLists.txt`, `cmake/` — build and Debian packaging contract
- `config/seesay.conf` — default app settings (installed to `/etc/seesay.conf`)
- `src/` — application, view, viewmodel, model, and platform layers
- `assets/` — icon, fonts, audio
- `screenshot/` — store screenshots (320x170)
- `tools/generate_assets.py` — regenerate the icon and placeholder screenshots

## Build

Device packages are built in the cloud and downloaded from the GitHub Actions artifact. There is no local cross-compilation toolchain.

```sh
# On GitHub Actions (push to main/master, or manual dispatch):
cmake --workflow --preset cp0-cross-package
# Deb output: dist/SeeSay_<version>_rev1_arm64.deb
```

Desktop emulator (requires vcpkg dependencies):

```sh
cmake --preset linux-x86-64
cmake --build --preset linux-x86-64
```

## Identity

| Field | Value |
| --- | --- |
| App name | SeeSay |
| Package name | seesay |
| Binary name | seesay |
| Share code | SEES |
| Version | 0.1.0 (development; first store release will be 1.0.0) |
| Author | xnppnx |
| License | Apache-2.0 |

## Privacy

- Camera: yes (core feature)
- Microphone: no
- Network: no
- Recording: no

## License

SeeSay application code is Apache-2.0. Bundled open-source components keep their own licenses. The speech engine (espeak-ng, GPL-3.0) is invoked as a separate command-line process and is never linked into the application.
