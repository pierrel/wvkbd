#!/bin/sh

set -eu

source=${1:?pass the pinned wordninja gzip as the first argument}
repo=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
scratch=$(mktemp -d)
trap 'rm -rf -- "$scratch"' EXIT HUP INT TERM
expected=5b3c6c418fea7188b3919c47829ff94bc010f8336c74cbb929cc20d0c9d9e901

[ "$(wc -c <"$source")" -eq 538593 ]
[ "$(sha256sum "$source" | awk '{print $1}')" = "$expected" ]
python3 "$repo/tools/generate-glide-dictionary.py" "$source" "$scratch/generated.h"
cmp -s "$scratch/generated.h" "$repo/glide-words-en.h"

cp "$source" "$scratch/wrong-size.gz"
printf x >>"$scratch/wrong-size.gz"
if python3 "$repo/tools/generate-glide-dictionary.py" \
    "$scratch/wrong-size.gz" "$scratch/wrong-size.h" 2>/dev/null; then
    printf '%s\n' 'wrong dictionary size was accepted' >&2
    exit 1
fi

cp "$source" "$scratch/wrong-hash.gz"
printf x | dd of="$scratch/wrong-hash.gz" bs=1 seek=0 conv=notrunc status=none
if python3 "$repo/tools/generate-glide-dictionary.py" \
    "$scratch/wrong-hash.gz" "$scratch/wrong-hash.h" 2>/dev/null; then
    printf '%s\n' 'wrong dictionary hash was accepted' >&2
    exit 1
fi

printf '%s\n' 'glide dictionary generation tests passed'
