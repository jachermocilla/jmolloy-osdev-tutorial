#!/bin/bash
#
# Clean every chapter.
#
# Two things get built per chapter, in two places. `make -C src clean` handles
# the kernel. The initrd chapters (08 and up) also build make_initrd and
# initrd.img in the chapter directory itself, via mkinitrd.sh, and those are
# this script's to remove. make_initrd.c, test.txt and test2.txt are sources
# and stay.
#
# Chapters are found rather than listed, so a new one needs no edit here.
#
# Usage: bash clean.sh

set -u

cd "$(dirname "$0")" || exit 1

status=0

while IFS= read -r makefile; do
    chapter=$(dirname "$(dirname "$makefile")")
    echo "==> $chapter"

    if ! make -C "$chapter/src" clean; then
        echo "    make clean FAILED" >&2
        status=1
    fi

    rm -f "$chapter/initrd.img" "$chapter/make_initrd"
done < <(find . -mindepth 3 -maxdepth 3 -name Makefile -path '*/src/*' | sort)

exit $status
