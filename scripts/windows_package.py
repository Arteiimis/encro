#!/usr/bin/env python3
"""Build the Windows symbol archive and prove the runtime archive is self-sufficient.

Two jobs, because both are about the same thing: what a Windows user receives.

  pack-symbols  assemble `<runtime-basename>-symbols.zip` from the build's PDB
                plus LICENSE and README.md. xpack copies a target's symbol file
                into bin/ unconditionally and has no declarative exclude, so the
                runtime archive strips it (see xmake.lua's after_installcmd) and
                the symbols ship separately — a crash report must be symbolised
                by exactly the build that produced it.

  verify        list every archive, assert the runtime one holds exactly the
                executables and libraries a user needs, then unpack it and RUN
                it. This is the check that a package whose own binary cannot
                start (2.0.1, whose bundled runtime came from the build image)
                can never ship again. Because the runner already has the
                redistributable installed, the run alone proves little: the
                bundled DLL is then broken on purpose and the executable must
                fail, which is what a machine without the redistributable does.

    python3 scripts/windows_package.py pack-symbols --archives build/xpack/encro
    python3 scripts/windows_package.py verify --archives build/xpack/encro
"""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

RUNTIME_REQUIRED = (
    'bin/encro.exe',
    'bin/onnxruntime.dll',
    'LICENSE',
    'README.md',
    'bin/msvcp140.dll',
    'bin/msvcp140_1.dll',
    'bin/msvcp140_atomic_wait.dll',
    'bin/vcruntime140.dll',
    'bin/vcruntime140_1.dll',
)

# A package carries the files a user runs plus its licence; debug symbols,
# on-demand provider libraries and OS components belong outside it.
FORBIDDEN_SUBSTRINGS = ('onnxruntime_providers_',)
FORBIDDEN_LOWER_SUBSTRINGS = ('ucrtbase', 'api-ms-win-')
FORBIDDEN_SUFFIXES = ('.pdb',)

SYMBOLS_EXPECTED = ('bin/encro.pdb', 'LICENSE', 'README.md')


def find_archives(directory):
    archives = sorted(Path(directory).glob('*.zip'))
    runtime = [path for path in archives if 'symbols' not in path.name]
    symbols = [path for path in archives if 'symbols' in path.name]
    if not runtime:
        raise SystemExit('no runtime archive was produced in %s' % directory)
    return runtime[0], (symbols[0] if symbols else None)


def pack_symbols(archives_dir, pdb):
    runtime, _ = find_archives(archives_dir)
    if not Path(pdb).is_file():
        raise SystemExit('no symbol file at %s' % pdb)

    destination = Path(archives_dir) / ('%s-symbols.zip' % runtime.stem)
    entries = (('bin/encro.pdb', Path(pdb)), ('LICENSE', Path('LICENSE')), ('README.md', Path('README.md')))
    with zipfile.ZipFile(destination, 'w', zipfile.ZIP_DEFLATED) as archive:
        for name, source in entries:
            if not source.is_file():
                raise SystemExit('missing %s for the symbol archive' % source)
            archive.write(source, name)
    print('packed %s (%d bytes)' % (destination.name, destination.stat().st_size))
    return 0


def describe(path):
    print('=== %s  (%s bytes) ===' % (path.name, format(path.stat().st_size, ',')))
    with zipfile.ZipFile(path) as archive:
        for entry in archive.infolist():
            print('    %-34s %12s' % (entry.filename, format(entry.file_size, ',')))


def verify(archives_dir):
    runtime, symbols = find_archives(archives_dir)
    for path in sorted(Path(archives_dir).glob('*.zip')):
        describe(path)

    with zipfile.ZipFile(runtime) as archive:
        names = archive.namelist()

    for needed in RUNTIME_REQUIRED:
        if needed not in names:
            raise SystemExit('%s is missing %s' % (runtime.name, needed))
    for name in names:
        lower = name.lower()
        if name.endswith(FORBIDDEN_SUFFIXES):
            raise SystemExit('%s ships debug symbols: %s' % (runtime.name, name))
        if any(part in name for part in FORBIDDEN_SUBSTRINGS):
            raise SystemExit('%s ships a provider library: %s' % (runtime.name, name))
        if any(part in lower for part in FORBIDDEN_LOWER_SUBSTRINGS):
            raise SystemExit('%s ships an OS component: %s' % (runtime.name, name))

    if symbols is None:
        raise SystemExit('no symbol archive was produced')
    with zipfile.ZipFile(symbols) as archive:
        symbol_names = archive.namelist()
    if sorted(symbol_names) != sorted(SYMBOLS_EXPECTED):
        raise SystemExit('%s should carry %s, found: %s'
                         % (symbols.name, ', '.join(SYMBOLS_EXPECTED), ', '.join(symbol_names)))
    print('archive contents match the intended file list')

    with tempfile.TemporaryDirectory() as unpacked:
        with zipfile.ZipFile(runtime) as archive:
            archive.extractall(unpacked)
        executable = Path(unpacked) / 'bin' / 'encro.exe'
        if not executable.is_file():
            raise SystemExit('the archive has no bin/encro.exe')

        printed = run_version(executable)
        if printed.returncode != 0:
            raise SystemExit('the packaged executable did not start (exit %d)' % printed.returncode)
        print('exit=0 output=%s' % (printed.stdout or printed.stderr).strip())

        bundled = Path(unpacked) / 'bin' / 'msvcp140.dll'
        if not bundled.is_file():
            raise SystemExit('the archive has no bin/msvcp140.dll to test')
        saved = bundled.read_bytes()
        bundled.write_bytes(b'not a dll')
        broken = run_version(executable)
        bundled.write_bytes(saved)
        print('exit with the bundled runtime broken: %d' % broken.returncode)
        if broken.returncode == 0:
            raise SystemExit('the executable ran without its bundled runtime; '
                             'the archive is not self-sufficient')

        restored = run_version(executable)
        if restored.returncode != 0:
            raise SystemExit('the restored package no longer starts')
        print('the archive carries the runtime it loads')
    return 0


def run_version(executable):
    return subprocess.run((str(executable), '--version'), capture_output=True, text=True)


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('command', choices=('pack-symbols', 'verify'))
    parser.add_argument('--archives', default='build/xpack/encro')
    parser.add_argument('--pdb', default='build/windows/x64/release/encro.pdb')
    args = parser.parse_args(argv[1:])

    if args.command == 'pack-symbols':
        return pack_symbols(args.archives, args.pdb)
    return verify(args.archives)


if __name__ == '__main__':
    sys.exit(main(sys.argv))
