# Suzy Cube for NextOS — installation

This package is **bring-your-own-data**. It contains the compatibility loader,
the launcher and the installer. It never contains the game.

## 1. Install the port

Unpack the ZIP into your firmware's ports directory, so that you end up with:

```
ports/Suzy Cube.sh
ports/suzycube/
```

Common locations: `/roms/ports` (ArkOS, R36S), `/storage/roms/ports` (NextOS),
`/roms2/ports` on dual-card setups.

## 2. Provide your own copy of the game

Drop your lawfully acquired **ARM64 Suzy Cube APK** into:

```
ports/suzycube/gamedata/
```

If your copy also came with a separate `main.<n>.com.noodlecake.suzycube.obb`,
put it in the same folder. Many builds carry the OBB inside the APK instead —
the installer handles both without being told which one you have.

## 3. Launch it once

Start **Suzy Cube** from your frontend. The first launch runs the installer
instead of the game:

* it checks the package name, the ABI and the three native libraries;
* it extracts the asset tree from the APK and, when present, from the OBB;
* Suzy Cube ships its data cut into `.splitN` chunks in **both** sources, so the
  installer rejoins them **in numeric order**. A missing chunk is a hard error,
  never a silently truncated file;
* only a fully validated tree is committed. If anything fails, nothing is
  written to your game folder and the next launch starts over cleanly.

Expect this to take a few minutes and roughly **1 GB of free space** on the
card while it runs — the installer deletes each chunk as it is consumed, so the
peak is transient. From the second launch on, the game starts directly.

## 4. Controls

| Input | Action |
|---|---|
| D-pad / left stick | move |
| South face button (Xbox A; printed B on an R36S) | jump / confirm |
| L1 / R1 | switch world on the map |
| **SELECT + START** | save and quit back to the frontend |

The game displays Xbox-style glyphs by position. The port automatically fixes
the common R36S firmware mapping that follows the printed Nintendo-style
letters (A/B and X/Y reversed by position). Existing positional mappings are
left unchanged. You are never asked to capture buttons.

Mapped digital buttons are also resampled from the kernel every frame on pads
that can be matched unambiguously. This prevents a dropped release event from
leaving a D-pad direction stuck. As a compatibility override,
`SC_FACE_LAYOUT=firmware` preserves the firmware's face-button mapping.

## Troubleshooting

The port writes `log.txt` next to the launcher, and the installer writes
`suzycube/nxextract.log`. Those two files describe every decision made, in
order — start there.

If the game exits immediately, the most common cause is an APK for the wrong
architecture (ARMv7 instead of ARM64) or an incomplete download.
