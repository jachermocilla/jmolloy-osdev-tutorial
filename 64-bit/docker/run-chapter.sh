#!/bin/bash
#
# Run a built chapter in QEMU, inside the container. The chapter folder name is
# the only argument:
#
#     run-chapter 03_screen_64
#
# These kernels draw to the VGA text buffer, not a serial port, so there is no
# graphical window to open. QEMU's curses display draws that 80x25 text screen
# straight into the terminal, which is what lets it run in a headless container
# and over ssh. Keyboard input reaches the kernel too, so the interactive
# chapters (14 keyboard, 15 exec) work here as well.
#
# To quit QEMU: press Alt+2 to reach the QEMU monitor, type  quit  and Enter.
# On a Mac terminal, press Esc then 2 instead of Alt+2.
#
set -euo pipefail

chapter=${1:-}

if [[ -z $chapter ]]; then
    echo "usage: run-chapter <chapter-folder>" >&2
    exit 2
fi

kernel=$chapter/src/kernel
if [[ ! -f $kernel ]]; then
    echo "error: $kernel not found -- build it first with: ./build.sh $chapter" >&2
    exit 1
fi

# -kernel loads the multiboot image. -initrd adds the RAM disk for the chapters
# that have one. There is no -enable-kvm, so QEMU runs by pure emulation and
# needs no special host devices or privileges: slower, but the same everywhere.
qemu=(qemu-system-x86_64 -kernel "$kernel")
if [[ -f $chapter/initrd.img ]]; then
    qemu+=(-initrd "$chapter/initrd.img")
fi
qemu+=(-display curses -no-reboot)

echo "==> ${qemu[*]}"
echo "    (Alt+2 then 'quit' to exit; Esc then 2 on a Mac)"
echo
exec "${qemu[@]}"
