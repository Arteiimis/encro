#!/usr/bin/env python3
"""Record a release entry for every tag at or below a boundary, with no artifacts.

The boundary (`--until`) is what makes this safe to re-run: versions above it are
never touched, so a version whose publish attempt failed stays visible as a
missing entry instead of being recorded as "never distributed".

Ordering is SemVer, not version sort: version sort puts 2.0.0 before
2.0.0-alpha.1 while semver puts every pre-release BELOW its release, which would
silently skip the pre-release tags this history contains.

    python3 scripts/record_release_history.py --until 2.0.0 --what-if-only
    python3 scripts/record_release_history.py --until 2.0.0

Requires an authenticated `gh` (GH_TOKEN in CI).
"""

from __future__ import annotations

import argparse
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import changelog
import semver

NEVER_DISTRIBUTED = [
    '> **This version was never distributed.** encro began publishing',
    '> downloadable binaries at a later version. This entry records the',
    "> project's history and carries nothing to install.",
    '',
]


def has_release(tag):
    result = subprocess.run(('gh', 'release', 'view', tag, '--json', 'tagName'),
                            capture_output=True, text=True, encoding='utf-8')
    return result.returncode == 0


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--until', required=True,
                        help='highest version to record; versions above it are never touched')
    parser.add_argument('--what-if-only', action='store_true',
                        help='print what would happen without creating anything')
    args = parser.parse_args(argv[1:])

    root = changelog.repo_root()
    boundary_text = args.until.lstrip('vV')
    try:
        boundary = semver.parse(boundary_text)
    except Exception:
        boundary = None
    if boundary is None:
        print('error: --until %r is not a version' % args.until, file=sys.stderr)
        return 2

    result = subprocess.run(('git', 'tag', '-l'), cwd=root, capture_output=True, text=True,
                            encoding='utf-8')
    tags = sorted(tag for tag in result.stdout.split() if tag.startswith('v'))

    lines = changelog.read_lines(changelog.default_path())
    created = kept = considered = 0
    above = []

    for tag in tags:
        version = tag[1:]
        parsed = semver.parse(version)
        if parsed is None:
            print('  skip %s (not a version)' % tag)
            continue
        if semver.compare(parsed, boundary) > 0:
            above.append(tag)
            continue
        considered += 1

        if has_release(tag):
            kept += 1
            print('  keep  %s (already has an entry)' % tag)
            continue

        print('  create %s' % tag)
        if not args.what_if_only:
            body = changelog.section(lines, version)
            if body is None:
                print('no changelog section for %s' % version, file=sys.stderr)
                return 1
            with tempfile.NamedTemporaryFile('w', suffix='.md', delete=False,
                                             encoding='utf-8', newline='\n') as handle:
                handle.write('\n'.join(NEVER_DISTRIBUTED + body) + '\n')
                notes = handle.name
            try:
                made = subprocess.run(('gh', 'release', 'create', tag, '--title', tag,
                                       '--notes-file', notes),
                                      capture_output=True, text=True, encoding='utf-8')
                if made.returncode != 0:
                    print('gh release create failed for %s: %s' % (tag, made.stderr.strip()),
                          file=sys.stderr)
                    return 1
            finally:
                Path(notes).unlink(missing_ok=True)
        created += 1

    print('')
    print('boundary %s : considered %d tags, created %d, already present %d'
          % (args.until, considered, created, kept))
    print('tags above the boundary left alone: %d (%s)' % (len(above), ', '.join(above)))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
