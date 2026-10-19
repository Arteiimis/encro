"""SemVer 2.0.0 grammar and precedence for encro's tag line.

Stdlib only, on purpose: the release jobs run on ubuntu and windows runners and
on developer machines, and a pip dependency would be one more thing that can be
missing exactly where a version is being compared.

Two separate things live here, because conflating them is what broke this
before:

  * `tag_grammar` decides whether a tag NAME is a legal version. It is strict:
    three numeric identifiers without leading zeros, an optional dot-separated
    pre-release, an optional build-metadata suffix.
  * `compare` decides which of two versions comes first. SemVer section 11, and
    build metadata takes no part in it.

Neither is `sort -V` or `git tag --sort=v:refname`: both rank a release BELOW
its own pre-releases (2.0.0-alpha.1 after 2.0.0), which silently mis-orders any
tag list containing pre-releases.

    python3 scripts/semver.py above <version> <boundary>   # exit 0 when above
    python3 scripts/semver.py selftest                     # the spec's own chain
"""

from __future__ import annotations

import re
import sys

_IDENT = r'(?:0|[1-9][0-9]*|[0-9]*[A-Za-z-][0-9A-Za-z-]*)'

# The tag-name grammar from the version-numbering spec.
TAG_GRAMMAR = re.compile(
    r'^v(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)'
    r'(?:-' + _IDENT + r'(?:\.' + _IDENT + r')*)?'
    r'(?:\+[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*)?$'
)

_VERSION = re.compile(
    r'^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)'
    r'(?:-([0-9A-Za-z.-]+))?'
    r'(?:\+([0-9A-Za-z.-]+))?$'
)


def is_tag(name):
    """True when a git tag name is a legal, v-prefixed version."""
    return TAG_GRAMMAR.match(name) is not None


def parse(text):
    """Parse a version into (major, minor, patch, prerelease-or-None).

    The leading `v` is optional. Build metadata is dropped: it is not part of
    precedence, so two versions differing only in it compare equal.
    """
    if text[:1] in ('v', 'V'):
        text = text[1:]
    match = _VERSION.match(text)
    if match is None:
        return None
    prerelease = None
    if match.group(4) is not None:
        prerelease = tuple(match.group(4).split('.'))
        if '' in prerelease:
            return None
    return (int(match.group(1)), int(match.group(2)), int(match.group(3)), prerelease)


def compare(left, right):
    """Return -1, 0 or 1 for the SemVer 2.0.0 precedence of left against right."""
    for index in range(3):
        if left[index] != right[index]:
            return -1 if left[index] < right[index] else 1

    left_pre, right_pre = left[3], right[3]
    if left_pre is None and right_pre is None:
        return 0
    # A release outranks every pre-release of the same version.
    if left_pre is None:
        return 1
    if right_pre is None:
        return -1

    for left_id, right_id in zip(left_pre, right_pre):
        left_num, right_num = left_id.isdigit(), right_id.isdigit()
        if left_num and right_num:
            if int(left_id) != int(right_id):
                return -1 if int(left_id) < int(right_id) else 1
        elif left_num != right_num:
            # Numeric identifiers always have lower precedence than alphanumeric.
            return -1 if left_num else 1
        elif left_id != right_id:
            return -1 if left_id < right_id else 1

    if len(left_pre) != len(right_pre):
        return -1 if len(left_pre) < len(right_pre) else 1
    return 0


def compare_text(left, right):
    """Compare two version strings; raises ValueError on a malformed one."""
    left_parsed, right_parsed = parse(left), parse(right)
    if left_parsed is None:
        raise ValueError('not a version: %r' % left)
    if right_parsed is None:
        raise ValueError('not a version: %r' % right)
    return compare(left_parsed, right_parsed)


def above(version, boundary):
    """True when version has strictly higher precedence than boundary."""
    return compare_text(version, boundary) > 0


# The ordering SemVer section 11 states, as a chain that must hold end to end.
SPEC_CHAIN = (
    '1.0.0-alpha',
    '1.0.0-alpha.1',
    '1.0.0-alpha.beta',
    '1.0.0-beta',
    '1.0.0-beta.2',
    '1.0.0-beta.11',
    '1.0.0-rc.1',
    '1.0.0',
)

ACCEPTED_TAGS = ('v2.0.0', 'v2.0.0-alpha.1', 'v1.7.0', 'v2.0.0+build.1', 'v0.1.0')

REJECTED_TAGS = (
    'v1.7',
    'V1.7.0',
    'release-1.7.0',
    'v2.0.0-alpha.01',
    'v01.0.0',
    'v2.0.0-',
    'v1.7.0.1',
)


def selftest():
    """Return a list of failures; empty means both halves behave."""
    failures = []

    for index in range(len(SPEC_CHAIN) - 1):
        lower, higher = SPEC_CHAIN[index], SPEC_CHAIN[index + 1]
        if compare_text(lower, higher) >= 0:
            failures.append('precedence: %s should sort below %s' % (lower, higher))
        if compare_text(higher, lower) <= 0:
            failures.append('precedence: %s should sort above %s' % (higher, lower))

    if compare_text('2.0.0+build.1', '2.0.0') != 0:
        failures.append('build metadata must not affect precedence')
    if compare_text('2.0.0-alpha.1', '2.0.0') >= 0:
        failures.append('a pre-release must sort below its release')

    for name in ACCEPTED_TAGS:
        if not is_tag(name):
            failures.append('the tag grammar rejects %r' % name)
    for name in REJECTED_TAGS:
        if is_tag(name):
            failures.append('the tag grammar accepts %r' % name)

    return failures


def main(argv):
    if len(argv) == 2 and argv[1] == 'selftest':
        failures = selftest()
        for failure in failures:
            print('  - %s' % failure)
        print('OK: semver precedence and tag grammar' if not failures else 'FAILURES: %d' % len(failures))
        return 0 if not failures else 1

    if len(argv) == 4 and argv[1] == 'above':
        version, boundary = argv[2], argv[3]
        try:
            result = compare_text(version, boundary)
        except ValueError as error:
            print('error: %s' % error, file=sys.stderr)
            return 2
        if result > 0:
            print('%s is above %s' % (version, boundary))
            return 0
        print('%s is not above %s' % (version, boundary))
        return 1

    print(__doc__.strip(), file=sys.stderr)
    return 2


if __name__ == '__main__':
    sys.exit(main(sys.argv))
