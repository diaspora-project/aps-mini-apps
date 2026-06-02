#!/usr/bin/env bash
# Move the log files of one experiment run into a new directory.
#
# Usage: bash scripts/save-logs.sh <target-dir>
#
# Collected from the current working directory:
#   *.out, *.err, *.ts.txt   — per-component logs
#   mofka.json, mofka-config.env — copied (not moved) if present, so the
#                                  next run can reuse the same Mofka setup
#
# Existing target-dir is reused; existing files inside it are overwritten.

set -euo pipefail

if [[ $# -ne 1 ]]; then
    echo "Usage: $0 <target-dir>" >&2
    exit 2
fi

target="$1"
mkdir -p "$target"

moved=0
for pattern in '*.out' '*.err' '*.ts.txt'; do
    for f in $pattern; do
        [[ -e "$f" ]] || continue
        mv -- "$f" "$target/"
        moved=$((moved + 1))
    done
done

copied=0
for f in mofka.json mofka-config.env; do
    if [[ -e "$f" ]]; then
        cp -- "$f" "$target/"
        copied=$((copied + 1))
    fi
done

echo "Moved $moved log file(s) and copied $copied config file(s) into $target/"
