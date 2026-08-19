# Omavid

A dead-simple video **trimmer and merger** for Linux. Open one or more videos, drag two handles to pick a start and end for each, preview the clip, and export — either a single clip or everything stitched together into one MP4. On Omarchy, the interface follows your theme's accent color.

Built with a **Qt Quick (QML)** interface on the Material style — the same Qt stack Quickshell builds on — and **ffmpeg** for the actual cutting. The C++ side compiles to a single executable; the QML is embedded in it via Qt resources.

<img width="3227" height="3227" alt="screenshot-2026-06-23_15-20-40" src="https://github.com/user-attachments/assets/c76047c8-618f-4c1c-91f9-e7024c4f953b" />

## Features

- **Length trimming** — scrub a filmstrip of thumbnails, drag the start/end handles, and preview the clip before you commit to it.
- **Merging** — add several videos to a playlist, trim each one independently, reorder or remove clips, then merge them in playlist order.
- **Frame-accurate cuts** — trims are re-encoded with `libx264`/`aac` so every cut lands exactly where you set it (no keyframe snapping).
- **Smart export** — always MP4, always preserving aspect ratio, and never upscaling. Pick Original/1080p/720p only when the source actually benefits.
- **Fast sharing** — exports are written with `+faststart` so the moov atom is up front and shared clips start playing before they finish downloading.
- **Keyboard-driven** — the whole edit can be done from the keyboard (see below).
- **Omarchy-aware** — follows your theme's accent color live, and falls back gracefully on any other distro.

## Usage

### Trimming

1. Open a video (`Ctrl+O` or click the preview).
2. Drag the two handles on the filmstrip to set the start and end.
3. Scrub with the playhead; press `Space` to play just the selection.
4. Export with `Ctrl+S` (or the download button).

### Merging

1. Open several videos at once (`Ctrl+O`; the file picker allows multiple selection).
2. In the playlist on the left, click a clip to edit it, use the ↑/↓ arrows to reorder, and × to remove one.
3. Each clip keeps its own trim (or its full length if you don't touch it).
4. Click **Merge** (or `Ctrl+M`) to re-encode every clip and concatenate them in order.

## Hotkeys

| Keys | Action |
|------|--------|
| `Space` | Play / pause |
| `←` / `→` | Move the playhead 1s |
| `Shift+←` / `Shift+→` | Move the playhead 5s |
| `Alt+←` / `Alt+→` | Move the playhead 0.2s |
| `Ctrl+Space` | Move the trim start to the playhead |
| `Alt+Space` | Move the trim end to the playhead |
| `Z` | Zoom into the selection for fine tuning (again to zoom out) |
| `Ctrl+O` | Open videos |
| `Ctrl+S` | Export the current clip |
| `Ctrl+M` | Merge and export the playlist |
| `Q` | Quit (asks first if a trim hasn't been exported) |
| `?` | Show these shortcuts in the app |

## Install

### Arch / Omarchy

Install via the Omarchy Package Repository (`omavid`), or build and install the local package straight from the source tree:

```bash
./bin/install          # builds, then runs `makepkg -fsi`
```

This installs the binary, desktop entry, app icon, and MIT license.

### AppImage

A self-contained x86_64 AppImage (with `ffmpeg`/`ffprobe` bundled) is built on every push and attached to each tagged release. Download the latest from the [Releases](https://github.com/GustavoDGoat/omavid/releases) page, make it executable, and run it — no install required:

```bash
chmod +x omavid-x86_64.AppImage
./omavid-x86_64.AppImage
```

For launcher integration you can drop it in `~/.local/bin`, or use [AppImageLauncher](https://github.com/TheAssassin/AppImageLauncher).

> `xdg-desktop-portal` (and a portal backend) must be present on the host for the file picker, even with the AppImage.

## Requirements

- A C++17 compiler
- Qt6: `qt6-base`, `qt6-declarative` (Qt Quick + Controls), `qt6-multimedia`
- `qmake6` (no CMake needed)
- `ffmpeg` and `ffprobe` on your `PATH` at runtime (bundled into the AppImage)
- `xdg-desktop-portal` and a portal backend for the file picker

## Build

```bash
./bin/build
```

Produces a single `omavid` binary in `build/`.

Build the AppImage (requires `squashfs-tools`; fetches the linuxdeploy tooling on the first run):

```bash
./bin/appimage
```

Produces `dist/omavid-x86_64.AppImage`.

## Test

```bash
./bin/test
```

Runs the QtTest suite (backend logic, ffmpeg argument construction, the QML shortcut harness, and an end-to-end merge).

## How it works

The C++ backend is the bridge between QML and the ffmpeg/ffprobe command-line tools:

- **`backend`** — owns the clip playlist and the current selection, probes videos, and drives thumbnail generation, single-clip export, and the merge.
- **`ffmpeg`** — thin wrappers around `ffprobe` (probing), `ffmpeg` (frame thumbnails, trim/merge argument lists).
- **`thumbworker`** — a `QThread` that renders the 12-frame filmstrip off the UI thread, with up to four parallel ffmpeg jobs and prompt cancellation.
- **`thumbprovider`** — serves the strip to QML as `image://thumbs/<revision>/<index>`.
- **`portalfilepicker`** — talks to the XDG desktop portal over D-Bus for native open/save dialogs (multi-select on open, a Quality combo on save).

Exports are written to a sibling temp file and atomically renamed into place only on success, so a failed or cancelled export never clobbers an existing file. Merges re-encode each clip (normalizing audio to 48 kHz stereo and pixel format to `yuv420p`) and then join them with a lossless concat — so mixed codecs and resolutions always come out as one clean MP4.

The accent color is read from `~/.local/state/omarchy/current/theme/colors.toml` and followed live via a file watcher, with a sensible fallback so the app looks right on distros without Omarchy themes.

## License

MIT — see [LICENSE](LICENSE). Omavid is a fork of [Omacut](https://github.com/omacom-io/omacut).
