#!/usr/bin/env python3
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
"""Offline SDL distribution checks; optionally compile/link relocated real consumers."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import importlib.util
import io
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tarfile
from tempfile import TemporaryDirectory
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("sdl_build", ROOT / "integration/SDL2/build.py")
BUILD = importlib.util.module_from_spec(spec)
spec.loader.exec_module(BUILD)
spec = importlib.util.spec_from_file_location("sdl_folder", BUILD.HERE / "folder.py")
FOLDER = importlib.util.module_from_spec(spec)
with patch.dict(sys.modules, {"build": BUILD}):
    spec.loader.exec_module(FOLDER)


def write(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data if isinstance(data, bytes) else data.encode())


def manifest(root):
    write(root / "manifest.sha256", "".join(
        f"{BUILD.digest(p)}  {p.relative_to(root).as_posix()}\n"
        for p in sorted(root.rglob("*"), key=lambda p: p.as_posix())
        if p.is_file() and p.name != "manifest.sha256"))


def source_tar(path, revision=BUILD.SDL_REV, extra=None):
    with tarfile.open(path, "w", format=tarfile.PAX_FORMAT, pax_headers={"comment": revision}) as tar:
        member = tarfile.TarInfo("LICENSE.txt")
        member.size = len(b"fixture SDL license\n")
        tar.addfile(member, io.BytesIO(b"fixture SDL license\n"))
        if extra:
            member = tarfile.TarInfo(extra[0])
            member.type = extra[1]
            member.linkname = "LICENSE.txt" if member.issym() or member.islnk() else ""
            tar.addfile(member)


def profile_header(root, width, height, fps):
    write(root / "include/ps5_opengl_display.h", "// Host format fixture; not a native SDK.\n#pragma once\n" +
          "".join(f"#define PS5_OPENGL_NATIVE_{name} {value}\n"
                  for name, value in (("WIDTH", width), ("HEIGHT", height), ("FPS", fps))))
    manifest(root)


def fixture(root, profile=None, example="smoke"):
    """Small format fixtures, never executable SDL/GL or claimed native evidence."""
    gl, native = root / "gl", root / "native"
    for name in ("ps5_opengl_core33", "glapi_bridge", "psbc.ps5", "mesa", "mesa_sse41",
                 "gallium", "glapi", "glsl", "glcpp", "glsl_util", "vtn", "nir", "compiler",
                 "mesa_util", "mesa_util_simd", "blake3", "mesa_util_c11"):
        write(gl / f"lib/lib{name}.a", b"!<arch>\n")
    for name in ("include/EGL/egl.h", "include/EGL/eglext.h", "include/EGL/eglplatform.h",
                 "include/GL/glcorearb.h", "include/KHR/khrplatform.h",
                 "lib/libSceAgc.so", "lib/libSceAgcDriver.so",
                 "lib/pkgconfig/ps5-opengl-core33.pc",
                 "lib/cmake/PS5OpenGLCore33/PS5OpenGLCore33Config.cmake"):
        write(gl / name, "fixture\n")
    write(gl / "lib/libPS5OpenGLCore33.a", "GROUP ( libglapi.a )\n")
    manifest(gl)
    if profile:
        profile_header(gl, *profile)
    native.mkdir()
    source_tar(native / "sdl-source.tar")
    shutil.copytree(BUILD.HERE, native / "integration", ignore=shutil.ignore_patterns("__pycache__"))
    for name in BUILD.NATIVE_ARTIFACTS:
        write(native / name, b"!<arch>\n")
    for name in BUILD.PAYLOAD_REQUIRED:
        write(native / "sdk" / name, b"!<arch>\n" if name.endswith(".a") else b"fixture\n")
    shutil.copyfile(ROOT / "LICENSE", native / "sdk/share/licenses/SDL2-PS5/LICENSE")
    shutil.copyfile(BUILD.HERE / "README.md", native / "sdk/share/SDL2/README.md")
    write(native / "sdk/share/licenses/SDL2/LICENSE.txt", "fixture SDL license\n")
    # Stand-ins for the two upstream install files intentionally excluded by the lane.
    write(native / "sdk/bin/sdl2-config", "unused\n")
    write(native / "sdk/share/aclocal/sdl2.m4", "unused\n")
    with patch.object(BUILD, "SDL_SOURCE_SHA256", BUILD.digest(native / "sdl-source.tar")):
        BUILD.record_receipt(native, "native", gl, BUILD.verify_sdk(gl), example)
    return gl, native


def edit_receipt(native, mutate):
    path = native / "receipt.json"
    receipt = json.loads(path.read_text())
    mutate(receipt)
    write(path, json.dumps(receipt))


def append(path, text):
    write(path, path.read_text() + text)


def reseal_payload(native, mutate):
    sdk = native / "sdk"
    path = sdk / BUILD.PAYLOAD_RECEIPT
    receipt = json.loads(path.read_text())
    mutate(sdk, receipt)
    write(path, json.dumps(receipt))
    manifest(sdk)
    edit_receipt(native, lambda r: r.update(payload_receipt_sha256=BUILD.digest(path),
                                            payload_manifest_sha256=BUILD.digest(sdk / "manifest.sha256")))


def negative_checks():
    cases = {
        "GL tampered archive": lambda g, n: write(g / "lib/libglapi.a", b"tampered"),
        "GL unlisted file": lambda g, n: write(g / "extra", "extra"),
        "GL malformed manifest": lambda g, n: write(g / "manifest.sha256", "bad\n"),
        "GL duplicate entry": lambda g, n: append(g / "manifest.sha256", (g / "manifest.sha256").read_text().splitlines(True)[0]),
        "GL missing required artifact": lambda g, n: ((g / "include/GL/glcorearb.h").unlink(), manifest(g)),
        "GL rehashed runtime": lambda g, n: (write(g / "lib/libps5_opengl_core33.a", b"!<arch>\nchanged"), manifest(g)),
        "GL changed manifest order": lambda g, n: write(g / "manifest.sha256", "".join(reversed((g / "manifest.sha256").read_text().splitlines(True)))),
        "native archive": lambda g, n: write(n / "cmake/sdl/libSDL2.a", "bad"),
        "missing native object": lambda g, n: (n / "cmake/CMakeFiles/g19-example.dir/example.c.obj").unlink(),
        "missing receipt artifacts": lambda g, n: edit_receipt(n, lambda r: r.update(artifacts={})),
        "legacy receipt": lambda g, n: edit_receipt(n, lambda r: r.pop("sdk_manifest_sha256")),
        "wrong schema": lambda g, n: edit_receipt(n, lambda r: r.update(schema_version=2)),
        "unknown example": lambda g, n: edit_receipt(n, lambda r: r.update(example="virtual-reconnect")),
        "example payload mismatch": lambda g, n: edit_receipt(n, lambda r: r.update(example="input-validation")),
        "missing input source": lambda g, n: (n / "integration/input_validation.c").unlink(),
        "changed joystick patch": lambda g, n: append(n / "integration/ps5-joystick.patch", "bad\n"),
        "claimed hardware": lambda g, n: edit_receipt(n, lambda r: r.update(hardware_run=True)),
        "host receipt": lambda g, n: edit_receipt(n, lambda r: r.update(mode="host")),
        "payload archive": lambda g, n: write(n / "sdk/lib/libSDL2.a", "bad"),
        "payload header": lambda g, n: write(n / "sdk/include/SDL2/SDL_config.h", "bad"),
        "payload unlisted file": lambda g, n: write(n / "sdk/extra", "bad"),
        "payload manifest": lambda g, n: append(n / "sdk/manifest.sha256", "bad\n"),
        "payload receipt": lambda g, n: append(n / "sdk" / BUILD.PAYLOAD_RECEIPT, " "),
        "rehashed payload traversal": lambda g, n: reseal_payload(n, lambda s, r: r["artifacts"].update({"../escape": "0" * 64})),
        "rehashed empty payload": lambda g, n: reseal_payload(n, lambda s, r: r.update(artifacts={})),
        "rehashed SDL license": lambda g, n: reseal_payload(n, lambda s, r: (write(s / "share/licenses/SDL2/LICENSE.txt", "wrong license"), r["artifacts"].update({"share/licenses/SDL2/LICENSE.txt": BUILD.digest(s / "share/licenses/SDL2/LICENSE.txt")}))),
        "rehashed empty manifest": lambda g, n: (write(n / "sdk/manifest.sha256", ""), edit_receipt(n, lambda r: r.update(payload_manifest_sha256=BUILD.digest(n / "sdk/manifest.sha256")))),
        "source bytes": lambda g, n: append(n / "sdl-source.tar", "bad"),
        "rewritten source and receipt": lambda g, n: (source_tar(n / "sdl-source.tar", extra=("extra", tarfile.REGTYPE)), edit_receipt(n, lambda r: r.update(sdl_source_tar_sha256=BUILD.digest(n / "sdl-source.tar")))),
        "integration bytes": lambda g, n: append(n / "integration/build.py", "# changed\n"),
        "integration extra": lambda g, n: write(n / "integration/extra", "bad"),
        "integration profile header": lambda g, n: append(n / "integration/ps5g19_display.h", "#error changed\n"),
        "wrong display receipt": lambda g, n: edit_receipt(n, lambda r: r.update(display_profile=dict(width=2560, height=1440, fps=120))),
        "unlisted display header": lambda g, n: write(g / "include/ps5_opengl_display.h", "bad\n"),
        "rehashed invalid display header": lambda g, n: profile_header(g, 1920, 1440, 120),
        "rehashed different profile": lambda g, n: profile_header(g, 2560, 1440, 120),
        "receipt tool": lambda g, n: edit_receipt(n, lambda r: r.update(receipt_tool_sha256="0" * 64)),
        "GL directory symlink": lambda g, n: (g / "alias").symlink_to(g / "lib", target_is_directory=True),
        "payload directory symlink": lambda g, n: ((n / "sdk/include").rename(n / "original-include"), (n / "sdk/include").symlink_to(n / "original-include", target_is_directory=True)),
        "receipt symlink": lambda g, n: ((n / "receipt.json").rename(n / "original-receipt"), (n / "receipt.json").symlink_to(n / "original-receipt")),
        "GL special file": lambda g, n: os.mkfifo(g / "fifo"),
        "payload special file": lambda g, n: os.mkfifo(n / "sdk/fifo"),
        "receipt special file": lambda g, n: ((n / "receipt.json").unlink(), os.mkfifo(n / "receipt.json")),
    }
    for path in ("../escape", "/escape", "C:/escape", "a\\b", "a/./b", "a//b"):
        cases[f"GL path {path}"] = lambda g, n, p=path: append(g / "manifest.sha256", "0" * 64 + f"  {p}\n")
        cases[f"receipt path {path}"] = lambda g, n, p=path: edit_receipt(n, lambda r: r["artifacts"].update({p: "0" * 64}))
    # WSL's temporary filesystem supports FIFO checks; Windows-mounted builds may not.
    with TemporaryDirectory(prefix="sdl-sdk-check-") as temporary:
        temporary = Path(temporary)
        base = temporary / "base"
        base.mkdir()
        gl, native = fixture(base)
        fixture_hash = BUILD.digest(native / "sdl-source.tar")
        with patch.object(BUILD, "SDL_SOURCE_SHA256", fixture_hash):
            BUILD.verify_native_build(native, gl)
            write(temporary / "later-docs/README.md", "Later acceptance companion, not build input")
            with patch.object(BUILD, "HERE", temporary / "later-docs"):
                BUILD.verify_native_build(native, gl)
            for index, (name, mutate) in enumerate(cases.items()):
                case = temporary / str(index)
                shutil.copytree(base, case)
                g, n = case / "gl", case / "native"
                mutate(g, n)
                try:
                    BUILD.verify_native_build(n, g)
                except (ValueError, OSError, tarfile.TarError):
                    pass
                else:
                    raise AssertionError(f"Accepted {name}")
        # Exercise tar structure separately with a fixture-only trusted checksum.
        tar_cases = [("../escape", tarfile.REGTYPE), ("/escape", tarfile.REGTYPE),
                     ("LICENSE.txt", tarfile.REGTYPE), ("alias", tarfile.SYMTYPE),
                     ("hardlink", tarfile.LNKTYPE), ("device", tarfile.CHRTYPE)]
        for extra in tar_cases:
            source_tar(native / "sdl-source.tar", extra=extra)
            checksum = BUILD.digest(native / "sdl-source.tar")
            edit_receipt(native, lambda r: r.update(sdl_source_tar_sha256=checksum))
            with patch.object(BUILD, "SDL_SOURCE_SHA256", checksum):
                try:
                    BUILD.verify_native_build(native, gl)
                except ValueError:
                    pass
                else:
                    raise AssertionError(f"Accepted tar member {extra}")
        os.mkfifo(gl / "lib/unexpected.a")
        # A regression fails immediately in the mock, never by reading the FIFO.
        with patch.object(BUILD.SDK_CHECKER, "verify_manifest",
                          side_effect=AssertionError("Shared checker reached a special SDK node")) as checker:
            try:
                BUILD.verify_sdk(gl)
            except ValueError as error:
                assert "Special file in SDK" in str(error)
            else:
                raise AssertionError("Accepted special SDK archive")
            checker.assert_not_called()
    print(f"PASS: valid fixture, {len(cases)} integrity/path mutations, {len(tar_cases)} unsafe tar cases, nonregular-node preflight")


def profile_checks():
    with TemporaryDirectory(prefix="sdl-profile-check-") as temporary:
        temporary = Path(temporary)
        for index, profile in enumerate([None] + [(w, h, f) for w, h in
                ((1920, 1080), (2560, 1440), (3840, 2160)) for f in (60, 120)]):
            root = temporary / str(index)
            root.mkdir()
            example = "input-validation" if index % 2 else "smoke"
            gl, native = fixture(root, profile, example)
            width, height, fps = profile or (1920, 1080, 60)
            selected = dict(width=width, height=height, fps=fps)
            identity = BUILD.verify_sdk(gl)
            assert set(identity) == {"sdk_manifest_sha256", "sdk_files", "sdk_runtime_sha256"}
            assert BUILD.SDK_CHECKER.display_profile(gl) == selected
            receipt = json.loads((native / "receipt.json").read_text())
            assert receipt["display_profile"] == selected
            assert receipt["example"] == example
            assert "ps5g19_display.h" in receipt["integration_inputs"]
            param = FOLDER.folder_parameters(selected)
            baseline = json.loads((ROOT / "native-app/param.json").read_text())
            language = baseline["localizedParameters"]["defaultLanguage"]
            baseline["localizedParameters"][language]["titleName"] = "SDL2 public SDK local candidate"
            if fps > 60:
                baseline["attribute3"] = 0x80040
            assert param == baseline
            code = '#include "ps5g19_display.h"\n' + "".join(
                f'_Static_assert(PS5_OPENGL_NATIVE_{name.upper()} == {value}, "{name}");\n'
                for name, value in selected.items())
            subprocess.run(["cc", "-std=c11", "-Werror", "-fsyntax-only", "-x", "c", "-",
                            "-I", str(BUILD.HERE), "-I", str(gl / "include")],
                           input=code, text=True, check=True)
            with patch.object(BUILD, "SDL_SOURCE_SHA256", BUILD.digest(native / "sdl-source.tar")):
                # Exercise the exact old schema/file-set without rewriting frozen builds.
                def old_receipt(r):
                    r.pop("display_profile")
                    r.pop("example")
                    r["integration_inputs"].pop("ps5g19_display.h")
                    for name in ("input_validation.c", "test_input.c", "ps5-joystick.patch"):
                        r["integration_inputs"].pop(name)
                (native / "integration/ps5g19_display.h").unlink()
                for name in ("input_validation.c", "test_input.c", "ps5-joystick.patch"):
                    (native / "integration" / name).unlink()
                edit_receipt(native, old_receipt)
                reseal_payload(native, lambda s, r: old_receipt(r))
                if profile is None:
                    BUILD.verify_native_build(native, gl)
                else:
                    try:
                        BUILD.verify_native_build(native, gl)
                    except ValueError as error:
                        assert "display profile mismatch" in str(error)
                    else:
                        raise AssertionError("Profile SDK accepted an old unprofiled receipt")
        for width, height, fps in ((1920, 1440, 120), (2560, 1080, 60), (3840, 2160, 90)):
            profile_header(gl, width, height, fps)
            try:
                BUILD.verify_sdk(gl)
            except ValueError:
                pass
            else:
                raise AssertionError("Accepted invalid manifest-covered profile")
            result = subprocess.run(["cc", "-fsyntax-only", "-x", "c", "-", "-I", str(BUILD.HERE),
                                     "-I", str(gl / "include")], input='#include "ps5g19_display.h"\n',
                                    text=True, capture_output=True)
            assert result.returncode and "Unsupported PS5 OpenGL display profile" in result.stderr
    print("PASS: legacy receipt, six profiles, compiled constants, HFR parameters, invalid profiles")


def host_matrix(source, sdk, out):
    """Real SDL + sanitizers; copied profile fixtures never claim native runtime modes."""
    identity = BUILD.verify_sdk(sdk)
    if (out.exists() or out == ROOT / "build" or not out.is_relative_to(ROOT / "build")
            or any(out.is_relative_to(p) or p.is_relative_to(out) for p in (source, sdk))):
        raise ValueError("Host output must be a new directory below this clone's build/, outside inputs")
    out.mkdir(parents=True)

    def build_profile(profile):
        width, height, fps = profile
        name = f"{height}p{fps}"
        copied = out / (name + "-host-fixture-sdk")
        shutil.copytree(sdk, copied)
        if height == 1080:
            (copied / "include/ps5_opengl_display.h").unlink(missing_ok=True)
            manifest(copied)  # Exercise the explicit old-SDK fallback.
        else:
            profile_header(copied, width, height, fps)
        stage = out / name
        log = out / (name + ".log")
        with log.open("w") as stream:
            result = subprocess.run([sys.executable, str(BUILD.HERE / "build.py"), "host",
                "--sdl-source", str(source), "--sdk-prefix", str(copied), "--out", str(stage)],
                stdout=stream, stderr=subprocess.STDOUT)
        assert result.returncode == 0, f"Host build failed: {log}"
        contract = (stage / "cmake/Testing/Temporary/LastTest.log").read_text()
        assert f"drawable={width}x{height} nominal_refresh={fps}Hz (not negotiated HDMI)" in contract
        assert "frames=180 probes=2 status=0" in contract
        assert "G42 PS5 driver ownership/reconnect: PASS (platform doubles)" in contract
        assert "G42 input app contracts: PASS (no GPU or physical input acceptance)" in contract
        receipt = json.loads((stage / "receipt.json").read_text())
        assert receipt["mode"] == "host" and receipt["hardware_run"] is False
        assert receipt["display_profile"] == dict(width=width, height=height, fps=fps)
        print(f"PASS: real SDL + ASan/UBSan {name} (copied host fixture): {log}", flush=True)
    with ThreadPoolExecutor(max_workers=3) as pool:
        list(pool.map(build_profile, ((1920, 1080, 60), (2560, 1440, 120), (3840, 2160, 120))))
    assert BUILD.verify_sdk(sdk) == identity


def consumers(native, gl, payload, out):
    receipt = BUILD.verify_native_build(native, gl)
    if (out.exists() or out == ROOT / "build" or not out.is_relative_to(ROOT / "build")
            or any(out.is_relative_to(p) or p.is_relative_to(out) for p in (native, gl, payload))):
        raise ValueError("Consumer output must be a new directory below this clone's build/, outside inputs")
    out.mkdir(parents=True)
    relocated_sdl, relocated_gl = out / "sdl2", out / "opengl"
    shutil.copytree(native / "sdk", relocated_sdl)
    shutil.copytree(gl, relocated_gl)
    assert BUILD.verify_sdk(relocated_gl)["sdk_manifest_sha256"] == receipt["sdk_manifest_sha256"]
    env = {**os.environ, "PS5_PAYLOAD_SDK": str(payload),
           "PKG_CONFIG_LIBDIR": f"{relocated_sdl}/lib/pkgconfig:{relocated_gl}/lib/pkgconfig"}
    env.pop("PKG_CONFIG_PATH", None)
    commands = []

    def run(*args, cwd=out):
        result = subprocess.run(list(map(str, args)), cwd=cwd, env=env, text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        log = f"command-{len(commands):02d}.log"
        write(out / log, result.stdout)
        commands.append({"argv": list(map(str, args)), "exit_code": result.returncode, "log": log})
        write(out / "commands.json", json.dumps(commands, indent=2))
        if result.returncode:
            raise AssertionError(f"Consumer command failed: {out / log}")
        return result.stdout

    run("sha256sum", "--check", "--strict", "manifest.sha256", cwd=relocated_sdl)
    for path in relocated_sdl.rglob("*"):
        if path.is_file():
            data = path.read_bytes()
            for forbidden in (str(ROOT).encode(), str(gl).encode(), str(payload).encode()):
                assert forbidden not in data, f"Machine path in {path}"
    cc, cxx = payload / "bin/prospero-clang", payload / "bin/prospero-clang++"
    source = '#ifndef SDL_MAIN_HANDLED\n#error Missing SDL_MAIN_HANDLED\n#endif\n' + (BUILD.HERE / "example.c").read_text()
    write(out / "main.c", source)
    flags = shlex.split(run("pkg-config", "--cflags", "sdl2"))
    libs = shlex.split(run("pkg-config", "--libs", "--static", "sdl2"))
    assert "-lSDL2" in libs and "-lPS5OpenGLCore33" in libs and "-lScePad" in libs
    run(cc, *flags, "-fPIC", "-MD", "-MF", "pkgconfig.d", "-c", "main.c", "-o", "main.o")
    run(cxx, "main.o", *libs, "-Wl,--gc-sections", "-Wl,--trace", "-o", "pkgconfig.elf")
    write(out / "CMakeLists.txt", '''cmake_minimum_required(VERSION 3.16)
project(SDLConsumer C CXX)
find_package(SDL2 CONFIG REQUIRED)
add_executable(consumer main.c)
set_target_properties(consumer PROPERTIES LINKER_LANGUAGE CXX)
target_compile_options(consumer PRIVATE -fPIC)
target_link_libraries(consumer PRIVATE SDL2::SDL2)
target_link_options(consumer PRIVATE -Wl,--gc-sections -Wl,--trace)
''')
    run("cmake", "-S", out, "-B", out / "cmake", "-G", "Ninja",
        "-DCMAKE_SYSTEM_NAME=Generic", "-DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY",
        f"-DCMAKE_C_COMPILER={cc}", f"-DCMAKE_CXX_COMPILER={cxx}",
        f"-DCMAKE_PREFIX_PATH={relocated_sdl};{relocated_gl}")
    run("cmake", "--build", out / "cmake")
    for path in (out / "pkgconfig.elf", out / "cmake/consumer"):
        assert path.read_bytes()[:4] == b"\x7fELF"
    for path in (out / "pkgconfig.d", out / "cmake/build.ninja"):
        text = path.read_text()
        assert str(native) not in text and str(gl) not in text
    print(f"PASS: relocated pkg-config and CMake SDL example compile/link (not executed): {out}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("native-build", "sdk-prefix", "payload-sdk", "out",
                 "sdl-source", "host-sdk-prefix", "host-out"):
        parser.add_argument("--" + name, type=Path)
    args = parser.parse_args()
    values = (args.native_build, args.sdk_prefix, args.payload_sdk, args.out)
    if any(values) and not all(values):
        parser.error("real consumer checks require --native-build, --sdk-prefix, --payload-sdk, --out")
    host_values = (args.sdl_source, args.host_sdk_prefix, args.host_out)
    if any(host_values) and not all(host_values):
        parser.error("host matrix requires --sdl-source, --host-sdk-prefix, --host-out")
    (ROOT / "build").mkdir(exist_ok=True)
    negative_checks()
    profile_checks()
    if all(host_values):
        host_matrix(*(path.resolve() for path in host_values))
    if all(values):
        consumers(*(path.resolve() for path in values))
