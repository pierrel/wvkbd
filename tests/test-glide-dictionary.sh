#!/bin/sh

set -eu

source=${1:?pass the pinned FrequencyWords en_50k.txt as the first argument}
repo=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
scratch=$(mktemp -d)
trap 'rm -rf -- "$scratch"' EXIT HUP INT TERM
expected=5351ff405b1126ef555791dd4d9798a48e3e9a501a9fc481a9da957752cfb458

[ "$(wc -c <"$source")" -eq 622749 ]
[ "$(sha256sum "$source" | awk '{print $1}')" = "$expected" ]
python3 "$repo/tools/generate-glide-dictionary.py" "$source" "$scratch/generated.h"
cmp -s "$scratch/generated.h" "$repo/glide-words-en.h"

cp "$source" "$scratch/wrong-size.txt"
printf x >>"$scratch/wrong-size.txt"
if python3 "$repo/tools/generate-glide-dictionary.py" \
    "$scratch/wrong-size.txt" "$scratch/wrong-size.h" 2>/dev/null; then
    printf '%s\n' 'wrong dictionary size was accepted' >&2
    exit 1
fi

cp "$source" "$scratch/wrong-hash.txt"
printf x | dd of="$scratch/wrong-hash.txt" bs=1 seek=0 conv=notrunc status=none
if python3 "$repo/tools/generate-glide-dictionary.py" \
    "$scratch/wrong-hash.txt" "$scratch/wrong-hash.h" 2>/dev/null; then
    printf '%s\n' 'wrong dictionary hash was accepted' >&2
    exit 1
fi

python3 "$repo/tests/test-generate-glide-dictionary.py"
printf '%s\n' 'glide dictionary generation tests passed'
