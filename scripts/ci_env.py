#!/usr/bin/env python3
"""The two bits of shell every release job needs, in one portable place.

They were three bash twins and one PowerShell step before this: the same
`XMAKE_GLOBALDIR` pin written three ways, plus a cache-size assertion that only
the Windows job had. Same reasoning as the rest of `scripts/` — one
implementation, no shell dialect to keep in sync.

  pin-xmake-globaldir     name the parent of the directory xmake appends `.xmake`
                          to, so the cached `~/.xmake/packages` is the path xmake
                          actually uses. Without it, Windows prefers
                          `%APPDATA%\\.xmake`, `~/.xmake/packages` stays empty and
                          the cache silently stores nothing.
  report-package-cache    print the package tree's size, and fail when the path
                          does not exist — a silently empty cache is how the
                          first attempt failed twice while reporting success.

    python3 scripts/ci_env.py pin-xmake-globaldir
    python3 scripts/ci_env.py report-package-cache
"""

from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path


def home():
    """The user's home directory, in the variable the platform actually sets."""
    return os.environ.get('USERPROFILE') or os.environ.get('HOME') or str(Path.home())


def packages_dir():
    # xmake appends `.xmake` to XMAKE_GLOBALDIR when it is set, so that is the
    # directory to look in — reporting the home-based path while a pin is in
    # effect would check a tree xmake never touches.
    root = os.environ.get('XMAKE_GLOBALDIR') or home()
    return Path(root) / '.xmake' / 'packages'


def pin_xmake_globaldir():
    target = os.environ.get('GITHUB_ENV')
    if not target:
        print('GITHUB_ENV is not set; run this inside a workflow step', file=sys.stderr)
        return 1
    value = home()
    with open(target, 'a', encoding='utf-8', newline='\n') as handle:
        handle.write('XMAKE_GLOBALDIR=%s\n' % value)
    print('XMAKE_GLOBALDIR=%s (packages will be %s)' % (value, packages_dir()))
    return 0


def report_package_cache():
    path = packages_dir()
    if not path.is_dir():
        print('packages dir does not exist: %s' % path, file=sys.stderr)
        return 1
    total = 0
    for root, _, files in os.walk(path):
        for name in files:
            try:
                total += os.path.getsize(os.path.join(root, name))
            except OSError:
                continue
    print('packages: {:,} bytes'.format(total))
    return 0


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('command', choices=('pin-xmake-globaldir', 'report-package-cache'))
    args = parser.parse_args(argv[1:])

    if args.command == 'pin-xmake-globaldir':
        return pin_xmake_globaldir()
    return report_package_cache()


if __name__ == '__main__':
    sys.exit(main(sys.argv))
