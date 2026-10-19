#!/usr/bin/env python3
"""Verify the release tag line against its own contract.

Five checks, each a way this has gone wrong or would go wrong silently:

  1. tag names match the semver grammar, and the grammar really rejects the
     forms it is meant to reject (a regex nobody attacks accepts everything);
  2. precedence order equals commit order — two independent orderings, not one
     sort checked with the comparator that produced it;
  3. every tag's tree carries its own version's section, newest first under
     [Unreleased];
  4. the working-tree CHANGELOG covers every tag and every section has a tag,
     with dates that never increase and links that resolve;
  5. change types are the six Keep a Changelog names, and no group is empty.

Reads the tags from git, so it needs no fixture. Exits 1 listing every failure.

    python3 scripts/verify_release_tags.py
"""

from __future__ import annotations

import datetime
import subprocess
import sys
from functools import cmp_to_key
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import changelog
import semver


def git(*args):
    """Run git and return stdout, raising when it fails."""
    result = subprocess.run(('git',) + args, capture_output=True, text=True, encoding='utf-8')
    if result.returncode != 0:
        raise RuntimeError('git %s failed: %s' % (' '.join(args), result.stderr.strip()))
    return result.stdout


def git_text(*args):
    """Run git and return stdout, or None when it fails."""
    result = subprocess.run(('git',) + args, capture_output=True, text=True, encoding='utf-8')
    return result.stdout if result.returncode == 0 else None


def main():
    failures = []

    def fail(message):
        failures.append(message)

    tags = sorted(git('tag', '-l').split())
    if not tags:
        print('no tags found')
        return 1

    # --- 1. tag names conform, and the grammar rejects what it should --------
    failures.extend(semver.selftest())
    for tag in tags:
        if not semver.is_tag(tag):
            fail('tag name does not conform to semver: %s' % tag)
        kind = git_text('cat-file', '-t', tag)
        if kind is None or kind.strip() != 'tag':
            fail('tag is not annotated: %s' % tag)

    # --- 2. precedence order equals commit order ----------------------------
    valid = [tag for tag in tags if semver.is_tag(tag)]
    # Stable: tags differing only in build metadata keep git's order, the same
    # way the old PowerShell version used Sort-Object -Stable.
    by_semver = sorted(valid, key=cmp_to_key(lambda a, b: semver.compare_text(a[1:], b[1:])))

    revisions = git('rev-list', 'main', '--first-parent').split()
    position = {revision: index for index, revision in enumerate(revisions)}
    unknown = len(revisions) + 1

    def history_rank(tag):
        commit = (git_text('rev-parse', '%s^{commit}' % tag) or '').strip()
        return position.get(commit, unknown)

    by_history = sorted(tags, key=history_rank, reverse=True)

    if by_history != by_semver:
        fail('precedence order does not match commit order - history: [%s] - semver: [%s]'
             % (', '.join(by_history), ', '.join(by_semver)))
    for index in range(1, len(by_semver)):
        if semver.compare_text(by_semver[index - 1][1:], by_semver[index][1:]) >= 0:
            fail('two tags share a version: %s and %s' % (by_semver[index - 1], by_semver[index]))

    # --- 3. every tag's tree carries its own newest CHANGELOG section -------
    for tag in tags:
        version = tag[1:]
        content = git_text('show', '%s:CHANGELOG.md' % tag)
        if content is None:
            fail('no CHANGELOG.md in the tree of %s' % tag)
            continue
        found = [name for name, _ in changelog.headings(content.split('\n'))]
        if not found:
            fail('no CHANGELOG.md in the tree of %s' % tag)
            continue
        if found[0] != 'Unreleased':
            fail("%s : first CHANGELOG heading is '%s', expected Unreleased" % (tag, found[0]))
        if len(found) < 2:
            fail('%s : CHANGELOG has no released section' % tag)
            continue
        if found[1] != version:
            fail("%s : newest released section is '%s', expected '%s'" % (tag, found[1], version))

    # --- 4. CHANGELOG covers every tag, newest first, with working links ----
    lines = changelog.read_lines(changelog.default_path())
    released = []
    previous_date = None
    for version, date in changelog.headings(lines):
        if version == 'Unreleased':
            continue
        released.append(version)
        if date is None:
            fail('CHANGELOG section has no ISO date: %s' % version)
            continue
        stamp = datetime.date(int(date[:4]), int(date[5:7]), int(date[8:10]))
        if previous_date is not None and stamp > previous_date:
            fail('CHANGELOG dates do not descend at %s' % version)
        previous_date = stamp

    tag_versions = [tag[1:] for tag in tags]
    for version in tag_versions:
        if version not in released:
            fail('tag exists without a CHANGELOG section: %s' % version)
    for version in released:
        if version not in tag_versions:
            fail('CHANGELOG section exists without a tag: %s' % version)

    links = dict(changelog.links(lines))
    if 'unreleased' not in links:
        fail('link block has no [unreleased] entry')
    lowest = released[-1] if released else None
    for version in released:
        url = links.get(version)
        if url is None:
            fail('no link for %s' % version)
            continue
        if version == lowest:
            if '/releases/tag/v' not in url:
                fail('the lowest version should link to its release page, not a compare: %s' % version)
        elif '/compare/v' not in url or '...v' not in url:
            fail('link is not a compare URL: %s' % version)

    # --- 5. change types are legal and no group is empty --------------------
    current_type = None
    current_type_has_entry = False

    def close_group(where=''):
        if current_type is not None and not current_type_has_entry:
            fail("empty group '### %s'%s" % (current_type, where))

    for line in lines:
        type_match = changelog.CHANGE_TYPE.match(line)
        if type_match is not None:
            close_group()
            name = type_match.group(1).strip()
            if name not in changelog.CHANGE_TYPES:
                fail("unknown change type '%s' (allowed: %s)" % (name, ', '.join(changelog.CHANGE_TYPES)))
            current_type = name
            current_type_has_entry = False
            continue
        heading_match = changelog.HEADING.match(line)
        if heading_match is not None:
            close_group(' in section %s' % heading_match.group(1))
            current_type = None
            current_type_has_entry = False
            continue
        if current_type is not None and line.startswith('- '):
            current_type_has_entry = True
    close_group(' at end of file')

    # --- report -------------------------------------------------------------
    if not failures:
        print('OK: %d tags verified (name grammar, annotated, precedence vs commit order, '
              'per-tag CHANGELOG section, %d CHANGELOG sections, %d links, legal change types)'
              % (len(tags), len(released), len(links)))
        return 0

    print('FAILURES (%d):' % len(failures))
    for message in failures:
        print('  - %s' % message)
    return 1


if __name__ == '__main__':
    sys.exit(main())
