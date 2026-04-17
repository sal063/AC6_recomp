#!/usr/bin/env python3

#              Copyright Catch2 Authors
# Distributed under the Boost Software License, Version 1.0.
#   (See accompanying file LICENSE.txt or copy at
#        https://www.boost.org/LICENSE_1_0.txt)

# SPDX-License-Identifier: BSL-1.0

"""
This test script verifies the behaviour of the legacy section filtering
using `-c`, `--section` CLI parameters.

This is done by having a hardcoded set of test filter + section filter
combinations, together with the expected number of assertions that will
be run inside the test for given filter combo.
"""

import os
import subprocess
import sys
from typing import Tuple, List
import xml.etree.ElementTree as ET

def make_cli_filter(section_names: Tuple[str, ...]) -> List[str]:
    final = []
    for name in section_names:
        final.append('--section')
        final.append(name)
    return final

def run_one_test(binary_path: str, test_name: str, section_names: Tuple[str, ...], expected_assertions: int):
    cmd = [
      binary_path,
      '--reporter', 'xml',
      test_name
    ]
    cmd.extend(make_cli_filter(section_names))
    try:
        ret = subprocess.run(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=True,
            universal_newlines=True,
        )
        stdout = ret.stdout
    except subprocess.SubprocessError as ex:
        print('Could not run "{}"'.format(cmd))
        print("Return code: {}".format(ex.returncode))
        print("stdout: {}".format(ex.stdout))
        print("stderr: {}".format(ex.stderr))
        raise

    try:
        tree = ET.fromstring(stdout)
    except ET.ParseError as ex:
        print("Invalid XML: '{}'".format(ex))
        raise

    # Validate that we ran exactly 1 test case, and it passed
    test_case_stats = tree.find('OverallResultsCases')
    expected_testcases = {'successes' : '1', 'failures' : '0', 'expectedFailures': '0', 'skips': '0'}
    assert test_case_stats.attrib == expected_testcases, f'We did not run single passing test case as expected. {test_name}: {test_case_stats.attrib}'

    # Validate that we got exactly the expected number of passing assertions
    expected_assertions = {'successes' : str(expected_assertions), 'failures' : '0', 'expectedFailures': '0', 'skips': '0'}
    assertion_stats = tree.find('OverallResults')
    assert assertion_stats.attrib == expected_assertions, f'"{test_name}": {assertion_stats.attrib} vs {expected_assertions}'


# Inputs taken from issue #3038
tests = {
  'foo': (
    ((), 3),
    (('A',), 3),
    (('A', 'B'), 0),
    (('A', 'B1'), 1),
    (('A', 'B2'), 1),
    (('A', 'B1', 'B2'), 1),
    (('A', 'B2', 'XXXX'), 1),
  ),
  'bar': (
    ((), 9),
    (('A',), 9),
    (('A', 'B1'), 3),
    (('XXXX',), 2),
    (('B1',), 2),
    (('A', 'B1', 'B2'), 3),
  ),
  'baz': (
    ((), 4),
    (('A',), 1),
    (('A', 'B'), 1),
    (('A', 'XXXX'), 1),
    (('B',), 3),
    (('XXXX',), 0),
  ),
  'qux': (
    ((), 12),
    (('A',), 7),
    (('B',), 9),
    (('B', 'XXXX'), 9),
    (('XXXX',), 6),
  ),
  'corge': (
    ((), 12),
    (('i=2',), 7),
    (('i=3',), 7),
  ),
  'grault': (
    ((), 12),
    (('A',), 3),
    (('B',), 9),
    (('B', 'i=1'), 7),
    (('B', 'XXXX'), 6),
  ),
}

if len(sys.argv) != 2:
    print("Wrong number of arguments, expected just the path to Catch2 SelfTest binary")
    exit(1)

bin_path = os.path.abspath(sys.argv[1])

for test_filter, specs in tests.items():
    for section_path, expected_assertions in specs:
        run_one_test(bin_path, test_filter, section_path, expected_assertions)
