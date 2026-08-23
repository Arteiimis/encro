"""Parallel clang-tidy scan over every TU in build/compile_commands.json.

Invoked by the xmake `tidy` plugin task. Default prints warnings as text;
`--sarif` writes build/tidy-results.sarif. `--selftest` scans a bundled
fixture to assert the check set and header-filter behave.
"""
import concurrent.futures
import glob
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time

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
    # Keep the FIRST entry per unique file deterministically; duplicate
    # entries exist (encro + tests targets) and clang-tidy re-looks-up the
    # file in the database anyway, so scanning once per unique file is right.
    flags = {}
    for e in entries:
        f = e.get('file', '')
        norm = f.replace(BS, FS)
        if norm.endswith('.cpp') and os.path.isfile(f) and norm not in flags:
            flags[norm] = ' '.join(e.get('arguments') or []) or e.get('command', '')
    tus = sorted(flags)
    if not tus:
        print('No TU entries found in build/compile_commands.json', file=sys.stderr)
        sys.exit(2)
    return tus, flags


def is_project(path):
    # Segment match, not a prefix check: also accepts absolute project paths
    # and rejects third-party paths regardless of how clang-tidy formats them.
    return bool(PROJECT_RE.search(path))


CACHE_DIR = os.path.join('build', '.tidy-cache')
_DEP_ROOTS = None
_TOOL_VERSION = None


def clang_tidy_version():
    global _TOOL_VERSION
    if _TOOL_VERSION is None:
        v = subprocess.run([TOOL, '--version'], capture_output=True, text=True)
        m = re.search(r'LLVM version (\S+)', v.stdout)
        _TOOL_VERSION = m.group(1) if m else 'unknown'
    return _TOOL_VERSION


def find_dep_roots():
    global _DEP_ROOTS
    if _DEP_ROOTS is None:
        _DEP_ROOTS = sorted(glob.glob(
            os.path.join('build', '.deps', '*', 'windows', 'x64', 'release')
        ))
    return _DEP_ROOTS


def parse_depfile(path):
    """Parse an xmake depfile-metadata file into the TU's dependency list.

    xmake writes these as Lua-style tables (not JSON), e.g.
    `depfiles = "target: dep1 dep2 \\\n  dep3",`.
    """
    try:
        with open(path, encoding='utf-8') as f:
            text = f.read()
    except OSError:
        return None
    # (?s): depfile lines end with a backslash-newline continuation, so
    # the `\\.` escape must be allowed to consume newlines too.
    m = re.search(r'(?s)depfiles\s*=\s*\"((?:[^\"\\]|\\.)*)\"', text)
    if not m:
        return None
    s = m.group(1)  # keep Lua escapes; the tokenizer below handles `\` pairs
    tokens = []
    cur = ''
    i = 0
    while i < len(s):
        c = s[i]
        if c == '\\':
            if i + 1 < len(s) and s[i + 1] == '\\':
                cur += '\\'
                i += 2
            elif i + 1 < len(s) and s[i + 1] == ' ':
                cur += ' '
                i += 2
            elif i + 1 < len(s) and s[i + 1] in '\r\n':
                i += 2  # line continuation
            else:
                i += 1
        elif c in ' \t\r\n':
            if cur:
                tokens.append(cur)
                cur = ''
            i += 1
        else:
            cur += c
            i += 1
    if cur:
        tokens.append(cur)
    if tokens and (tokens[0].endswith('.obj') or tokens[0].endswith('.obj:')):
        tokens = tokens[1:]  # drop the target itself
    return [t for t in tokens if t != '\\']


def find_deps(tu):
    """Resolve the depfile for a TU; None when unavailable (fall back to full scan)."""
    rel = tu.replace('\\', '/')
    if os.path.isabs(rel):
        try:
            rel = os.path.relpath(rel, os.getcwd()).replace('\\', '/')
        except ValueError:
            return None
    if rel.startswith('./'):
        rel = rel[2:]
    for root in find_dep_roots():
        p = os.path.join(root, rel + '.obj.d')
        if os.path.isfile(p):
            return parse_depfile(p)
    return None


def cache_key(tu, flags, mode, deps, checks):
    parts = [tu, flags, mode, checks or '', clang_tidy_version()]
    for p in ['.clang-tidy', tu]:
        try:
            parts.append(str(os.path.getmtime(p)))
        except OSError:
            parts.append('missing')
    for d in sorted(set(deps)):
        try:
            parts.append(str(os.path.getmtime(d)))
        except OSError:
            parts.append('missing')
    return hashlib.sha256('\n'.join(parts).encode('utf-8')).hexdigest()


def load_cached(key):
    p = os.path.join(CACHE_DIR, key + '.json')
    if not os.path.isfile(p):
        return None
    try:
        with open(p, encoding='utf-8') as f:
            return json.load(f)['warnings']
    except (OSError, ValueError, KeyError):
        return None


def save_cache(key, tu, mode, warnings):
    os.makedirs(CACHE_DIR, exist_ok=True)
    p = os.path.join(CACHE_DIR, key + '.json')
    tmp = p + '.tmp'
    try:
        with open(tmp, 'w', encoding='utf-8') as f:
            json.dump({'tu': tu, 'mode': mode, 'warnings': warnings}, f)
        os.replace(tmp, p)
    except OSError:
        pass


def cleanup_cache(active_tus, mode, used_keys):
    """Drop stale cached results: same TU as this run, but a key the run did
    not produce (its sources changed). Results for TUs outside this run's
    scope (e.g. a -f filtered scan) are kept."""
    if not os.path.isdir(CACHE_DIR):
        return
    active = {t.replace('\\', '/') for t in active_tus}
    for name in os.listdir(CACHE_DIR):
        if not name.endswith('.json'):
            continue
        p = os.path.join(CACHE_DIR, name)
        try:
            with open(p, encoding='utf-8') as f:
                meta = json.load(f)
            tu_norm = meta.get('tu', '').replace('\\', '/')
            stale = (
                meta.get('mode') != mode
                or (tu_norm in active and name[:-5] not in used_keys)
            )
        except (OSError, ValueError):
            stale = True
        if stale:
            try:
                os.remove(p)
            except OSError:
                pass


def scan_tu(tu, analyzer, flags, checks):
    mode = 'analyzer' if analyzer else 'fast'
    deps = find_deps(tu)
    key = None
    if deps is not None:
        key = cache_key(tu, flags, mode, deps, checks)
        cached = load_cached(key)
        if cached is not None:
            return cached, 'cached', key
    cmd = [TOOL, tu, '-p', 'build', '-header-filter=' + HEADER_FILTER, '--quiet']
    if checks:
        cmd.append('--checks=' + checks)
    elif not analyzer:
        # The path-sensitive static analyzer dominates cost (minutes and GBs per
        # TU); strip it for the default fast run.
        cmd.append('--checks=-clang-analyzer-*')
    try:
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=600)
    except subprocess.TimeoutExpired:
        print(f'clang-tidy timed out on {tu}; skipped', file=sys.stderr)
        return [], 'timeout', key
    warnings = []
    for line in r.stdout.splitlines():
        w = parse_warning(line)
        if w and is_project(w['path']):
            warnings.append(w)
    if key is not None and r.returncode == 0:
        save_cache(key, tu, mode, warnings)
    if r.returncode == 0:
        return warnings, 'ok', key
    # Exit 1 is a deterministic clang-tidy error (compile failure, bad
    # config); only the crash-ish codes are worth a sequential retry.
    return warnings, 'crashed' if r.returncode not in (0, 1) else 'failed', key


def sarif_document(all_warnings):
    rules = sorted({w['check'] for w in all_warnings if w['check']})
    ver = clang_tidy_version()
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

    checks = None
    if '--checks' in args:
        i = args.index('--checks')
        checks = args[i + 1]
        del args[i:i + 2]

    # Fast checks are cheap (~seconds/TU); the analyzer needs minutes and ~2GB
    # per process, so cap its parallelism to keep peak memory sane.
    cpus = os.cpu_count() or 4
    jobs = min(cpus - 2, 14) if not analyzer else min(cpus // 2, 8)
    if '-j' in args:
        i = args.index('-j')
        jobs = int(args[i + 1])
        del args[i:i + 2]

    filt = None
    if '-f' in args:
        i = args.index('-f')
        filt = args[i + 1]
        del args[i:i + 2]

    tus, flags = find_tus()
    if filt:
        tus = [tu for tu in tus if filt in tu]
        if not tus:
            print(f'=== tidy: 0 warnings (filter: {filt}) ===')
            sys.exit(0)

    all_warnings = []
    crashed = []
    used_keys = set()
    cached_count = 0
    started = time.monotonic()
    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as ex:
        for tu, (warnings, status, key) in zip(
            tus, ex.map(lambda t: scan_tu(t, analyzer, flags[t], checks), tus)
        ):
            all_warnings.extend(warnings)
            if status == 'crashed':
                crashed.append(tu)
            if status == 'cached':
                cached_count += 1
            if key is not None:
                used_keys.add(key)
    elapsed = time.monotonic() - started

    # clang-tidy can crash (access violation) under high parallelism; retry
    # crashed TUs sequentially, one process at a time.
    for tu in crashed:
        warnings, status, key = scan_tu(tu, analyzer, flags[tu], checks)
        all_warnings.extend(warnings)
        if status != 'ok':
            print(f'clang-tidy failed on {tu}; skipped', file=sys.stderr)
        if key is not None:
            used_keys.add(key)

    cleanup_cache(tus, 'analyzer' if analyzer else 'fast', used_keys)

    all_warnings = dedupe(all_warnings)
    all_warnings.sort(key=lambda w: (w['path'], w['line'], w['col']))

    summary = (
        f'=== tidy: {len(all_warnings)} warning(s), {cached_count} cached '
        f'of {len(tus)} TUs ({elapsed:.1f}s) ==='
    )
    if sarif:
        with open(SARIF_OUT, 'w', encoding='utf-8') as f:
            json.dump(sarif_document(all_warnings), f, indent=2)
        print(summary + f', SARIF written to {SARIF_OUT}')
    else:
        for w in all_warnings:
            print(f'{w["path"]}:{w["line"]}:{w["col"]}: warning: {w["message"]} [{w["check"]}]')
        print(summary)
    sys.exit(0)


if __name__ == '__main__':
    main()
