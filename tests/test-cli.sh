#!/bin/sh

set -eu

binary=${1:?pass the wvkbd binary}
scratch=$(mktemp -d)
trap 'rm -rf -- "$scratch"' EXIT HUP INT TERM

"$binary" --help >"$scratch/help" 2>&1
grep -F -- '--mod-swipe - Tap, Ctrl/Alt swipe, or glide Latin letters' \
    "$scratch/help" >/dev/null
if "$binary" --mod-swipe -O >"$scratch/incompatible" 2>&1; then
    printf '%s\n' 'incompatible output mode was accepted' >&2
    exit 1
fi
grep -Fx -- '--mod-swipe cannot be combined with -O' \
    "$scratch/incompatible" >/dev/null
if grep -F 'Failed to create display' "$scratch/incompatible" >/dev/null; then
    printf '%s\n' 'incompatible options reached Wayland setup' >&2
    exit 1
fi

printf '%s\n' 'modifier swipe CLI tests passed'
