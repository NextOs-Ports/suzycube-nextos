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

The port is validated against **Suzy Cube v1.0.13 arm64**
(`com.noodlecake.suzycube`). Other arm64 builds of the same package are
accepted by the installer and should work; ARMv7-only copies do **not** work.

Drop your lawfully acquired **ARM64 Suzy Cube APK** into:

```
ports/suzycube/gamedata/
```

**The Google Play build ships its data in a separate OBB file.** If your APK
came with a `main.<n>.com.noodlecake.suzycube.obb` (on the phone it lives in
`Android/obb/com.noodlecake.suzycube/`), you MUST copy it into the same folder
— the APK alone is not enough. Some repacked builds carry the OBB inside the
APK instead; the installer handles both without being told which one you have,
and tells you in `suzycube/nxextract.log` if the data is incomplete.

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
| A (the letter shown by the game) | jump / confirm |
| B (the letter shown by the game) | back / cancel |
| L1 / R1 | switch world on the map |
| **SELECT + START** | save and quit back to the frontend |

Since v1.1.10, face buttons follow the firmware's own A/B/X/Y meaning by
default: when the game displays A, press the button printed A on the device.
This keeps a normal Xbox controller unchanged and makes Nintendo-labelled R36S
handhelds match the on-screen letters. To retain v1.1.9's position-based
behavior, set `SC_FACE_LAYOUT=auto`; `SC_FACE_LAYOUT=xbox` forces Xbox
positions whenever an unambiguous `evdev` snapshot is available.

Mapped digital buttons are also resampled from the kernel every frame on pads
that can be matched unambiguously. This prevents a dropped release event from
leaving a D-pad direction stuck.

Version 1.1.10 keeps the `evdev` probe only to verify that a mapped physical
axis is genuinely analog, while taking its value from SDL's calibrated mapped
output. This preserves firmware inversion, half-axis and scaling rules and
fixes a lower-left diagonal arriving as a half press. The existing radial
0.40/0.30 hysteresis, trigger handling and neutral focus/hotplug sample remain
unchanged.

## Troubleshooting

The port writes `log.txt` next to the launcher, and the installer writes
`suzycube/nxextract.log`. Those two files describe every decision made, in
order — start there.

If the game exits immediately, the most common cause is an APK for the wrong
architecture (ARMv7 instead of ARM64) or an incomplete download.
