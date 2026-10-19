"""CHANGELOG.md section extraction and shape, shared by the release scripts.

The extractor feeds GitHub release bodies, so a section must start at its own
`## [<version>]` heading and stop before whatever comes next:

  * the next `## [` heading, or
  * the link-definition block (`[1.2.3]: ...`), which follows the OLDEST
    section instead of another heading.

The second terminator is not optional. Without it, the oldest version's body
would run on through every link line and swallow the names of every other
version with them.

    python3 scripts/changelog.py section <version> [--path CHANGELOG.md]

Prints the section and exits 0, or prints why and exits 1 — a release can never
be published with an empty or invented body.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

HEADING = re.compile(r'^## \[(.+?)\](?: - (\d{4}-\d{2}-\d{2}))?')
LINK = re.compile(r'^\[([^\]]+)\]: (\S+)$')
CHANGE_TYPE = re.compile(r'^### (.+)$')

CHANGE_TYPES = ('Added', 'Changed', 'Deprecated', 'Removed', 'Fixed', 'Security')


def repo_root():
    return Path(__file__).resolve().parent.parent


def default_path():
    return repo_root() / 'CHANGELOG.md'


def read_lines(path):
    """Read as UTF-8 with newlines normalised away, so line contents compare."""
    text = Path(path).read_text(encoding='utf-8')
    return text.replace('\r\n', '\n').replace('\r', '\n').split('\n')


def headings(lines):
    """Every `## [...]` heading in file order, as (version, date-or-None)."""
    found = []
    for line in lines:
        match = HEADING.match(line)
        if match is not None:
            found.append((match.group(1), match.group(2)))
    return found


def links(lines):
    """Every link-definition line, as (name, url)."""
    found = []
    for line in lines:
        match = LINK.match(line)
        if match is not None:
            found.append((match.group(1), match.group(2)))
    return found


def section(lines, version):
    """The lines of one version's section, or None when there is no such section."""
    wanted = version[1:] if version[:1] in ('v', 'V') else version
    start = None
    for index, line in enumerate(lines):
        match = HEADING.match(line)
        if match is not None and match.group(1) == wanted:
            start = index
            break
    if start is None:
        return None

    end = len(lines)
    for index in range(start + 1, len(lines)):
        line = lines[index]
        if line.startswith('## [') or LINK.match(line) is not None:
            end = index
            break

    last = end - 1
    while last > start and not lines[last].strip():
        last -= 1
    return lines[start:last + 1]


def main(argv):
    parser = argparse.ArgumentParser(description='Print one CHANGELOG section.')
    parser.add_argument('command', choices=('section',))
    parser.add_argument('version')
    parser.add_argument('--path', default=None)
    args = parser.parse_args(argv[1:])

    path = Path(args.path) if args.path else default_path()
    if not path.is_file():
        print('no CHANGELOG at %s' % path, file=sys.stderr)
        return 1

    body = section(read_lines(path), args.version)
    if body is None:
        wanted = args.version[1:] if args.version[:1] in ('v', 'V') else args.version
        print("no changelog section for version '%s' in %s" % (wanted, path), file=sys.stderr)
        return 1

    print('\n'.join(body))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
