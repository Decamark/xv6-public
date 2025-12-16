// add-symbol-file
// ELF binaries like init.c are based at 0x0
// We need to add a support for relocation

#include "types.h"
#include "stat.h"
#include "user.h"

int
main(int argc, char** argv)
{
  __asm__ volatile(
    "movb $1, 0x0;"
  );
  exit();
}
