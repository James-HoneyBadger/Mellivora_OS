/*
 * elfinfo.c - read a file's ELF32 header and say what the loader saw
 *
 * Mellivora loads three image formats and the file's header decides which one
 * an image is (docs/spec/EXEC.md section 1): HBX, ELF32, or a flat binary with
 * no header at all. This program ships as the second of them, so pointed at
 * itself it describes the container it arrived in - class, byte order, machine,
 * entry point, and the PT_LOAD segments the kernel copied to the addresses in
 * their own program headers.
 *
 * It reads the *file*, not its own memory, and that is the point. By the time a
 * program runs, its segments have been copied to the addresses their headers
 * named and the first copy has landed on the ELF header and program header
 * table themselves; the file is the only surviving copy of the description.
 *
 * Usage: elfinfo [file]              (default: /bin/elfinfo)
 */
#include <stdio.h>
#include <string.h>

#include <mv/args.h>
#include <mv/types.h>

#define EHDR_SIZE   52
#define PHDR_SIZE   32

/* e_ident offsets */
#define EI_CLASS    4
#define EI_DATA     5

#define ELFCLASS32  1
#define ELFDATA2LSB 1

#define ET_REL      1
#define ET_EXEC     2
#define ET_DYN      3

#define EM_386      3

#define PT_LOAD     1

#define PT_FLAG_X   1
#define PT_FLAG_W   2
#define PT_FLAG_R   4

static u32 le32(const unsigned char *p)
{
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static unsigned le16(const unsigned char *p)
{
    return (unsigned)p[0] | ((unsigned)p[1] << 8);
}

static const char *type_name(unsigned t)
{
    switch (t) {
    case ET_REL:  return "REL (relocatable)";
    case ET_EXEC: return "EXEC (fixed address)";
    case ET_DYN:  return "DYN (position independent)";
    default:      return "unknown";
    }
}

static const char *machine_name(unsigned m)
{
    switch (m) {
    case EM_386: return "i386";
    case 8:      return "MIPS";
    case 20:     return "PowerPC";
    case 40:     return "ARM";
    case 62:     return "x86-64";
    default:     return "not i386";
    }
}

static void flags_text(unsigned f, char out[4])
{
    out[0] = (f & PT_FLAG_R) ? 'r' : '-';
    out[1] = (f & PT_FLAG_W) ? 'w' : '-';
    out[2] = (f & PT_FLAG_X) ? 'x' : '-';
    out[3] = '\0';
}

int main(void)
{
    static char args[MV_ARGS_BUFSZ];
    char *argv[MV_ARGS_MAX];
    int argc = mv_args("elfinfo", args, sizeof args, argv, MV_ARGS_MAX);
    const char *path = (argc > 1) ? argv[1] : "/bin/elfinfo";
    unsigned char eh[EHDR_SIZE];
    unsigned char ph[PHDR_SIZE];
    FILE *f;
    unsigned phoff, phnum, phentsize, i;
    unsigned loads = 0;

    f = fopen(path, "rb");
    if (f == 0) {
        fprintf(stderr, "elfinfo: cannot open %s\n", path);
        return 1;
    }
    if (fread(eh, 1, sizeof eh, f) != sizeof eh) {
        fprintf(stderr, "elfinfo: %s is shorter than an ELF header\n", path);
        fclose(f);
        return 1;
    }
    if (eh[0] != 0x7F || eh[1] != 'E' || eh[2] != 'L' || eh[3] != 'F') {
        printf("elfinfo: %s is not an ELF image\n", path);
        fclose(f);
        return 1;
    }

    printf("elfinfo: %s\n", path);
    printf("  class    %s\n", eh[EI_CLASS] == ELFCLASS32 ? "ELF32" : "not ELF32");
    printf("  data     %s\n",
           eh[EI_DATA] == ELFDATA2LSB ? "little-endian" : "big-endian");
    printf("  type     %s\n", type_name(le16(eh + 16)));
    printf("  machine  %s\n", machine_name(le16(eh + 18)));
    printf("  entry    0x%08x\n", (unsigned)le32(eh + 24));

    phoff = le32(eh + 28);
    phentsize = le16(eh + 42);
    phnum = le16(eh + 44);
    printf("  phdrs    %u x %u at 0x%08x\n", phnum, phentsize, phoff);

    if (phentsize != PHDR_SIZE) {
        printf("elfinfo: program headers are %u bytes, not %u: this is not an "
               "ELF32 image the loader can walk\n", phentsize, (unsigned)PHDR_SIZE);
        fclose(f);
        return 1;
    }
    if (fseek(f, (long)phoff, SEEK_SET) != 0) {
        fprintf(stderr, "elfinfo: cannot seek to the program headers\n");
        fclose(f);
        return 1;
    }

    for (i = 0; i < phnum; i++) {
        char fl[4];
        unsigned type;

        if (fread(ph, 1, sizeof ph, f) != sizeof ph) {
            fprintf(stderr, "elfinfo: program header %u is past the end\n", i);
            fclose(f);
            return 1;
        }
        type = le32(ph + 0);
        if (type != PT_LOAD)
            continue;
        flags_text(le32(ph + 24), fl);
        printf("  LOAD %s vaddr 0x%08x filesz 0x%08x memsz 0x%08x\n",
               fl, (unsigned)le32(ph + 8), (unsigned)le32(ph + 16),
               (unsigned)le32(ph + 20));
        loads++;
    }

    fclose(f);
    printf("%u loadable segment%s\n", loads, loads == 1 ? "" : "s");
    printf("ELFINFO_OK\n");
    return 0;
}
