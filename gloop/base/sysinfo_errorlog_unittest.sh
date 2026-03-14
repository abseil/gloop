#!/bin/bash
# Copyright 2005 Google, Inc.
# Author: Paul Menage

# Simple test to check that errors are logged by sysinfo functions at
# the appropriate times.

output=$TEST_TMPDIR/unittestbin.out
binary=$TEST_SRCDIR/_main/gloop/base/sysinfo_unittest

"$binary" > "$output" 2>&1
rc=$?
if [ $rc != 0 ]; then
  {
    echo "FAIL: $binary exited with $rc"
    echo "--- $binary OUTPUT BEGIN ---"
    cat "$output"
    echo "--- $binary OUTPUT END -----"
  } >&2
  exit 1
fi

check_string() {
    local string=$1
    local count=$2

    grep -a "$string" "$output"
    check_eq "$(grep -ac "$string" "$output")" "$count" "$string != $count"
}

check_string NonexistentKeyword 1
check_string YouShouldntSeeThisError 0
check_string nonexistentfile 1
check_string nonexistentquietfile 0

# Count the number of times each error line occurs (stripping the
# date/time) and check that none of them occur more than three times.

check_eq "$(grep -a ^E "$output" | cut -c14- | sort | uniq -c | tee /dev/stderr | grep -v '^ *[123]')" ""

echo PASS

