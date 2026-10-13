#!/usr/bin/env python3
#
# Copyright 2026 Signal Messenger, LLC
# SPDX-License-Identifier: AGPL-3.0-only
#
# Chromium's build/ compiles Java with `--release 25`, but RingRTC and Signal
# build with JDK 21, which can't read class files from Java 25. Run as a DEPS
# hook to compile with `--release 21` instead.

import os
import re

GYP_DIR = os.path.join(os.path.dirname(__file__), '..', 'build', 'android', 'gyp')
PATCHES = {
    'compile_java.py': r"('--release',(?:\s*#[^\n]*\n)*\s*')\d+'",
    'turbine.py': r"(\['--release', ')\d+'",
}

for name, pattern in PATCHES.items():
    path = os.path.join(GYP_DIR, name)
    with open(path) as f:
        contents = f.read()
    patched, count = re.subn(pattern, r"\g<1>21'", contents)
    if count != 1:
        raise SystemExit(f'Failed to patch the javac release in {path}')
    if patched != contents:
        with open(path, 'w') as f:
            f.write(patched)
