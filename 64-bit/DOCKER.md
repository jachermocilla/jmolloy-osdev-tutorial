# Building and running a chapter with Docker

These files let you build and run any chapter without installing a compiler,
nasm, binutils or QEMU on your machine. You need only Docker (with the Compose
plugin).

    docker/Dockerfile        Ubuntu 22.04 plus gcc, binutils, nasm and QEMU
    docker/build-chapter.sh  the build step that runs inside the container
    docker/run-chapter.sh    the QEMU step that runs inside the container
    docker-compose.yml       the builder service
    build.sh                 build a chapter
    run.sh                   run a chapter (building it first if needed)

## Build

    ./build.sh --list              show the chapters you can build
    ./build.sh 03_screen_64        build one chapter
    ./build.sh 08_vfs_initrd_64    a later chapter, kernel plus RAM disk
    ./build.sh --clean 08_...      delete a chapter's build products

The first run builds the image, which takes a minute or two; later runs reuse
it. The kernel lands in `<chapter>/src/kernel`, and chapters 08 and up also
produce `<chapter>/initrd.img`. Both appear in the chapter folder on your
machine, owned by you, because the sources are mounted into the container and
it runs as your user.

## Run

    ./run.sh 03_screen_64          build if needed, then boot in QEMU

These kernels draw to the VGA text buffer rather than a serial port, so there
is no graphical window. QEMU's curses display draws the 80x25 text screen right
in your terminal, which is what lets it run in a container and over ssh with no
X server. Keyboard input reaches the kernel, so the interactive chapters (14
keyboard, 15 exec) work here too.

To quit QEMU, press Alt+2 to reach the QEMU monitor, type `quit` and press
Enter. On a Mac terminal, press Esc then 2 instead of Alt+2.

If you would rather use a graphical QEMU window, run the kernel with QEMU
installed on the host instead, as each chapter's README describes:

    qemu-system-x86_64 -kernel 08_vfs_initrd_64/src/kernel \
                       -initrd 08_vfs_initrd_64/initrd.img

## Notes

The kernel is 64-bit x86 compiled natively and then rewritten to a 32-bit
multiboot container. The compose file pins the platform to `linux/amd64` so
building and running both work on Apple Silicon and other ARM hosts, where
Docker runs the container under emulation.

QEMU here runs by pure emulation, with no `-enable-kvm`, so it needs no special
host access and behaves the same on every machine. It is fast enough for these
small kernels. On a Linux host with KVM you can speed it up by giving the
container `/dev/kvm` and adding `-enable-kvm`, but that is optional and not
portable, so it is left out by default.

After editing any of the Docker files, rebuild the image once with
`docker compose build`.
