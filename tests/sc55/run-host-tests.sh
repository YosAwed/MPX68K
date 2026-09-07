#!/bin/sh
# macOS-only ROM validation and AVAudioEngine lifecycle checks. No Roland ROMs.
set -eu
cd "$(dirname "$0")/../.."
sc55_test_dir=$(mktemp -d /tmp/mpx68k-sc55-host.XXXXXX)
trap 'rm -rf "$sc55_test_dir"' EXIT
for source in "X68000 Shared/SC55/core/"*.cpp; do
    name=$(basename "$source" .cpp)
    xcrun clang++ -std=c++14 -O2 -c "$source" -o "$sc55_test_dir/$name.o"
done
xcrun swiftc -module-cache-path "$sc55_test_dir/modules" \
    -import-objc-header "X68000 Shared/SC55/SC55Bridge.h" \
    "X68000 Shared/SC55/SC55Synthesizer.swift" tests/sc55/test_host.swift \
    "$sc55_test_dir/"*.o -lc++ -o "$sc55_test_dir/test-host"
"$sc55_test_dir/test-host"
