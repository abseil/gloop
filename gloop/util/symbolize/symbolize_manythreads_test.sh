#!/bin/bash

# For each --symbol_map_compression_level we require a fresh process so that
# the call to util::SymbolMap::GetCached() constructs a new SymbolMap.
for compression_level in {0..3}; do
  "${TEST_SRCDIR}///gloop/util/symbolize/symbolize_manythreads_test" --symbol_map_compression_level=$compression_level || exit 1;
done
