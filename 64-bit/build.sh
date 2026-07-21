#!/bin/bash
#
# Build one chapter of the 64-bit tutorial inside a Docker container, so you
# need Docker but no local compiler, nasm or binutils.
#
#     ./build.sh 03_screen_64        build one chapter
#     ./build.sh --list              list the chapters you can build
#     ./build.sh --clean 08_...      remove that chapter's build products
#
# The container is Ubuntu 22.04 with gcc, binutils and nasm. Sources are
# mounted from this directory, so the kernel (and initrd.img for chapters 08
# and up) appears in the chapter folder just as a local build would.
#
set -euo pipefail

# Run from this script's own directory (the 64-bit/ root) so it works no matter
# where it is called from.
cd "$(dirname "$0")"

usage() {
    echo "usage: $0 <chapter-folder>"
    echo "       $0 --list"
    echo "       $0 --clean <chapter-folder>"
}

# A chapter is any folder with a src/Makefile. Listing them by search, rather
# than hard-coding names, means a newly added chapter needs no edit here.
list_chapters() {
    find . -mindepth 3 -maxdepth 3 -path '*/src/Makefile' \
        | sed 's|^\./||; s|/src/Makefile$||' | sort
}

# Remove build products. This mirrors the Makefile's clean plus the two files
# mkinitrd.sh leaves in the chapter folder, and uses only rm, so it needs no
# local toolchain.
clean_chapter() {
    local chapter=$1
    rm -f "$chapter"/src/*.o \
          "$chapter"/src/kernel \
          "$chapter"/src/kernel64.elf \
          "$chapter"/initrd.img \
          "$chapter"/make_initrd
    echo "cleaned $chapter"
}

case ${1:-} in
    ""|-h|--help)
        usage
        exit 0 ;;
    --list)
        list_chapters
        exit 0 ;;
    --clean)
        chapter=${2:-}
        if [[ -z $chapter || ! -d $chapter/src ]]; then
            echo "error: give a chapter folder to clean." >&2
            usage
            exit 2
        fi
        clean_chapter "$chapter"
        exit 0 ;;
esac

chapter=$1

if [[ ! -d $chapter/src ]]; then
    echo "error: no chapter named '$chapter' here." >&2
    echo
    echo "available chapters:"
    list_chapters | sed 's/^/  /'
    exit 1
fi

# id -u and id -g are passed to compose so the container writes the build
# products as you, not as root.
DOCKER_UID=$(id -u) DOCKER_GID=$(id -g) \
    docker compose run --rm builder "$chapter"
