#!/usr/bin/env python3
"""Run a test binary and report a failure by case name, not by a log tail.

CI used two different implementations of this — a grep over the JUnit XML on
Linux and an XML walk in PowerShell on Windows — and the grep silently
under-reported, because it only matched a `<failure>` that followed the closing
`</testcase>` tag directly. One parser for both platforms removes that class of
difference.

    python3 scripts/run_test_suite.py --binary build/linux/x86_64/release/tests \
        --report /tmp/ut.xml --log /tmp/ut.log --label "unit tests"
"""

from __future__ import annotations

import argparse
import subprocess
import sys
import xml.etree.ElementTree as ElementTree
from pathlib import Path


def failing_cases(report):
    """Every failing case as (classname, name, message), from the JUnit report."""
    try:
        tree = ElementTree.parse(report)
    except (OSError, ElementTree.ParseError) as error:
        return [('<report>', str(report), 'could not be read: %s' % error)]

    found = []
    for case in tree.iter('testcase'):
        failure = case.find('failure')
        error = case.find('error')
        problem = failure if failure is not None else error
        if problem is None:
            continue
        message = (problem.get('message') or problem.text or '').strip().split('\n')[0]
        found.append((case.get('classname', '?'), case.get('name', '?'), message[:400]))
    return found


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--binary', required=True)
    parser.add_argument('--report', required=True, help='where the JUnit XML goes')
    parser.add_argument('--log', required=True, help='where the console output goes')
    parser.add_argument('--label', default='tests')
    parser.add_argument('--tail', type=int, default=25)
    args = parser.parse_args(argv[1:])

    report, log = Path(args.report), Path(args.log)
    report.parent.mkdir(parents=True, exist_ok=True)
    log.parent.mkdir(parents=True, exist_ok=True)

    with log.open('w', encoding='utf-8', errors='replace') as handle:
        try:
            completed = subprocess.run(
                (args.binary, '-r', 'console', '-r', 'junit::out=%s' % report),
                stdout=handle, stderr=subprocess.STDOUT)
            code = completed.returncode
        except OSError as error:
            # A missing or non-executable binary is a wrong path, not a test
            # failure; say which rather than raising a traceback.
            print('::error::could not run %s: %s' % (args.binary, error))
            return 1

    lines = log.read_text(encoding='utf-8', errors='replace').splitlines()
    for line in lines[-args.tail:]:
        print(line)

    if code == 0:
        return 0

    print('::error::%s failed (exit %d)' % (args.label, code))
    # The report is written even when cases throw, and it names them; the log
    # tail above does not.
    for classname, name, message in failing_cases(report):
        print('FAILED: %s :: %s' % (classname, name))
        if message:
            print('        %s' % message)
    return code


if __name__ == '__main__':
    sys.exit(main(sys.argv))
