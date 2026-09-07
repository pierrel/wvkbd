# wvkbd fork guide

This repository is Pierre's fork of `jjsullivan5196/wvkbd`. `origin` is the fork
and `upstream` is the canonical project. The fork's `main` branch tracks upstream;
EmacsOS-specific work lives on `emacsos`, initially based on tag `v0.16` to match
the PinePhone's packaged keyboard.

Keep the fork narrowly about keyboard input and rendering. Phone session selection,
deployment, and fallback behavior belong in the sibling `emacsos` repository.
Do not duplicate gesture interpretation in Emacs: wvkbd owns touch classification
and emits ordinary Wayland keyboard chords that work in Emacs, Firefox, and Android.

Build the upstream-compatible default with `make`. EmacsOS builds the same mobintl
layout under a side-by-side name with:

```sh
make BIN=wvkbd-emacos LAYOUT=mobintl
```

Before committing C changes, run `make format` and build with `-Wall`. Preserve the
stock command-line and signal behavior unless a reviewed EmacsOS design explicitly
changes it. The packaged `/usr/bin/wvkbd-mobintl` is the phone's recovery fallback;
this repository must never install over that path.
