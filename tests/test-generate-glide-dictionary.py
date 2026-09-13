#!/usr/bin/env python3

import importlib.util
import pathlib
import tempfile


REPO = pathlib.Path(__file__).resolve().parent.parent
SPEC = importlib.util.spec_from_file_location(
    "glide_dictionary_generator", REPO / "tools/generate-glide-dictionary.py"
)
GENERATOR = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(GENERATOR)


def expect_failure(text, message):
    with tempfile.TemporaryDirectory() as scratch:
        output = pathlib.Path(scratch) / "output.h"
        try:
            GENERATOR.generate(text, output)
        except SystemExit as error:
            assert str(error) == "generate-glide-dictionary: " + message
        else:
            raise AssertionError(message + " was accepted")


def bucket_word(number):
    interior = bytearray()
    while True:
        interior.append(ord("a") + number % 26)
        number //= 26
        if number == 0:
            return b"a" + bytes(reversed(interior)) + b"a"


def main():
    original_max_words = GENERATOR.MAX_WORDS
    original_payload_limit = GENERATOR.STATIC_PAYLOAD_LIMIT

    GENERATOR.MAX_WORDS = 2
    expect_failure(b"the 10\nbroken\n", "malformed frequency record")
    expect_failure(
        b"the 10\nthere 11\n",
        "frequency records are not positive and non-increasing",
    )

    GENERATOR.MAX_WORDS = 513
    bucket = b"".join(
        bucket_word(index) + b" " + str(1000 - index).encode() + b"\n"
        for index in range(513)
    )
    expect_failure(bucket, "dictionary bucket exceeds its bound")

    GENERATOR.MAX_WORDS = 1
    GENERATOR.STATIC_PAYLOAD_LIMIT = GENERATOR.BUCKET_TABLE_BYTES + 2
    expect_failure(b"aa 1\n", "dictionary payload is too large")

    GENERATOR.MAX_WORDS = original_max_words
    GENERATOR.STATIC_PAYLOAD_LIMIT = original_payload_limit
    print("glide dictionary validation tests passed")


if __name__ == "__main__":
    main()
