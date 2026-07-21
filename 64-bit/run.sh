#!/bin/bash
#
# Run a chapter in QEMU inside the Docker container, so you need Docker but no
# local QEMU. If the chapter has not been built yet, this builds it first.
#
#     ./run.sh 03_screen_64      build if needed, then run
#     ./run.sh --list            list the chapters you can run
#
# The kernel's 80x25 text screen is drawn in your terminal (QEMU's curses
# display), so this works over ssh and in a plain terminal with no X server.
# To quit QEMU: press Alt+2 (Esc then 2 on a Mac), type  quit  and Enter.
#
set -euo pipefail

# Run from this script's own directory (the 64-bit/ root).
cd "$(dirname "$0")"

usage() {
    echo "usage: $0 <chapter-folder>"
    echo "       $0 --list"
}

list_chapters() {
    find . -mindepth 3 -maxdepth 3 -path '*/src/Makefile' \
        | sed 's|^\./||; s|/src/Makefile$||' | sort
}

case ${1:-} in
    ""|-h|--help)
        usage
        exit 0 ;;
    --list)
        list_chapters
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

# Build first if there is no kernel yet, so "./run.sh <chapter>" just works.
if [[ ! -f $chapter/src/kernel ]]; then
    echo "not built yet -- building $chapter first."
    ./build.sh "$chapter"
    echo
fi

# QEMU's curses display needs a terminal, so keep the TTY (no -T) and pass TERM
# through so curses picks the right terminfo. The build products are already on
# disk, so the user id only matters if QEMU writes anything; we pass it for
# consistency with build.sh.
DOCKER_UID=$(id -u) DOCKER_GID=$(id -g) \
    docker compose run --rm -e TERM --entrypoint run-chapter builder "$chapter"
