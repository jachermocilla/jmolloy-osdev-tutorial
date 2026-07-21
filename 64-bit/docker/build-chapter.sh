#!/bin/bash
#
# Build one chapter of the 64-bit tutorial. This runs inside the container;
# docker compose passes the chapter folder name as the only argument.
#
#     build-chapter 03_screen_64
#
# There are two kinds of chapter, told apart by whether mkinitrd.sh is present:
#
#     03-07   Kernel only. Built with `make -C src`.
#     08+     Kernel plus a RAM disk. mkinitrd.sh builds the initrd packer,
#             packs initrd.img (chapter 15 also builds the ring 3 program),
#             then runs `make -C src` itself.
#
# -e stops on the first failed command, -u catches unset variables, and
# pipefail makes a failure anywhere in a pipeline fail the whole line.
set -euo pipefail

chapter=${1:-}

if [[ -z $chapter ]]; then
    echo "usage: build-chapter <chapter-folder>" >&2
    exit 2
fi

# A real chapter has a src/ directory with the Makefile in it.
if [[ ! -d $chapter/src ]]; then
    echo "error: '$chapter' is not a chapter folder (no src/ inside)." >&2
    exit 1
fi

cd "$chapter"

if [[ -f mkinitrd.sh ]]; then
    echo "==> $chapter (kernel + initrd)"
    bash mkinitrd.sh
else
    echo "==> $chapter (kernel only)"
    make -C src
fi

echo
echo "built: $chapter/src/kernel"
if [[ -f initrd.img ]]; then
    echo "       $chapter/initrd.img"
fi
