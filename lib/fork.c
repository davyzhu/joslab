// implement fork from user space

#include <inc/string.h>
#include <inc/lib.h>

// PTE_COW marks copy-on-write page table entries.
// It is one of the bits explicitly allocated to user processes (PTE_AVAIL).
#define PTE_COW		0x800

//
// Custom page fault handler - if faulting page is copy-on-write,
// map in our own private writable copy.
//
static void
pgfault(struct UTrapframe *utf)
{
	void *addr = (void *) utf->utf_fault_va;
	uint32_t err = utf->utf_err;
	int r;
    pte_t pte = vpt[PGNUM((uint32_t)addr)];
    
	// Check that the faulting access was (1) a write, and (2) to a
	// copy-on-write page.  If not, panic.
	// Hint:
	//   Use the read-only page table mappings at vpt
	//   (see <inc/memlayout.h>).
    //   In user space, we cannot execute walk_pgdir()

	// LAB 4: Your code here.
    if (0 == (err & FEC_WR)) 
      panic("Not write: err %x va 0x%x\n", err, (uint32_t)addr);
    if (0 == (pte & PTE_COW)) {
      panic("Not copy-on-write, env_id 0x%x, va 0x%x\n", 
            thisenv->env_id, (uint32_t)addr);
    }
	// Allocate a new page, map it at a temporary location (PFTEMP),
	// copy the data from the old page to the new page, then move the new
	// page to the old page's address.
	// Hint:
	//   You should make three system calls.
	//   No need to explicitly delete the old page's mapping.

	// LAB 4: Your code here.
    void* va_beg = (void*)ROUNDDOWN((uint32_t)addr, PGSIZE);
    cprintf("pgfault: envid %x addr 0x%x va_beg 0x%x\n", 
            thisenv->env_id, (uint32_t)addr, va_beg);

    if ((r = sys_page_alloc(0, (void*)PFTEMP, PTE_URW)) < 0)
      panic("sys_page_alloc: %e", r);
    cprintf("pgfault: p0\n");
	memmove((void*)PFTEMP, va_beg, PGSIZE);
    //cprintf("pgfault: p1\n");
	if ((r = sys_page_map(0, (void*)PFTEMP, 0, va_beg, PTE_URW)) < 0)
      panic("sys_page_map: %e", r);

    //sys_page_unmap(0, (void*)PFTEMP);
    
}

//
// Map our virtual page pn (address pn*PGSIZE) into the target envid
// at the same virtual address.  If the page is writable or copy-on-write,
// the new mapping must be created copy-on-write, and then our mapping must be
// marked copy-on-write as well.  (Exercise: Why do we need to mark ours
// copy-on-write again if it was already copy-on-write at the beginning of
// this function?)
//
// Returns: 0 on success, < 0 on error.
// It is also OK to panic on error.
//
static int
duppage(envid_t envid, unsigned pn)
{
	int r;
    unsigned va;

	// LAB 4: Your code here.
    va = pn << PGSHIFT;

    // vpt[pn] is equal to *(vpt+pn)
    if (vpt[pn] & PTE_P) {
      if ((vpt[pn] & PTE_W) || (vpt[pn] & PTE_COW)) {

        /* cprintf("pn 0x%x va 0x%x\n", pn, va); */

        // check UTOP
        if ((pn >= PGNUM(UTOP)) || (va >= UTOP))
          panic("va must below UTOP\n");

        // check PTE_U
        if (!(vpt[pn] & PTE_U)) 
          panic("perm wrong: must contain PTE_U\n");

        // map child (pp_ref of the page should be 1 before this call,
        // and 2 after this call, bug here)
        if ((r = sys_page_map(0, (void*)va, envid, (void*)va, PTE_P | PTE_U | PTE_COW)) < 0)
          panic("sys_page_map: %e", r);

        // change parent perm(cannot change directly, use syscall)
        if ((r = sys_page_map(0, (void*)va, 0, (void*)va, PTE_P | PTE_U | PTE_COW)) < 0)
          panic("sys_page_map: %e", r);
        
        // check parent perm
        if ((vpt[pn] & PTE_W) && (vpt[pn] & PTE_COW))
          panic("both PTE_W and PTE_COW are set\n");

      } else {
        if ((r = sys_page_map(0, (void*)va, envid, (void*)va, PGOFF(vpt[pn]))) < 0)
          panic("sys_page_map: %e", r);
      }
    }
	return 0;
}

//
// User-level fork with copy-on-write.
// Set up our page fault handler appropriately.
// Create a child.
// Copy our address space and page fault handler setup to the child.
// Then mark the child as runnable and return.
//
// Returns: child's envid to the parent, 0 to the child, < 0 on error.
// It is also OK to panic on error.
//
// Hint:
//   Use vpd, vpt, and duppage.
//   Remember to fix "thisenv" in the child process.
//   Neither user exception stack should ever be marked copy-on-write,
//   so you must allocate a new page for the child's user exception stack.
//
envid_t
fork(void)
{
  // LAB 4: Your code here.
  envid_t envid;
  int r;
  // Set up our page fault handler appropriately.
  set_pgfault_handler(pgfault);
  
  // Create a child.
  if((envid = sys_exofork()) < 0) {
    panic("sys_exofork: %e", envid);
  }

  if (envid == 0) {
    // We're the child.
    // The copied value of the global variable 'thisenv'
    // is no longer valid (it refers to the parent!).
    // Fix it and return 0.
    
    thisenv = &envs[ENVX(sys_getenvid())];
    //cprintf("Child env %x\n", thisenv->env_id);
    return 0;
  }
  
  // copy address space 
  int i,j,pn;
  i=j=pn=0;
  cprintf("begin of duppage\n");
  
  for (i = 0; i < PDX(UTOP); i++) {
    if(vpd[i] & PTE_P) {
      for (j = 0; j < NPTENTRIES; j++) {
        pn = i*NPTENTRIES+j;
        if (pn!=PGNUM(UXSTACKTOP-PGSIZE))
          duppage(envid, pn);
      }
    }
  }
  
  cprintf("end of duppage\n");
  
  // copy page fault handler setup to the child.
  if ((r = sys_env_set_pgfault_upcall(envid, thisenv->env_pgfault_upcall)) < 0)
    panic("sys_env_set_pgfault_upcall: %e", r);

  // allocate a new page for the child's user exception stack
  if ((r = sys_page_alloc(envid, (void*)(UXSTACKTOP-PGSIZE), PTE_URW)) < 0)
    panic("sys_page_alloc: %e", r);

  // Then mark the child as runnable and return.
  if ((r = sys_env_set_status(envid, ENV_RUNNABLE)) < 0)
    panic("sys_env_set_status: %e", r);

  return envid;
}

// Challenge!
int
sfork(void)
{
	panic("sfork not implemented");
	return -E_INVAL;
}
