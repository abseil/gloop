#! /bin/bash

source googletest.sh || exit 1

BINDIR=$TEST_SRCDIR///gloop/util/symbolize
EXE=$BINDIR/hugepage_text_test_helper
EXE_NOPIE=$BINDIR/hugepage_text_test_helper_no_pie

typeset -a tests
while read line; do
  if [[ $line =~ \.$ ]]; then
    T=$line
  else
    tests+=(${T}${line})
  fi
done < <($EXE --nologtostderr --gunit_list_tests)

for exe in "$EXE" "$EXE_NOPIE"; do
  for t in ${tests[*]}; do
    for proc_self_exe in '' '/no/such/file'; do
      "$exe" --gunit_filter="$t" --proc_self_exe="$proc_self_exe" "$@" || exit 1
    done
  done
done

exit 0
