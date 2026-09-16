#!/bin/sh

set -eu

binary=${1:?pass the wvkbd binary}
scratch=$(mktemp -d)
trap 'rm -rf -- "$scratch"' EXIT HUP INT TERM

"$binary" --help >"$scratch/help" 2>&1
grep -F -- '--mod-swipe - Tap, Ctrl/Alt/Ctrl+Alt swipes, or glide Latin letters' \
    "$scratch/help" >/dev/null
grep -F -- '--glide-learning-fd 3 - Send private swipe observations on fd 3' \
    "$scratch/help" >/dev/null
if "$binary" --glide-learning-fd 4 >"$scratch/learning-invalid" 2>&1; then
    printf '%s\n' 'non-fd3 learning transport was accepted' >&2
    exit 1
fi
grep -F 'usage:' "$scratch/learning-invalid" >/dev/null
if XDG_RUNTIME_DIR="$scratch" WAYLAND_DISPLAY=missing \
    "$binary" --glide-learning-fd 3 >"$scratch/learning-valid" 2>&1; then
    printf '%s\n' 'learning fd3 unexpectedly exited without a display' >&2
    exit 1
fi
grep -F 'Failed to create display' "$scratch/learning-valid" >/dev/null
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

if XDG_RUNTIME_DIR="$scratch" WAYLAND_DISPLAY=missing "$binary" --mod-swipe -o >"$scratch/compatible" 2>&1; then
    printf '%s\n' '--mod-swipe -o unexpectedly exited without a display' >&2
    exit 1
fi
grep -Fx -- 'Failed to create display' "$scratch/compatible" >/dev/null
if grep -F -- 'cannot be combined' "$scratch/compatible" >/dev/null; then
    printf '%s\n' '--mod-swipe -o was rejected before Wayland setup' >&2
    exit 1
fi

printf '%s\n' 'modifier swipe CLI tests passed'
