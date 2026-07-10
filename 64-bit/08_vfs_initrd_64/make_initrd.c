// make_initrd.c -- Builds an initrd.img from a list of files.
//                  Written for JamesM's kernel development tutorials.
//
// This is a *host* program. It runs on your development machine and writes a
// file that the kernel will later read. The struct below is therefore a
// contract with initrd.h, and both sides must agree byte for byte.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stddef.h>

#define MAX_FILES 64

struct initrd_header
{
    unsigned char magic;
    char name[64];
    unsigned int offset;
    unsigned int length;
};

// The kernel asserts exactly this in initrd.h. Assert it here too, so a
// mismatch is caught by whichever side is compiled first.
_Static_assert(sizeof(struct initrd_header) == 76, "on-disk layout changed");
_Static_assert(offsetof(struct initrd_header, offset) == 68, "offset moved");

int main(int argc, char **argv)
{
    if (argc < 3 || (argc - 1) % 2 != 0)
    {
        fprintf(stderr, "usage: %s <src1> <name1> [<src2> <name2> ...]\n", argv[0]);
        return 1;
    }

    int nheaders = (argc - 1) / 2;
    if (nheaders > MAX_FILES)
    {
        fprintf(stderr, "Error: at most %d files\n", MAX_FILES);
        return 1;
    }

    // The tutorial leaves this array uninitialised and then writes all 64
    // entries to the file, leaking whatever was on its stack into initrd.img.
    struct initrd_header headers[MAX_FILES];
    memset(headers, 0, sizeof(headers));

    printf("size of header: %zu\n", sizeof(struct initrd_header));
    unsigned int off = sizeof(struct initrd_header) * MAX_FILES + sizeof(unsigned int);

    for (int i = 0; i < nheaders; i++)
    {
        const char *src = argv[i*2 + 1];
        const char *dst = argv[i*2 + 2];
        printf("writing file %s->%s at 0x%x\n", src, dst, off);

        if (strlen(dst) >= sizeof(headers[i].name))
        {
            fprintf(stderr, "Error: name too long: %s\n", dst);
            return 1;
        }
        strcpy(headers[i].name, dst);

        // "rb", not "r". On this host it makes no difference; on a host where
        // it does, you would spend an evening finding out.
        FILE *stream = fopen(src, "rb");
        if (stream == 0)
        {
            fprintf(stderr, "Error: file not found: %s\n", src);
            return 1;
        }
        fseek(stream, 0, SEEK_END);
        headers[i].offset = off;
        headers[i].length = (unsigned int)ftell(stream);
        off += headers[i].length;
        fclose(stream);
        headers[i].magic = 0xBF;
    }

    FILE *wstream = fopen("./initrd.img", "wb");
    if (!wstream) { perror("initrd.img"); return 1; }

    unsigned int n = (unsigned int)nheaders;   // the kernel reads a u32int here
    fwrite(&n, sizeof(unsigned int), 1, wstream);
    fwrite(headers, sizeof(struct initrd_header), MAX_FILES, wstream);

    for (int i = 0; i < nheaders; i++)
    {
        FILE *stream = fopen(argv[i*2 + 1], "rb");
        unsigned char *buf = malloc(headers[i].length);
        fread(buf, 1, headers[i].length, stream);
        fwrite(buf, 1, headers[i].length, wstream);
        fclose(stream);
        free(buf);
    }

    fclose(wstream);
    printf("wrote initrd.img (%u bytes)\n", off);
    return 0;
}
