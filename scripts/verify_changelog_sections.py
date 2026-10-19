#!/usr/bin/env python3
"""Check that every release tag has a changelog section fit to publish.

The section text becomes a GitHub release body, so this asserts the shape that
makes it publishable rather than trusting the extractor:

  * every `v<version>` tag has a section;
  * the extracted body starts with that version's own heading;
  * it contains no other version's heading and no link-definition line;
  * the [Unreleased] section is NOT extractable, because no tag names it.

A tag that is not a version (no `v<digit>`) is not a release and is skipped.

    python3 scripts/verify_changelog_sections.py
"""

from __future__ import annotations

import subprocess
import sys
from contextlib import redirect_stderr
from io import StringIO
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import changelog


def main():
    root = changelog.repo_root()
    result = subprocess.run(('git', 'tag', '-l'), cwd=root, capture_output=True, text=True,
                            encoding='utf-8')
    if result.returncode != 0:
        print('git tag failed: %s' % result.stderr.strip(), file=sys.stderr)
        return 1

    tags = sorted(tag for tag in result.stdout.split() if tag[:2].startswith('v') and tag[1:2].isdigit())
    print('tags: %d' % len(tags))

    lines = changelog.read_lines(changelog.default_path())
    failures = 0

    for tag in tags:
        version = tag[1:]
        body = changelog.section(lines, version)
        if body is None:
            print('  FAIL %s : no section' % version)
            failures += 1
            continue

        problems = []
        if not body[0].startswith('## [%s]' % version):
            problems.append("first line is '%s'" % body[0])
        others = [line for line in body[1:] if line.startswith('## [')]
        if others:
            problems.append('body contains %d other version heading(s): %s'
                            % (len(others), ' | '.join(others)))
        link_lines = [line for line in body if changelog.LINK.match(line) is not None]
        if link_lines:
            problems.append('body contains %d link-definition line(s)' % len(link_lines))

        if problems:
            for problem in problems:
                print('  FAIL %s : %s' % (version, problem))
            failures += 1
        else:
            print('  ok   %s  (%d lines)' % (version, len(body)))
            if version == '0.1.0':
                print('       ^ the oldest section, whose terminator is the link block '
                      'rather than another heading')

    print('')
    # The gate calls the extractor's CLI in CI, so the CLI's exit code is what
    # this asserts — not the library's return value.
    with redirect_stderr(StringIO()):
        probe = changelog.main(['changelog.py', 'section', 'unreleased'])
    if probe == 0:
        print('  FAIL the unreleased section was extractable')
        failures += 1
    else:
        print('  ok   the unreleased section is not extractable (the extractor exits %d)' % probe)

    print('')
    if failures == 0:
        print('ALL SECTIONS OK (%d versions)' % len(tags))
        return 0
    print('FAILURES: %d' % failures)
    return 1


if __name__ == '__main__':
    sys.exit(main())
