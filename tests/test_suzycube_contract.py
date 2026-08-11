#!/usr/bin/env python3
"""Hermetic release-contract gate for Suzy Cube.

Audits source, generated launcher, BYO recipe, release manifest and the
project-built Linux ELF. It never executes owner Android code.
"""

from __future__ import annotations

import hashlib
import json
import subprocess
import sys
import tempfile
from pathlib import Path

PORT = Path(__file__).resolve().parents[1]
REPO = PORT.parents[1]
PUBLIC_ELF = PORT / "build" / "suzycube-nextos"


def require(value, message):
    if not value:
        raise AssertionError(message)


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def no_duplicates(pairs):
    result = {}
    for key, value in pairs:
        require(key not in result, "duplicate JSON key: %s" % key)
        result[key] = value
    return result


def load_json(path: Path):
    return json.loads(path.read_text(encoding="utf-8"),
                      object_pairs_hook=no_duplicates)


def run(*argv, cwd=None):
    result = subprocess.run(argv, cwd=cwd, text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, check=False)
    require(result.returncode == 0,
            "%s failed (%d)\n%s\n%s" % (" ".join(argv), result.returncode,
                                        result.stdout, result.stderr))
    return result.stdout


def check_launcher_is_generated():
    """The launcher is a build artifact of nxport.json, never hand-edited: it
    is regenerated here and compared byte for byte."""
    nxport = load_json(PORT / "nxport.json")
    require(nxport["schema_version"] == 2, "nxport schema drift")
    require(nxport["id"] == "suzycube", "wrong port id")
    require(nxport["executable"] == "suzycube-nextos", "wrong executable")
    require(nxport["launcher_name"] == "Suzy Cube.sh", "wrong launcher name")
    require(nxport["nxextract"]["version"] == "1.2.6", "nxextract pin drift")
    require(nxport["home_mode"] == "port",
            "saves must stay inside the port directory")

    generator = REPO / "framework/nxbootstrap/tools/generate-port.py"
    version = (REPO / "framework/nxbootstrap/VERSION").read_text().strip()
    with tempfile.TemporaryDirectory() as tmp:
        run(sys.executable, "-B", str(generator), str(PORT / "nxport.json"),
            "--output", tmp)
        regenerated = Path(tmp) / nxport["launcher_name"]
        require(regenerated.read_bytes() ==
                (PORT / nxport["launcher_name"]).read_bytes(),
                "launcher differs from the generated one; do not hand-edit it")
        require((Path(tmp) / "suzycube/nxport.json").read_bytes() ==
                (PORT / "nxport.json").read_bytes(),
                "nxport.json is not in canonical form")

    manifest = load_json(PORT / "nxrelease.json")
    require(manifest["package"]["launcher_contract"]["version"] == version,
            "launcher_contract.version does not match framework VERSION")


def check_retired_artifacts():
    """Rule #34: one launcher, no secondary shell entry point."""
    for dead in ("run.sh", "es_map.sh", "es2sdl.awk", "build.sh",
                 "nxbootstrap.sh", "nxdeployment.json"):
        require(not (PORT / dead).exists(), "retired artifact is back: %s" % dead)
    launcher = (PORT / "Suzy Cube.sh").read_text(encoding="utf-8")
    require("run.sh" not in launcher, "launcher still chains to run.sh")


def check_recipe():
    """The two-source, split-per-source data layout is the whole reason this
    port needs a merge hook; the gate keeps that contract explicit."""
    run(sys.executable, "-B", str(PORT / "nxextract/nxextract.py"),
        "recipe-check", "--recipe", str(PORT / "extractor.json"))
    recipe = load_json(PORT / "extractor.json")
    require(recipe["input"]["packages"] == ["com.noodlecake.suzycube"],
            "recipe must refuse a different package")
    hooks = recipe.get("hooks", [])
    require(len(hooks) == 1 and hooks[0]["id"] == "suzycube-merge-data",
            "the APK+OBB merge hook is missing")
    require((PORT / "nxextract/merge-suzycube-data.py").exists(),
            "merge hook script is missing")
    committed = set(recipe["commit"])
    for rule in recipe["extract"]:
        root = rule["destination"].split("/")[0]
        require(root in committed,
                "destination %s escapes the commit roots" % rule["destination"])


def check_manifest_pins():
    manifest = load_json(PORT / "nxrelease.json")
    require(manifest["package"]["version"] ==
            (PORT / "version.txt").read_text().strip(),
            "nxrelease version does not match version.txt")
    require(manifest["release"]["max_glibc"] == "2.30",
            "public ceiling must stay at the ArkOS/R36S glibc")
    for entry in manifest["files"]:
        source = PORT / entry["source"]
        require(source.exists(), "manifest references a missing file: %s"
                % entry["source"])
        require(sha256(source) == entry["sha256"],
                "stale sha256 pin for %s" % entry["source"])


def check_public_elf():
    require(PUBLIC_ELF.exists(), "build the public ELF first")
    versions = run("readelf", "--version-info", str(PUBLIC_ELF))
    highest = max(
        (line.split("GLIBC_")[1].split()[0].rstrip(")")
         for line in versions.splitlines() if "GLIBC_" in line),
        key=lambda text: [int(part) for part in text.split(".")])
    require([int(p) for p in highest.split(".")] <= [2, 30],
            "public ELF exceeds GLIBC_2.30: %s" % highest)

    dynamic = run("readelf", "-dW", str(PUBLIC_ELF))
    require("RPATH" not in dynamic and "RUNPATH" not in dynamic,
            "public ELF carries a search path")

    symbols = run("nm", str(PUBLIC_ELF))
    names = {line.split()[-1] for line in symbols.splitlines() if line.strip()}
    for required in ("nxcompat_probe", "nxinput_create",
                     "nxaudio_classify_backend", "nx_load", "sc_jni_init"):
        require(required in names, "missing symbol: %s" % required)

    # `/home/` and `/var/home/` are legitimate runtime literals from nxcompat;
    # only a named directory under them would identify the build machine.
    import re
    text = PUBLIC_ELF.read_bytes()
    leak = re.search(rb"/home/[A-Za-z0-9._-]|/mnt/ARQUIVOS|192\.168\.[0-9]", text)
    require(leak is None,
            "public ELF leaks a build-machine path: %r" % (leak and leak.group()))


def check_input_recovery_contract():
    source = (PORT / "src/input.c").read_text(encoding="utf-8")
    helper = (PORT / "src/input_evdev.c").read_text(encoding="utf-8")
    policy = (PORT / "src/input_policy.h").read_text(encoding="utf-8")
    require("EVIOCGKEY" in source,
            "input must resample current evdev key state")
    require("sc_evdev_apply_button_snapshot" in source and
            "sc_evdev_apply_button_snapshot" in helper,
            "authoritative button snapshot helper is disconnected")
    require("SC_FACE_LAYOUT" in source and
            "SC_EVDEV_FACE_NINTENDO_LABELS" in source,
            "capability-based face normalization is missing")
    require("ev.cdevice.which == controller_instance" in source and
            "ev.jdevice.which == controller_instance" in source,
            "hotplug must not close the active pad for an unrelated device")
    require("nxinput_core_filter_stick" in source and
            "SC_INPUT_STICK_ENTER_DEADZONE 0.40f" in policy and
            "SC_INPUT_STICK_EXIT_DEADZONE 0.30f" in policy,
            "worn-stick radial hysteresis policy is missing")
    require("nxinput_core_trigger" in source and
            "SC_INPUT_TRIGGER_DEADZONE 0.05f" in policy and
            "SDL_CONTROLLER_AXIS_TRIGGERLEFT) + 1.0f" not in source,
            "released SDL triggers must stay at zero")
    require("motion_neutral_pending" in source and
            "request_neutral_motion();" in source and
            "motion_non_neutral || motion_neutral_pending" in source,
            "focus/hotplug must flush a neutral MotionEvent")


def main():
    check_launcher_is_generated()
    check_retired_artifacts()
    check_recipe()
    check_manifest_pins()
    check_public_elf()
    check_input_recovery_contract()
    print("SUZY CUBE CONTRACT: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
