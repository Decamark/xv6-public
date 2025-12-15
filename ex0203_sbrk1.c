/**
 * Write a user program that grows its address space with 1 byte by calling sbrk(1).
 * Run the program and investigate the page table for the program before the call 
 * to sbrk and after the call to sbrk. How much space has the kernel allocated?
 * What does the pte for the new memory contain?
 */

// 4096 bytes (https://github.com/Decamark/xv6-public/blob/main/vm.c#L240)
// gdb kernel
// target remote :26000
// bp allocuvm

#include "types.h"
#include "user.h"

int
main()
{
  // ???: How can I set a breakpoint here from GDB?
  // 
  // asm("int $3");
  __asm__ __volatile__(
    ".globl ex0203\n"
    "ex0203:\n"
  );
  sbrk(1);
  exit();
}
