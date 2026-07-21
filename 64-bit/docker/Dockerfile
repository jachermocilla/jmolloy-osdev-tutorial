FROM ubuntu:22.04

# Stop apt from asking questions during the image build.
ENV DEBIAN_FRONTEND=noninteractive

# The chapter Makefiles use gcc (as cc), make, ld and objcopy for the C and
# linking steps, and nasm for the assembly in boot.s. build-essential provides
# the first four; nasm is a separate package. qemu-system-x86 provides
# qemu-system-x86_64, which runs the finished kernel.
RUN apt-get update \
 && apt-get install -y --no-install-recommends \
        build-essential \
        nasm \
        qemu-system-x86 \
 && rm -rf /var/lib/apt/lists/*

# The build and run steps live in the image so the compose file stays small and
# each rule is written down in exactly one place: "initrd chapters run
# mkinitrd.sh, the rest run make" for the build, and the QEMU line for the run.
COPY docker/build-chapter.sh /usr/local/bin/build-chapter
COPY docker/run-chapter.sh   /usr/local/bin/run-chapter
# Absolute mode (not `chmod +x`, which respects the build umask) so the scripts
# are executable by everyone, including the non-root user the container runs as.
RUN chmod 0755 /usr/local/bin/build-chapter /usr/local/bin/run-chapter

# The 64-bit/ tree is mounted here by docker-compose.yml.
WORKDIR /build
ENTRYPOINT ["build-chapter"]
