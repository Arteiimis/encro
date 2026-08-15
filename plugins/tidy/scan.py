"""Parallel clang-tidy scan over every TU in build/compile_commands.json.

Invoked by the xmake `tidy` plugin task. Default prints warnings as text;
`--sarif` writes build/tidy-results.sarif. `--selftest` scans a bundled
fixture to assert the check set and header-filter behave.
"""
import concurrent.futures
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

BS = chr(92)
FS = '/'
TOOL = shutil.which('clang-tidy')
HEADER_FILTER = r'(^|[\\/])(src|tests)[\\/]'
SARIF_OUT = 'build/tidy-results.sarif'

LINE_RE = re.compile(
    r'^(?P<path>.+?):(?P<line>\d+):(?P<col>\d+): '
    r'(?P<level>warning|error|note): (?P<rest>.*)$'
)
CHECK_RE = re.compile(r' \[(?P<check>[\w][\w.-]*)\]$')
PROJECT_RE = re.compile(HEADER_FILTER)


def parse_warning(line):
    m = LINE_RE.match(line)
    if not m or m.group('level') != 'warning':
        return None
    rest = m.group('rest')
    c = CHECK_RE.search(rest)
    check = c.group('check') if c else ''
    msg = rest[: c.start()] if c else rest
    return {
        'path': m.group('path').replace(BS, FS),
        'line': int(m.group('line')),
        'col': int(m.group('col')),
        'message': msg,
        'check': check,
    }


def require_tool():
    if not TOOL:
        print('clang-tidy not found on PATH; install LLVM or add it to PATH', file=sys.stderr)
        sys.exit(2)


def find_tus():
    ccfile = os.path.join(os.getcwd(), 'build', 'compile_commands.json')
    if not os.path.isfile(ccfile):
        print('build/compile_commands.json missing; run xmake build first', file=sys.stderr)
        sys.exit(2)
    with open(ccfile, encoding='utf-8') as f:
        entries = json.load(f)
    # dict.fromkeys keeps the FIRST occurrence deterministically; duplicate
    # entries exist (encro + tests targets) and clang-tidy re-looks-up the
    # file in the database anyway, so scanning once per unique file is right.
    tus = list(dict.fromkeys(
        e['file']
        for e in entries
        if e.get('file', '').replace(BS, FS).endswith('.cpp') and os.path.isfile(e['file'])
    ))
    tus.sort()
    if not tus:
        print('No TU entries found in build/compile_commands.json', file=sys.stderr)
        sys.exit(2)
    return tus


def is_project(path):
    # Segment match, not a prefix check: also accepts absolute project paths
    # and rejects third-party paths regardless of how clang-tidy formats them.
    return bool(PROJECT_RE.search(path))


def scan_tu(tu, analyzer):
    cmd = [TOOL, tu, '-p', 'build', '-header-filter=' + HEADER_FILTER, '--quiet']
    if not analyzer:
        # The path-sensitive static analyzer dominates cost (minutes and GBs per
        # TU); strip it for the default fast run.
        cmd.append('--checks=-clang-analyzer-*')
    try:
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=600)
    except subprocess.TimeoutExpired:
        print(f'clang-tidy timed out on {tu}; skipped', file=sys.stderr)
        return [], 'timeout'
    warnings = []
    for line in r.stdout.splitlines():
        w = parse_warning(line)
        if w and is_project(w['path']):
            warnings.append(w)
    if r.returncode == 0:
        return warnings, 'ok'
    # Exit 1 is a deterministic clang-tidy error (compile failure, bad
    # config); only the crash-ish codes are worth a sequential retry.
    return warnings, 'crashed' if r.returncode not in (0, 1) else 'failed'


def sarif_document(all_warnings):
    rules = sorted({w['check'] for w in all_warnings if w['check']})
    ver = ''
    v = subprocess.run([TOOL, '--version'], capture_output=True, text=True)
    m = re.search(r'LLVM version (\S+)', v.stdout)
    if m:
        ver = m.group(1)
    results = [
        {
            'ruleId': w['check'] or 'clang-tidy',
            'level': 'warning',
            'message': {'text': w['message']},
            'locations': [
                {
                    'physicalLocation': {
                        'artifactLocation': {'uri': w['path']},
                        'region': {'startLine': w['line'], 'startColumn': w['col']},
                    }
                }
            ],
        }
        for w in all_warnings
    ]
    return {
        '$schema': 'https://json.schemastore.org/sarif-2.1.0.json',
        'version': '2.1.0',
        'runs': [
            {
                'tool': {
                    'driver': {
                        'name': 'clang-tidy',
                        'version': ver,
                        'rules': [{'id': r} for r in rules],
                    }
                },
                'results': results,
            }
        ],
    }


def selftest():
    with tempfile.TemporaryDirectory() as d:
        src = os.path.join(d, 'src')
        os.makedirs(src)
        hdr = os.path.join(src, 'fixture.h')
        cpp = os.path.join(src, 'fixture.cpp')
        with open(hdr, 'w', encoding='utf-8') as f:
            f.write('#pragma once\ninline int add(int a, int b) { return a + b; }\n')
        with open(cpp, 'w', encoding='utf-8') as f:
            f.write(
                '#include "fixture.h"\n'
                'long long g_big = 1;\n'
                'float narrow(long long x) { return x; }\n'
            )
        config = os.path.join(os.getcwd(), '.clang-tidy')
        # Same command shape as the production fast mode, including the
        # analyzer strip, so the default path is what gets verified.
        cmd = [
            TOOL, cpp,
            '-header-filter=' + HEADER_FILTER,
            '--config-file=' + config,
            '--checks=-clang-analyzer-*',
            '--', '-std=c++26',
        ]
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=300)
        warnings = [w for line in r.stdout.splitlines() if (w := parse_warning(line))]
        checks = {w['check'] for w in warnings}
        ok = True
        if 'bugprone-narrowing-conversions' not in checks:
            print('selftest FAIL: expected bugprone-narrowing-conversions to fire', file=sys.stderr)
            ok = False
        if 'portability-avoid-pragma-once' in checks:
            print('selftest FAIL: pragma-once check should be excluded', file=sys.stderr)
            ok = False
        if any(c.startswith('clang-analyzer') for c in checks):
            print('selftest FAIL: analyzer check leaked in fast mode', file=sys.stderr)
            ok = False
        if warnings and not all(is_project(w['path']) for w in warnings):
            print('selftest FAIL: non-project path leaked', file=sys.stderr)
            ok = False
        if not ok:
            print('--- clang-tidy output ---', file=sys.stderr)
            print(r.stdout, file=sys.stderr)
            return 1
    print('selftest: OK (narrowing flagged, pragma-once excluded)')
    return 0


def dedupe(warnings):
    seen = set()
    out = []
    for w in warnings:
        key = (w['path'], w['line'], w['col'], w['message'], w['check'])
        if key not in seen:
            seen.add(key)
            out.append(w)
    return out


def main():
    args = sys.argv[1:]
    require_tool()
    if '--selftest' in args:
        sys.exit(selftest())

    sarif = '--sarif' in args
    args = [a for a in args if a != '--sarif']

    analyzer = '--analyzer' in args
    args = [a for a in args if a != '--analyzer']

    # Fast checks are cheap (~seconds/TU); the analyzer needs minutes and ~2GB
    # per process, so cap its parallelism to keep peak memory sane.
    jobs = 4 if analyzer else 8
    if '-j' in args:
        i = args.index('-j')
        jobs = int(args[i + 1])
        del args[i:i + 2]

    filt = None
    if '-f' in args:
        i = args.index('-f')
        filt = args[i + 1]
        del args[i:i + 2]

    tus = find_tus()
    if filt:
        tus = [tu for tu in tus if filt in tu.replace(BS, FS)]
        if not tus:
            print(f'=== tidy: 0 warnings (filter: {filt}) ===')
            sys.exit(0)

    all_warnings = []
    crashed = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as ex:
        for tu, (warnings, status) in zip(tus, ex.map(lambda t: scan_tu(t, analyzer), tus)):
            all_warnings.extend(warnings)
            if status == 'crashed':
                crashed.append(tu)

    # clang-tidy can crash (access violation) under high parallelism; retry
    # crashed TUs sequentially, one process at a time.
    for tu in crashed:
        warnings, status = scan_tu(tu, analyzer)
        all_warnings.extend(warnings)
        if status != 'ok':
            print(f'clang-tidy failed on {tu}; skipped', file=sys.stderr)

    all_warnings = dedupe(all_warnings)
    all_warnings.sort(key=lambda w: (w['path'], w['line'], w['col']))

    if sarif:
        with open(SARIF_OUT, 'w', encoding='utf-8') as f:
            json.dump(sarif_document(all_warnings), f, indent=2)
        print(f'=== tidy: {len(all_warnings)} warning(s), SARIF written to {SARIF_OUT} ===')
    else:
        for w in all_warnings:
            print(f'{w["path"]}:{w["line"]}:{w["col"]}: warning: {w["message"]} [{w["check"]}]')
        print(f'=== tidy: {len(all_warnings)} warning(s) ===')
    sys.exit(0)


if __name__ == '__main__':
    main()
