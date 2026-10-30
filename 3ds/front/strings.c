/* strings.c: the game's DATA\STRINGS.PAK, read by the front end on its own (the game's reader,
   GAMESTRN.C, shares a file handle and a rotating buffer with the game's thread). The format,
   as GAMESTRN.C's header gives it: a word, the Huffman tree's node count; the nodes, 4 bytes
   each (character, unused, left, right; a left of 0xFF is a leaf; the root is the last); a
   word, the block count; per block its number (word) and offset (long). At a block: a word,
   the string count, then a word per string, its offset from the end of that table. A string
   is decoded high bit first, 1 to the right, until '|', 0xFF or 512 characters. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "plat.h"
#include "strings3ds.h"

static unsigned char *pak;
static long paklen;
static int nnodes, nblocks;
static const unsigned char *nodes;
static const unsigned char *blocks;

static unsigned w16(const unsigned char *p) { return p[0] | p[1] << 8; }
static unsigned long w32(const unsigned char *p) { return p[0] | p[1] << 8 | (unsigned long)p[2] << 16 | (unsigned long)p[3] << 24; }

int uw_strings_load(void)
{
    char path[1100];
    FILE *f;
    if (pak) return 0;
    if (plat_resolve("DATA\\STRINGS.PAK", PLAT_READ, path, sizeof path)) return -1;
    if (!(f = fopen(path, "rb"))) return -1;
    fseek(f, 0, SEEK_END);
    paklen = ftell(f);
    fseek(f, 0, SEEK_SET);
    pak = malloc((size_t)paklen);
    if (!pak || fread(pak, 1, (size_t)paklen, f) != (size_t)paklen) { fclose(f); free(pak); pak = NULL; return -1; }
    fclose(f);
    nnodes = (int)w16(pak);
    nodes = pak + 2;
    nblocks = (int)w16(nodes + nnodes * 4);
    blocks = nodes + nnodes * 4 + 2;
    return 0;
}

/* The raw string (escapes and all) into out; 0 if there is none. */
int uw_string_raw(int block, int idx, char *out, int outsz)
{
    int b, i, node, bit = 7, n = 0;
    const unsigned char *p;
    unsigned long off = 0;
    unsigned count;
    if (uw_strings_load()) return 0;
    for (b = 0; b < nblocks; b++)
        if ((int)w16(blocks + b * 6) == block) { off = w32(blocks + b * 6 + 2); break; }
    if (b == nblocks || off + 2 > (unsigned long)paklen) return 0;
    count = w16(pak + off);
    if (idx < 0 || (unsigned)idx >= count) return 0;
    p = pak + off + 2 + count * 2 + w16(pak + off + 2 + idx * 2);
    for (;;) {
        node = nnodes - 1;
        while (nodes[node * 4 + 2] != 0xFF) {
            if (p >= pak + paklen) { out[n] = 0; return n > 0; }
            node = (*p >> bit) & 1 ? nodes[node * 4 + 3] : nodes[node * 4 + 2];
            if (--bit < 0) { bit = 7; p++; }
        }
        i = nodes[node * 4];
        if (i == '|' || i == 0xFF || n >= outsz - 1 || n >= 511) break;
        out[n++] = (char)i;
    }
    out[n] = 0;
    return 1;
}

/* A name as shown: '_' as a space, the plural after '&' and a leading article dropped. */
const char *uw_name(int block, int idx, char *out, int outsz)
{
    char *amp, *s;
    if (!uw_string_raw(block, idx, out, outsz)) { out[0] = 0; return out; }
    if ((amp = strchr(out, '&')) != NULL) *amp = 0;
    for (s = out; *s; s++) if (*s == '_') *s = ' ';
    if (!strncmp(out, "a ", 2)) memmove(out, out + 2, strlen(out + 2) + 1);
    else if (!strncmp(out, "an ", 3)) memmove(out, out + 3, strlen(out + 3) + 1);
    else if (!strncmp(out, "the ", 4)) memmove(out, out + 4, strlen(out + 4) + 1);
    return out;
}
