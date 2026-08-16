#!/usr/bin/env python3
"""Hermetic release-contract gate for Suzy Cube.

Audits source, generated launcher, BYO recipe, release manifest and the
project-built Linux ELF. It never executes owner Android code.
"""

from __future__ import annotations

import hashlib
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

PORT = Path(__file__).resolve().parents[1]
PUBLIC_ELF = PORT / "build" / "suzycube-nextos"


def resolve_framework_repository() -> Path:
    configured = os.environ.get("SC_FRAMEWORK_REPOSITORY")
    direct = os.environ.get("SC_FRAMEWORK_ROOT_HOST")
    candidates = []
    if configured:
        candidates.append(Path(configured))
    elif direct:
        candidates.append(Path(direct).parent)
    else:
        result = subprocess.run(
            ("git", "-C", str(PORT), "rev-parse", "--show-toplevel"),
            text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            check=False)
        if result.returncode != 0:
            raise AssertionError("Suzy Cube checkout is not a Git repository")
        top = Path(result.stdout.strip())
        candidates.extend((top, PORT.parent, PORT.parents[1]))
    for candidate in candidates:
        candidate = candidate.resolve()
        if (candidate / "framework/nxgenerator/nxgenerator.py").is_file():
            return candidate
    raise AssertionError(
        "framework repository not found; set SC_FRAMEWORK_REPOSITORY")


REPO = resolve_framework_repository()
FRAMEWORK = REPO / "framework"


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


def check_project_is_generated():
    """nxproject.json is the only declarative source for launcher, nxport,
    PortMaster metadata, NXExtract and NXSplash integration."""
    project = load_json(PORT / "nxproject.json")
    nxport = load_json(PORT / "nxport.json")
    require(project["schema_version"] == 2, "nxproject schema drift")
    expected_nxport = dict(project["nxport"])
    expected_required = list(expected_nxport["required_files"])
    if "nxsplash-nextos" not in expected_required:
        expected_required.insert(1, "nxsplash-nextos")
    expected_nxport["required_files"] = expected_required
    require(expected_nxport == nxport,
            "nxport differs from nxproject plus generated NXSplash")
    require(nxport["schema_version"] == 2, "nxport schema drift")
    require(nxport["id"] == "suzycube", "wrong port id")
    require(nxport["executable"] == "suzycube-nextos", "wrong executable")
    require(nxport["launcher_name"] == "Suzy Cube.sh", "wrong launcher name")
    require(nxport["nxextract"]["version"] == "1.2.10", "nxextract pin drift")
    require(nxport["home_mode"] == "port",
            "saves must stay inside the port directory")

    generator = FRAMEWORK / "nxgenerator/nxgenerator.py"
    bootstrap_version = (FRAMEWORK / "nxbootstrap/VERSION").read_text().strip()
    with tempfile.TemporaryDirectory() as tmp:
        output = Path(tmp) / "generated"
        run(sys.executable, "-B", str(generator),
            str(PORT / "nxproject.json"), "--source-root", str(PORT),
            "--output", str(output))
        generated_port = output / "suzycube"
        comparisons = {
            output / nxport["launcher_name"]: PORT / nxport["launcher_name"],
            generated_port / "nxport.json": PORT / "nxport.json",
            generated_port / "nxproject.json": PORT / "nxproject.json",
            generated_port / "port.json": PORT / "port.json",
            generated_port / "extractor.json": PORT / "extractor.json",
            generated_port / "nxsplash-nextos": PORT / "nxsplash-nextos",
            generated_port / "nxextract/nxextract.py":
                PORT / "nxextract/nxextract.py",
            generated_port / "nxextract/run-extractor.sh":
                PORT / "nxextract/run-extractor.sh",
            generated_port / "nxextract/nxextract-runtime-env.sh":
                PORT / "nxextract/nxextract-runtime-env.sh",
            generated_port / "nxextract/nxextract-ui":
                PORT / "nxextract/nxextract-ui",
        }
        for generated, checked_in in comparisons.items():
            require(generated.read_bytes() == checked_in.read_bytes(),
                    "%s differs from nxgenerator output" % checked_in.name)

        receipt = load_json(generated_port / "GENERATION.json")
        require(receipt["generator"]["version"] == "0.2.10",
                "nxgenerator receipt version drift")
        require(receipt["source_pins"]["nxextract"]["version"] == "1.2.10",
                "generated NXExtract engine pin drift")
        require(receipt["source_pins"]["nxextract"]["ui_version"] == "1.2.9",
                "generated NXExtract UI pin drift")
        require(receipt["source_pins"]["nxsplash"]["version"] == "0.1.2",
                "generated NXSplash pin drift")

    manifest = load_json(PORT / "nxrelease.json")
    require(manifest["package"]["launcher_contract"]["version"] ==
            bootstrap_version,
            "launcher_contract.version does not match framework VERSION")

    metadata = load_json(PORT / "port.json")
    require(metadata["version"] == 4, "PortMaster metadata must be schema v4")
    require(metadata["attr"]["runtime"] == [],
            "PortMaster runtime must be an explicit list")
    require(metadata["attr"]["arch"] == ["aarch64"],
            "PortMaster architecture drift")
    require(metadata["attr"]["min_glibc"] == "2.27",
            "PortMaster glibc floor drift")


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
    reference_container_sha = (
        "49afbb38b5be44d2edbbb7ec4b3d0f8a8d805e6a39541e79e4d6d0c41e27ab0b"
    )
    require(reference_container_sha not in
            (PORT / "extractor.json").read_text(encoding="utf-8"),
            "reference APK SHA must identify evidence, never lock the recipe")
    require(recipe["input"]["packages"] == ["com.noodlecake.suzycube"],
            "recipe must refuse a different package")
    require(recipe["abi_order"] == ["arm64-v8a"],
            "recipe must fail closed on a different ABI")
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


def check_public_documentation():
    public_paths = (
        PORT / "README.md", PORT / "INSTALLATION.md", PORT / "NOTICE.md",
        PORT / "nxextract-version.txt", PORT / "nxrelease.json")
    public_text = "\n".join(
        path.read_text(encoding="utf-8") for path in public_paths).lower()
    for forbidden in ("apkpure", "apkmirror", "apkvision", "5play"):
        require(forbidden not in public_text,
                "public files reveal an APK origin: %s" % forbidden)

    installation = (PORT / "INSTALLATION.md").read_text(encoding="utf-8")
    for required in (
            "## Português", "## English", "Suzy Cube 1.0.13",
            "com.noodlecake.suzycube", "arm64-v8a", "113052018",
            "49afbb38b5be44d2edbbb7ec4b3d0f8a8d805e6a39541e79e4d6d0c41e27ab0b"):
        require(required in installation,
                "INSTALLATION.md lacks required owner-data identity: %s" %
                required)


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
    require("sc_evdev_any_key_pressed" in source and
            "sc_evdev_any_key_pressed" in helper,
            "idle guard against a stuck SDL button state is disconnected")
    # A numeracao de botao do firmware nao e' portavel (o muOS numera as
    # teclas nao-gamepad antes do bloco gamepad): ler estado de botao por
    # ordinal traduzido em keycode foi o defeito de campo v1.1.2-v1.1.6.
    require("sc_evdev_code_for_sdl_button" not in source and
            "sc_evdev_code_for_sdl_button" not in helper,
            "button state must never be read through an SDL ordinal")
    require("sc_evdev_abs_code_for_sdl_axis" in source and
            "SDL_CONTROLLER_BINDTYPE_AXIS" in source,
            "stick axes must be resolved through the firmware mapping bind")
    require("value = SDL_GameControllerGetAxis(controller, axis);" in source,
            "mapped stick values must come from SDL's calibrated output")
    require("sc_evdev_axis_normalize" not in source and
            "evdev_axis_value" not in source,
            "mapped stick values must not bypass SDL through raw evdev")
    require("SC_FACE_LAYOUT" in source and
            "SC_EVDEV_FACE_NINTENDO_LABELS" in source,
            "capability-based face normalization is missing")
    require("configured_face_policy = FACE_POLICY_AUTO;" in source and
            "usando auto" in source,
            "face buttons must normalize proven Nintendo layouts by default")
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
    require("sc_input_dpad_axes" in source,
            "the unit-tested full-level D-pad path is disconnected")
    require("dlsym(RTLD_DEFAULT, \"SDL_JoystickGetVendor\")" in source and
            "dlsym(RTLD_DEFAULT, \"SDL_JoystickGetProduct\")" in source,
            "SDL 2.0.6 joystick identity must remain an optional runtime API")
    require("SDL_JoystickGetVendor(joy)" not in source and
            "SDL_JoystickGetProduct(joy)" not in source,
            "public ELF must not raise its SDL floor for joystick identity")

    bionic = (PORT / "src/bionic.c").read_text(encoding="utf-8")
    require('A("reallocarray", my_reallocarray)' in bionic and
            "E(reallocarray)" not in bionic,
            "reallocarray must use the overflow-safe low-glibc shim")


def main():
    check_project_is_generated()
    check_retired_artifacts()
    check_recipe()
    check_public_documentation()
    check_manifest_pins()
    check_public_elf()
    check_input_recovery_contract()
    print("SUZY CUBE CONTRACT: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
