#!/usr/bin/env python3
# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

"""Audit an installed SDK and build three isolated consumers (Linux/WSL)."""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shlex
import shutil
import subprocess
import sys
import xml.etree.ElementTree as ET


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def verify_manifest(sdk):
    manifest = sdk / 'manifest.sha256'
    expected = {}
    for line in manifest.read_text().splitlines():
        match = re.fullmatch(r'([0-9a-f]{64}) [ *](.+)', line)
        if not match:
            raise ValueError(f'Invalid manifest line: {line!r}')
        checksum, name = match.groups()
        path = PurePosixPath(name)
        if path.is_absolute() or '..' in path.parts or '\\' in name or ':' in name or name in expected or str(path) != name:
            raise ValueError(f'Unsafe or duplicate manifest path: {name}')
        expected[name] = checksum
    actual = set()
    for path in sdk.rglob('*'):
        if path.is_symlink():
            raise ValueError(f'Symlink in SDK: {path}')
        if path.is_file():
            actual.add(path.relative_to(sdk).as_posix())
    if actual != set(expected) | {'manifest.sha256'}:
        raise ValueError(f'Manifest file-set mismatch: {sorted(actual ^ (set(expected) | {"manifest.sha256"}))}')
    for name, checksum in expected.items():
        if digest(sdk / name) != checksum:
            raise ValueError(f'Manifest hash mismatch: {name}')
    for archive in (sdk / 'lib').glob('*.a'):
        with archive.open('rb') as stream:
            magic = stream.read(8)
        if archive.name == 'libPS5OpenGLCore33.a':
            if not re.search(r'\bGROUP\s*\(', archive.read_text()):
                raise ValueError('Umbrella library is not a GROUP linker script')
        elif magic != b'!<arch>\n':
            raise ValueError(f'Not a regular archive: {archive}')
    for name in ('libSceAgc.so', 'libSceAgcDriver.so'):
        if not (sdk / 'lib' / name).stat().st_size:
            raise ValueError(f'Empty import: {name}')
    return {'sha256': digest(manifest), 'files': len(expected)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('sdk', 'payload-sdk', 'example-dir', 'registry', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--forbid-root', type=Path)
    args = parser.parse_args()
    sdk, payload, example, registry, output = [getattr(args, name).resolve() for name in ('sdk', 'payload_sdk', 'example_dir', 'registry', 'output')]
    output.mkdir(parents=True, exist_ok=False)
    summary = {'status': 'FAIL', 'sdk': str(sdk), 'payload_sdk': str(payload), 'python': sys.version, 'commands': [], 'tools': {}}
    env = os.environ.copy()
    env['PKG_CONFIG_LIBDIR'] = str(sdk / 'lib/pkgconfig')
    env.pop('PKG_CONFIG_PATH', None)
    env['PS5_PAYLOAD_SDK'] = str(payload)

    def run(command, cwd=output, name=None):
        command = list(map(str, command))
        name = name or f'command-{len(summary["commands"]):02d}'
        result = subprocess.run(command, cwd=cwd, env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        (output / (name + '.log')).write_text(result.stdout)
        summary['commands'].append({'argv': command, 'cwd': str(cwd), 'exit_code': result.returncode, 'log': name + '.log'})
        if result.returncode:
            raise RuntimeError(f'{name} failed; see {output / (name + ".log")}')
        return result.stdout

    def reject(path):
        path = path.resolve()
        if args.forbid_root and path.is_relative_to(args.forbid_root.resolve()):
            raise ValueError(f'Operational dependency on original runtime tree: {path}')
        return str(path)

    try:
        summary['manifest'] = verify_manifest(sdk)
        summary['consumer_sources'] = {name: digest(example / name) for name in ('main.c', 'Makefile.installed')}
        cc, cxx = payload / 'bin/prospero-clang', payload / 'bin/prospero-clang++'
        for tool in (str(cc), str(cxx), 'make', 'pkg-config', 'cmake', 'nm', 'clang', 'ld.lld'):
            found = shutil.which(tool)
            if found:
                summary['tools'][tool] = {'path': found, 'sha256': digest(Path(found)), 'version': run([found, '--version'])}
        summary['registry_sha256'] = digest(registry)
        commands = set()
        for feature in ET.parse(registry).getroot().findall('feature'):
            if feature.get('api') != 'gl' or tuple(map(int, feature.get('number').split('.'))) > (3, 3):
                continue
            for requirement in feature.findall('require'):
                if requirement.get('profile') in (None, 'core'):
                    commands.update(c.get('name') for c in requirement.findall('command'))
            for removal in feature.findall('remove'):
                if removal.get('profile') == 'core':
                    commands.difference_update(c.get('name') for c in removal.findall('command'))
        libraries = [sdk / 'lib' / name for name in ('libglapi_bridge.a', 'libglapi.a')]
        symbols = set(re.findall(r'\b(gl[A-Za-z0-9_]+)$', run(['nm', '-g', '--defined-only', *libraries], name='installed-symbols'), re.MULTILINE))
        if len(commands) != 344 or commands - symbols:
            raise ValueError(f'GL surface mismatch: count={len(commands)}, missing={sorted(commands - symbols)}')
        summary['gl33'] = {'commands': 344, 'exported': 344, 'libraries': {str(p): digest(p) for p in libraries}}
        for name in ('make', 'pkgconfig', 'cmake'):
            (output / name).mkdir()
            shutil.copyfile(example / 'main.c', output / name / 'main.c')
        make_dir = output / 'make'
        shutil.copyfile(example / 'Makefile.installed', make_dir / 'Makefile.installed')
        make_log = run(['make', '--no-print-directory', '-f', 'Makefile.installed', '-j2',
                        f'PS5_PAYLOAD_SDK={payload}', f'PS5_OPENGL_PREFIX={sdk}',
                        f'CC={shlex.quote(str(cc))} -MD -MF main.d', f'CXX={shlex.quote(str(cxx))} -Wl,--trace'], make_dir, 'make-build')
        cflags = shlex.split(run(['pkg-config', '--cflags', 'ps5-opengl-core33'], name='pkgconfig-cflags'))
        libs = shlex.split(run(['pkg-config', '--libs', 'ps5-opengl-core33'], name='pkgconfig-libs'))
        if not {'-lPS5OpenGLCore33', '-lSceAgcDriver', '-lSceVideoOut'} <= set(libs):
            raise ValueError('Incomplete pkg-config contract')
        pkg_dir = output / 'pkgconfig'
        run([cc, *cflags, '-MD', '-MF', 'main.d', '-c', 'main.c', '-o', 'main.o'], pkg_dir, 'pkgconfig-compile')
        pkg_log = run([cxx, 'main.o', *libs, '-Wl,--gc-sections', '-Wl,--build-id=sha1', '-Wl,--trace', '-o', 'triangle.elf'], pkg_dir, 'pkgconfig-link')
        cmake_dir = output / 'cmake'
        (cmake_dir / 'CMakeLists.txt').write_text('''cmake_minimum_required(VERSION 3.16)
project(independent_consumer LANGUAGES C CXX)
find_package(PS5OpenGLCore33 CONFIG REQUIRED)
add_executable(triangle main.c)
set_target_properties(triangle PROPERTIES SUFFIX ".elf")
set_property(TARGET triangle PROPERTY LINKER_LANGUAGE CXX)
target_link_libraries(triangle PRIVATE PS5OpenGLCore33::OpenGL)
target_compile_options(triangle PRIVATE -MD)
target_link_options(triangle PRIVATE "LINKER:--gc-sections" "LINKER:--build-id=sha1" "LINKER:--trace")
''')
        run(['cmake', '-S', cmake_dir, '-B', cmake_dir / 'build', '-G', 'Unix Makefiles', f'-DCMAKE_C_COMPILER={cc}',
             f'-DCMAKE_CXX_COMPILER={cxx}', '-DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY',
             f'-DPS5OpenGLCore33_DIR={sdk / "lib/cmake/PS5OpenGLCore33"}'], name='cmake-configure')
        cmake_log = run(['cmake', '--build', cmake_dir / 'build', '--verbose', '-j2'], name='cmake-build')
        summary['consumers'] = {}
        for name, work, trace in [('make', make_dir, make_log), ('pkgconfig', pkg_dir, pkg_log), ('cmake', cmake_dir / 'build', cmake_log)]:
            dependencies = set()
            for depfile in work.rglob('*.d'):
                content = depfile.read_text().replace('\\\n', '')
                for word in shlex.split(content.split(':', 1)[1]):
                    dependencies.add(reject(work / word))
            inputs = set()
            for line in trace.splitlines():
                candidate = line.strip().split('(', 1)[0]
                if candidate and '\x00' not in candidate and len(candidate) < 4096:
                    path = work / candidate
                    try:
                        is_file = path.is_file()
                    except OSError:
                        is_file = False
                    if is_file:
                        inputs.add(reject(path))
            if not dependencies or not any(Path(p).is_relative_to(sdk / 'include') for p in dependencies):
                raise ValueError(f'{name}: missing installed header dependency evidence')
            if not inputs or not any(Path(p).is_relative_to(sdk / 'lib') for p in inputs):
                raise ValueError(f'{name}: missing installed linker input evidence')
            summary['consumers'][name] = {'headers': sorted(dependencies), 'link_inputs': sorted(inputs)}
        summary['outputs'] = {str(p.relative_to(output)): digest(p) for p in output.rglob('*.elf')}
        summary['status'] = 'PASS'
    except Exception as error:
        summary['error'] = str(error)
    (output / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    print(json.dumps({key: summary[key] for key in ('status', 'error') if key in summary}))
    return 0 if summary['status'] == 'PASS' else 1


if __name__ == '__main__':
    sys.exit(main())
