// test user-level fault handler -- alloc pages to fix faults

#include <inc/lib.h>

void
print_regs(struct PushRegs *regs)
{
	cprintf("  edi  0x%08x\n", regs->reg_edi);
	cprintf("  esi  0x%08x\n", regs->reg_esi);
	cprintf("  ebp  0x%08x\n", regs->reg_ebp);
	cprintf("  oesp 0x%08x\n", regs->reg_oesp);
	cprintf("  ebx  0x%08x\n", regs->reg_ebx);
	cprintf("  edx  0x%08x\n", regs->reg_edx);
	cprintf("  ecx  0x%08x\n", regs->reg_ecx);
	cprintf("  eax  0x%08x\n", regs->reg_eax);
}

void 
print_utf(struct UTrapframe *utf) {
  
  print_regs(&(utf->utf_regs));
}

void
handler(struct UTrapframe *utf)
{
  // execute in user mode CPL=3
  // ebp and esp point to user exception stack
	int r;
	void *addr = (void*)utf->utf_fault_va;
    //cprintf("eflags %x\n", utf->utf_eflags);
    //cprintf("this env id 0x%x\n", thisenv->env_id);
	cprintf("fault %x\n", addr);
	if ((r = sys_page_alloc(0, ROUNDDOWN(addr, PGSIZE),
				PTE_P|PTE_U|PTE_W)) < 0)
		panic("allocating at %x in page fault handler: %e", addr, r);
	//cprintf("return to handler\n");
    // maybe page fault at this point. check info pg
    snprintf((char*) addr, 100, "this string was faulted in at %x",
             addr);
	//cprintf("!!after snprintf\n");    
}

void
umain(int argc, char **argv)
{
  //cprintf("enter umain\n");
  /* 
   * int r;
   * void *addr = (char*)0xDeadBeef;
   */
  set_pgfault_handler(handler);
  /* 
   * if ((r = sys_page_alloc(0, ROUNDDOWN((char*)0xDeadBeef, PGSIZE),
   *                         PTE_P|PTE_U|PTE_W)) < 0)
   *   panic("allocating at %x in page fault handler: %e", addr, r);
   * cprintf("return to umain\n");
   */
    
  cprintf("%s\n", (char*)0xDeadBeef);
  cprintf("return to umain\n");
  cprintf("%s\n", (char*)0xCafeBffe);
}
