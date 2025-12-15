#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "defs.h"
#include "x86.h"
#include "elf.h"

int
exec(char *path, char **argv)
{
  dprintf("Executing %s\n", path);

  char *s, *last;
  int i, off;
  uint argc, vaddr, base, sp, ustack[3+MAXARG+1];
  struct elfhdr elf;
  struct inode *ip;
  struct proghdr ph;
  pde_t *pgdir, *oldpgdir;
  struct proc *curproc = myproc();

  begin_op();

  if((ip = namei(path)) == 0){
    end_op();
    cprintf("exec: fail\n");
    return -1;
  }
  ilock(ip);
  pgdir = 0;

  // #!/interp
  char buf[10];
  if (readi(ip, buf, 0, sizeof(buf)) != sizeof(buf))
    goto bad;
  dprintf("First 10 bytes: %x\n", *(uint*)buf);
  if (buf[0] == '#' && buf[1] == '!') {
    // Release lock
    iunlockput(ip);
    end_op();
    // Tailor arguments
    char* argv0 = kalloc();
    argv0[0] = 's';
    argv0[1] = 'h';
    argv0[2] = 0;
    char** _argv = kalloc();
    _argv[0] = argv0;
    _argv[1] = path;
    _argv[2] = 0;
    exec("sh", _argv);
    return 0;
  }

  // Check ELF header
  if(readi(ip, (char*)&elf, 0, sizeof(elf)) != sizeof(elf))
    goto bad;
  if(elf.magic != ELF_MAGIC)
    goto bad;
  dprintf("elf.entry: 0x%x\n", elf.entry);

  if((pgdir = setupkvm()) == 0)
    goto bad;

  // Load program into memory.
  base = 0; // Exercise 2.5
  vaddr = base;
  for(i=0, off=elf.phoff; i<elf.phnum; i++, off+=sizeof(ph)){
    if(readi(ip, (char*)&ph, off, sizeof(ph)) != sizeof(ph))
      goto bad;
    dprintf("ph = {\n");
    dprintf("  type:   0x%x\n", ph.type);
    dprintf("  memsz:  0x%x\n", ph.memsz);
    dprintf("  filesz: 0x%x\n", ph.filesz);
    dprintf("  vaddr:  0x%x\n", ph.vaddr);
    dprintf("}\n");
    if(ph.type != ELF_PROG_LOAD)
      continue;
    if(ph.memsz < ph.filesz)
      goto bad;
    if(ph.vaddr + ph.memsz < ph.vaddr)
      goto bad;
    dprintf("Allocated 0x%x - ", vaddr);
    if((vaddr = allocuvm(pgdir, vaddr, base + ph.vaddr + ph.memsz)) == 0)
      goto bad;
    dprintf("0x%x\n", base + ph.vaddr + ph.memsz);
    if(ph.vaddr % PGSIZE != 0)
      goto bad;
    if(loaduvm(pgdir, base + (char*)ph.vaddr, ip, ph.off, ph.filesz) < 0)
      goto bad;
  }
  iunlockput(ip);
  end_op();
  ip = 0;

  // Allocate two pages at the next page boundary.
  // Make the first inaccessible.  Use the second as the user stack.
  vaddr = PGROUNDUP(vaddr);
  if((vaddr = allocuvm(pgdir, vaddr, vaddr + 2*PGSIZE)) == 0)
    goto bad;
  clearpteu(pgdir, (char*)(vaddr - 2*PGSIZE));
  sp = vaddr;
  dprintf("sp: 0x%x\n", sp);

  // Push argument strings, prepare rest of stack in ustack.
  for(argc = 0; argv[argc]; argc++) {
    if(argc >= MAXARG)
      goto bad;
    sp = (sp - (strlen(argv[argc]) + 1)) & ~3;
    if(copyout(pgdir, sp, argv[argc], strlen(argv[argc]) + 1) < 0)
      goto bad;
    ustack[3+argc] = sp;
  }
  ustack[3+argc] = 0;

  ustack[0] = 0xffffffff;  // fake return PC
  ustack[1] = argc;
  ustack[2] = sp - (argc+1)*4;  // argv pointer

  sp -= (3+argc+1) * 4;
  if(copyout(pgdir, sp, ustack, (3+argc+1)*4) < 0)
    goto bad;

  // Save program name for debugging.
  for(last=s=path; *s; s++)
    if(*s == '/')
      last = s+1;
  safestrcpy(curproc->name, last, sizeof(curproc->name));

  // Commit to the user image.
  oldpgdir = curproc->pgdir;
  curproc->pgdir = pgdir;
  curproc->sz = vaddr - base;
  curproc->tf->eip = base + elf.entry;  // main
  curproc->tf->esp = sp;
  switchuvm(curproc);
  freevm(oldpgdir);
  return 0;

 bad:
  if(pgdir)
    freevm(pgdir);
  if(ip){
    iunlockput(ip);
    end_op();
  }
  return -1;
}
