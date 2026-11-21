#!/bin/sh
# Host checks for the SCSIEXROM.DAT validation (macOS, no ROM files needed).
# Set SCSIEXROM=/path/to/SCSIEXROM.DAT to also check a real dump.
set -eu
cd "$(dirname "$0")/../.."
dir=$(mktemp -d /tmp/mpx68k-scsirom.XXXXXX)
trap 'rm -rf "$dir"' EXIT
xcrun swiftc -module-cache-path "$dir/modules" \
    "X68000 Shared/SCSIROMImage.swift" tests/scsirom/test_scsirom.swift \
    -o "$dir/test-scsirom"
"$dir/test-scsirom"
