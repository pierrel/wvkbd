# FrequencyWords English frequency list

`glide-words-en.h` is an adapted subset of
`hermitdave/FrequencyWords/content/2018/en/en_50k.txt` at commit
`525f9b560de45753a5ea01069454e72e9aa541c6`:

FrequencyWords is maintained by Hermit Dave. The list is derived from the
OpenSubtitles 2018 English corpus.

https://github.com/hermitdave/FrequencyWords/blob/525f9b560de45753a5ea01069454e72e9aa541c6/content/2018/en/en_50k.txt

The exact source is 622749 bytes with SHA-256
`5351ff405b1126ef555791dd4d9798a48e3e9a501a9fc481a9da957752cfb458`.
FrequencyWords identifies the source corpus as OpenSubtitles 2018 and licenses
the generated content under Creative Commons Attribution-ShareAlike 4.0
International (CC BY-SA 4.0).

The wvkbd generator filters that list to the first 20000 unique lowercase ASCII
words of length 2 through 24, preserves source frequency order within
first/last-letter buckets, and emits a packed C header. This is a modification
of the source data. The adapted data is distributed under the same CC BY-SA 4.0
license. The complete license is in `LICENSES/CC-BY-SA-4.0.txt` and online at:

https://creativecommons.org/licenses/by-sa/4.0/legalcode
