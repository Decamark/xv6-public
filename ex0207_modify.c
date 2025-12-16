// Set a kernel-space address (0x80103390) in ph.vaddr
// Set a large integer in ph.memsz
//
// This won't be caught at here:
// if(newsz >= KERNBASE)
//   return 0;
// Only at here:
// if(newsz < oldsz)
//   return oldsz;

#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"
#include "elf.h"

int
main(int argc, char** argv)
{
  // Open our malicious program first
  int fd;
  if ((fd = open("ex0207_exploit", O_RDWR)) < 0) {
    printf(2, "ex0207_modify: cannot open ex0207_exploit\n");
    exit();
  }

  struct stat st;
  if(fstat(fd, &st) < 0){
    printf(2, "ex0207_modify: cannot stat ex0207_exploit\n");
    close(fd);
    exit();
  }

  char* raw = (char*)malloc(st.size);
  if (read(fd, raw, st.size) != st.size) {
    printf(2, "ex0207_modify: failed to read ex0207_exploit\n");
    exit();
  }

  // Write to ph.vaddr
  // TODO: Read all
  struct elfhdr* elf = (struct elfhdr*)raw;

  struct proghdr* ph;
  printf(1, "%x\n", elf->magic);
  printf(1, "%d\n", (int)elf->phnum);
  for(int i=0, off=elf->phoff; i<elf->phnum; i++, off+=sizeof(ph)){
    ph = raw + off;
    printf(1, "Section: %s\n", raw + ph->off);
  }

  // __asm__ volatile(
  //   "movb $1, 0x0;"
  // );
  exit();
}
