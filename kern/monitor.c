// Simple command-line kernel monitor useful for
// controlling the kernel and exploring the system interactively.

#include <inc/stdio.h>
#include <inc/string.h>
#include <inc/memlayout.h>
#include <inc/assert.h>
#include <inc/x86.h>

#include <kern/console.h>
#include <kern/monitor.h>
#include <kern/kdebug.h>
#include <kern/trap.h>

#include <kern/pmap.h>

#define CMDBUF_SIZE	80	// enough for one VGA text line


struct Command {
  const char *name;
  const char *desc;
  // return -1 to force monitor to exit
  int (*func)(int argc, char** argv, struct Trapframe* tf);
};

static struct Command commands[] = {
  { "help", "Display this list of commands", mon_help },
  { "infokern", "Display information about the kernel", mon_kerninfo },
  { "bt", "Backtrace", mon_backtrace},
  { "map", "Display virtual to physical address mapping", mon_map},
  { "perm", "set/clear permission of virtual address", mon_perm},
  { "dump", "dump memory content according to p)hysical v)irtual address", mon_dump},
};
#define NCOMMANDS (sizeof(commands)/sizeof(commands[0]))

unsigned read_eip();

/***** Implementations of basic kernel monitor commands *****/

int perm2str(pte_t pte, char perm[]) {
  (pte & PTE_P) ? perm[8] = 'P' : '-'; 
  (pte & PTE_W) ? perm[7] = 'W' : '-'; 
  (pte & PTE_U) ? perm[6] = 'U' : '-'; 
  (pte & PTE_PWT) ? perm[5] = 'T' : '-'; 
  (pte & PTE_PCD) ? perm[4] = 'C' : '-'; 
  (pte & PTE_A) ? perm[3] = 'A' : '-'; 
  (pte & PTE_D) ? perm[2] = 'D' : '-'; 
  (pte & PTE_PS) ? perm[1] = 'S' : '-'; 
  (pte & PTE_G) ? perm[0] = 'G' : '-'; 
  //cprintf("perm %s\n", perm);
  return 0;
}

// loop all PDEs and PTEs, print valid element
// Idea copy from QEMU's "info pg"
// Usage: $ map 0xf0003000
//        $ map 0xf0003000 0xf0005000 
//        Display phys address and perm bits of 
//        virtual address 0x3000, 0x4000 and 0x5000

int mon_map(int argc, char **argv, struct Trapframe *tf) {
  pde_t * pgdir=NULL;
  pte_t * pgtab=NULL;
  pte_t pte=0;
  uint32_t va_beg=0, va_end=0, va=0;
  char perm[10]={'-','-','-','-','-','-','-','-','-','\0'};
  
  if (argc==1) {
    cprintf("missing 2nd arg: va_beg\n");
    return 1;
  } else if (argc > 1) {
    va_beg = str2hex(strfind(argv[1], 'x') + 1);
    if (argc == 3)
      va_end = str2hex(strfind(argv[2], 'x') + 1);
    else
      va_end = va_beg;
  }

  /* cprintf("%x %x\n", va_beg, va_end); */
  pgdir = (pde_t *) KADDR(rcr3());
  cprintf("                       876543210\n");
  cprintf("va       pa       perm(GSDACTUWP)\n");
  for (va = va_beg; va <= va_end; va += PGSIZE) {
    pgtab = pgdir_walk(pgdir, (void*)va, 0);
    perm2str(*pgtab, perm);
    cprintf("0x%x   0x%x   %s\n", va, PTE_ADDR(*pgtab), perm);
  }
  return 0;
}

// Usage: $ perm s 0xf0003000 0x04
//        Set bit. Or 0x10 to virtual address 0xf0003000 perm bits
//        $ perm c 0xf0003000 0x04
//        Clear bit. And ~(0x10) to virtual address 0xf0003000 perm bits
int mon_perm(int argc, char **argv, struct Trapframe *tf) {
  pde_t * pgdir=NULL;
  pte_t * pgtab=NULL;
  pte_t pte=0;

  int set=-1; // 1 is set, 0 is clear, -1 is do nothing
  uint32_t va=0, ori_perm = 0, perm=0;
  
  if (argc!=4) {
    cprintf("Missing 2nd, 3rd, 4th args: va_beg, set/clear, perm\n");
    return 1;
  } 

  if (argv[1][0] == 's') 
    set = 1;
  else if (argv[1][0] == 'c') 
    set = 0;
  else { 
    cprintf("Neither set nor clr\n");
    return 1;
  }

  va = str2hex(strfind(argv[2], 'x') + 1);
  perm = str2hex(strfind(argv[3], 'x') + 1);
  
  pgdir = (pde_t *) KADDR(rcr3());
  pgtab = pgdir_walk(pgdir, (void*)va, 0);
  if(pgtab==0) cprintf("Page not found.\n");
  ori_perm = (*pgtab) & 0xfff;
  if (set==1)
    *pgtab |= perm;
  else if (set==0)
    *pgtab &= ~perm;
  perm = (*pgtab) & 0xfff;

  cprintf("va 0x%x pa 0x%x: perm from 0x%x to 0x%x\n", 
	  va, PTE_ADDR(*pgtab), ori_perm, perm);
  return 0;
}

// Usage: $ dump v 0xf0100000
//        Dump 32b from virtual address 0xf0100000
//        $ dump v 0xf0100000 0xf0005000
//        Dump content from virtual address 0xf0003000 to 0xf0005000     
//        $ dump p 0x3000 0x5000
//        Dump content from physical address 0x3000 to 0x5000    

int mon_dump(int argc, char **argv, struct Trapframe *tf) {
  
  uint32_t va_beg=0, va_end=0, va=0;
  uint32_t pa_beg=0, pa_end=0, pa=0;
  
  bool v=-1; // 1 is virtual, 0 is physical, -1 is do nothing
  pde_t * pgdir=NULL;
  pte_t * pgtab=NULL;

  if (!(argc==3 || argc==4)) {
    cprintf("Missing 2nd, 3rd, (4th) args: v/p, a_beg, (a_end)\n");
    return 1;
  } 
  
  if(argv[1][0] == 'v') {
    v = 1;
    va_beg = str2hex(strfind(argv[2], 'x') + 1);
    if (argc==4)
      va_end = str2hex(strfind(argv[3], 'x') + 1);
  }
  else if (argv[1][0] == 'p') {
    v = 0;
    pa_beg = str2hex(strfind(argv[2], 'x') + 1);
    if (argc==4)
      pa_end = str2hex(strfind(argv[3], 'x') + 1);
  }
  else {
    cprintf("neithor v nor p\n");
    return 1;
  }
  
  pgdir = (pde_t *) KADDR(rcr3());
  va = va_beg;
  pgtab = pgdir_walk(pgdir, (void*)va, 0);
  if(pgtab==0) cprintf("Page not found.\n");
  pa_beg = PTE_ADDR(*pgtab) | (va_beg & 0xFFF);  
  cprintf("v)0x%x p)0x%x: 0x%x\n", va_beg, pa_beg, *(uint32_t*)va_beg);

  return 0;
}


int
mon_help(int argc, char **argv, struct Trapframe *tf)
{
  int i;

  for (i = 0; i < NCOMMANDS; i++)
    cprintf("%s - %s\n", commands[i].name, commands[i].desc);
  return 0;
}

int
mon_kerninfo(int argc, char **argv, struct Trapframe *tf)
{
  extern char entry[], etext[], edata[], end[];

  cprintf("Special kernel symbols:\n");
  cprintf("  entry  %08x (virt)  %08x (phys)\n", entry, entry - KERNBASE);
  cprintf("  etext  %08x (virt)  %08x (phys)\n", etext, etext - KERNBASE);
  cprintf("  edata  %08x (virt)  %08x (phys)\n", edata, edata - KERNBASE);
  cprintf("  end    %08x (virt)  %08x (phys)\n", end, end - KERNBASE);
  cprintf("Kernel executable memory footprint: %dKB\n",
	  (end-entry+1023)/1024);
  return 0;
}

int
mon_backtrace(int argc, char **argv, struct Trapframe *tf)
{

  // Your code here.
  // bug: after enter kernel->init->sh type bt
  // print wrong info at mem_init
  uint32_t ebp, last_ebp, eip, arg[5];
  char eip_fn_name_t[100];
  int eip_offset;
  struct Eipdebuginfo eipdi;

  ebp = (uint32_t)read_ebp();
  eip = (uint32_t)read_eip();
  /* 
   *          dMem     ^             iMem
   *      +-----------+|            +----------+
   *      |   ret     ++ frame N    |          |
   * +--->|           |             |          |
   * |    +-----------+  frame N+1  |          |
   * +----+   ret     <+   |          |
   *      |   ebp     +)----------->|          |
   *      |           ||  eip       |          |
   *      |           ||            |          |
   * ---->|           ||            |          |
   *  esp +-----------+|            +----------+
   *                  -+
   */
  do {
    int i;
    for(i=0;i<5;i++) {
      arg[i] = *((uint32_t *)ebp+2+i);
    }
    cprintf("ebp %x eip %x args %08x %08x %08x %08x %08x\n", ebp, eip,
            arg[0], arg[1], arg[2], arg[3], arg[4]);

    debuginfo_eip(eip, &eipdi);
    strncpy(eip_fn_name_t, eipdi.eip_fn_name, eipdi.eip_fn_namelen);
    eip_fn_name_t[eipdi.eip_fn_namelen]='\0';
    cprintf("        %s:%d: %s+%x\n", eipdi.eip_file, eipdi.eip_line,
            eip_fn_name_t, (eip-eipdi.eip_fn_addr));

    eip = *((uint32_t *)ebp+1);
    last_ebp = ebp;
    ebp = *((uint32_t *)ebp);

  } while (last_ebp);

  return 0;
}



/***** Kernel monitor command interpreter *****/

#define WHITESPACE "\t\r\n "
#define MAXARGS 16

static int
runcmd(char *buf, struct Trapframe *tf)
{
  int argc;
  char *argv[MAXARGS];
  int i;

  // Parse the command buffer into whitespace-separated arguments
  argc = 0;
  argv[argc] = 0;
  while (1) {
    // gobble whitespace
    while (*buf && strchr(WHITESPACE, *buf))
      *buf++ = 0;
    if (*buf == 0)
      break;

    // save and scan past next arg
    if (argc == MAXARGS-1) {
      cprintf("Too many arguments (max %d)\n", MAXARGS);
      return 0;
    }
    argv[argc++] = buf;
    while (*buf && !strchr(WHITESPACE, *buf))
      buf++;
  }
  argv[argc] = 0;

  // Lookup and invoke the command
  if (argc == 0)
    return 0;
  for (i = 0; i < NCOMMANDS; i++) {
    if (strcmp(argv[0], commands[i].name) == 0)
      return commands[i].func(argc, argv, tf);
  }
  cprintf("Unknown command '%s'\n", argv[0]);
  return 0;
}

void
monitor(struct Trapframe *tf)
{
  char *buf;

  cprintf("Welcome to the JOS kernel monitor!\n");
  cprintf("Type 'help' for a list of commands.\n");

	if (tf != NULL)
		print_trapframe(tf);

  while (1) {
    buf = readline("K> ");
    if (buf != NULL)
      if (runcmd(buf, tf) < 0)
	break;
  }
}

// return EIP of caller.
// does not work if inlined.
// putting at the end of the file seems to prevent inlining.
unsigned
read_eip()
{
  uint32_t callerpc;
  __asm __volatile("movl 4(%%ebp), %0" : "=r" (callerpc));
  return callerpc;
}
