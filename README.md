# Omacut

A dead-simple video **length** trimmer. Open one or more videos, drag the two handles to pick a start and end for each, preview the clip, and export — either a single clip or everything merged into one MP4. On Omarchy, the interface follows your theme's accent color.

## Merging

Every video you open lands in the playlist on the left. Click a clip to edit its trim, use the up/down arrows to reorder, and remove clips with the × button. **Merge** re-encodes each clip (with its own trim, if any) and concatenates them in playlist order. The merge dialog offers the same Original/1080p/720p quality choices as single exports, never upscaling any clip.

Built using **Qt Quick (QML)** UI with the Material style — the same Qt stack Quickshell builds on — and **ffmpeg** for the cut. The C++ side compiles to a single executable; the QML is embedded in it via Qt resources.

<img width="3227" height="3227" alt="screenshot-2026-06-23_15-20-40" src="https://github.com/user-attachments/assets/c76047c8-618f-4c1c-91f9-e7024c4f953b" />

## Hotkeys

- *Space*: Start/stop video playback.
- *Left/Right*: Move the playhead by 1 second.
- *Shift+Left/Right*: Move the playhead by 5 seconds.
- *Alt+Left/Right*: Move the playhead by 0.2 seconds.
- *Ctrl+Space*: Move the start of the trim to the playhead.
- *Alt+Space*: Move the end of the trim to the playhead.
- *Z*: Zoom into the trimmed selection for fine tuning (Z again zooms back out).
- *Ctrl+O*: Open new files to trim.
- *Ctrl+S*: Export the current trim.
- *Ctrl+M*: Merge and export the playlist.
- *Q*: Quit (asks first if a trim hasn't been exported).
- *?*: Show the hotkeys in the app.

## Install

Install via the Omarchy Package Repository via the `omacut` package. It's installed by default in new installations of Omarchy (from Quattro forward).

## Requirements

- `xdg-desktop-portal` and a portal backend for the file picker
- `ffmpeg` and `ffprobe` on your PATH (used at runtime)

Exports are always written as MP4 files, regardless of the input video's container. The export dialog offers Original/1080p/720p quality — never upscaling, and always preserving the aspect ratio.

## Build

Uses Qt's own build tool, `qmake6` (no cmake needed):

```bash
./bin/build
```

This produces a single `omacut` binary in `build/`.

Requirements:

- A C++17 compiler and Qt6: `qt6-base`, `qt6-declarative` (Qt Quick + Controls),
  `qt6-multimedia`

## Test

```bash
./bin/test
```

## Package

Build and install the local Arch package:

```bash
./bin/install
```

This runs `./bin/build`, then `makepkg -fsi` from `pkgbuild/` so same-version local packages are rebuilt and reinstalled. Extra arguments are passed through to `makepkg`, for example `./bin/install --clean`. The package installs the binary, desktop entry, app icon, and MIT license. Local package outputs such as `pkgbuild/pkg/`, `pkgbuild/src/`, and `*.pkg.tar.*` are ignored.

## License

MIT. See `LICENSE`.
