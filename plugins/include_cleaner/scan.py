"""Parallel clang-include-cleaner scan over all TUs in compile_commands.json.

Invoked by the xmake `include-cleaner` plugin task. Prints every unused
include ("- header @Line:N") prefixed by the TU path, then a summary line.
Exit code 1 if --check and any unused include was found.
"""
import concurrent.futures
import json
import os
import shutil
import subprocess
import sys

BS = chr(92)
TOOL = shutil.which('clang-include-cleaner')
if not TOOL:
    print('clang-include-cleaner not found on PATH; install LLVM or add it to PATH', file=sys.stderr)
    sys.exit(2)


def main():
    check = '--check' in sys.argv
    args = [a for a in sys.argv[1:] if a != '--check']
    jobs = 16
    if '-j' in args:
        i = args.index('-j')
        jobs = int(args[i + 1])
        del args[i:i + 2]
    filt = None
    if '-f' in args:
        i = args.index('-f')
        filt = args[i + 1]
        del args[i:i + 2]

    ccfile = os.path.join(os.getcwd(), 'build', 'compile_commands.json')
    if not os.path.isfile(ccfile):
        print('build/compile_commands.json missing; run xmake build first', file=sys.stderr)
        sys.exit(2)

    with open(ccfile, encoding='utf-8') as f:
        entries = json.load(f)
    tus = sorted(e['file'] for e in entries
                 if e.get('file', '').replace(BS, '/').endswith('.cpp')
                 and os.path.isfile(e['file']))
    if not tus:
        print('No TU entries found in build/compile_commands.json', file=sys.stderr)
        sys.exit(2)

    # Filter mode: cheap pre-filter — only scan TUs that mention the header
    # (as an include path) somewhere, so `-f job_state` scans a handful of files.
    if filt:
        tus = [tu for tu in tus
               if filt in open(tu, encoding='utf-8', errors='replace').read()]
        if not tus:
            print(f'=== unused includes: 0 (filter: {filt}) ===')
            sys.exit(0)

    def scan(tu):
        r = subprocess.run([TOOL, '--print=changes', '-p',
                            os.path.join(os.getcwd(), 'build'), tu],
                           capture_output=True, text=True, timeout=300)
        if r.returncode != 0:
            return []
        return [(tu.replace(BS, '/'), line.strip())
                for line in r.stdout.splitlines()
                if line.startswith('-') and (not filt or filt in line)]

    hits = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as ex:
        for res in ex.map(scan, tus):
            hits.extend(res)

    hits = sorted(set(hits))
    for tu, line in hits:
        print(f'{tu} :: {line}')
    print(f'=== unused includes: {len(hits)}'
          + (f' (filter: {filt})' if filt else '') + ' ===')
    sys.exit(1 if check and hits else 0)


if __name__ == '__main__':
    main()
