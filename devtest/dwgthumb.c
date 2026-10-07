/* dwgthumb.c - dump the embedded DWG thumbnail (preview) to a file. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dwgload.h"
#include <dwg.h>

int main(int argc, char **argv)
{
  if (argc < 3) { fprintf(stderr, "usage: dwgthumb in.dwg out.bin\n"); return 2; }
  Dwg_Data dwg;
  memset(&dwg, 0, sizeof(dwg));
  dwg.opts = 0;
  int err = dwg_read_file(argv[1], &dwg);
  printf("read ret=%d objects=%u\n", err, dwg.num_objects);
  unsigned char *ch = dwg.thumbnail.chain;
  size_t sz = dwg.thumbnail.size;
  printf("thumbnail: chain=%p size=%zu\n", (void *)ch, sz);
  if (!ch || sz < 8) { printf("no embedded thumbnail\n"); dwg_free(&dwg); return 1; }
  FILE *f = fopen(argv[2], "wb");
  if (!f) { printf("cannot write\n"); dwg_free(&dwg); return 1; }
  fwrite(ch, 1, sz, f);
  fclose(f);
  printf("wrote %zu bytes; first8=%02X %02X %02X %02X %02X %02X %02X %02X\n",
         sz, ch[0], ch[1], ch[2], ch[3], ch[4], ch[5], ch[6], ch[7]);
  dwg_free(&dwg);
  return 0;
}