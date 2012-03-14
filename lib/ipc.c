// User-level IPC library routines

#include <inc/lib.h>

// Receive a value via IPC and return it.
// If 'pg' is nonnull, then any page sent by the sender will be mapped at
//	that address.
// If 'from_env_store' is nonnull, then store the IPC sender's envid in
//	*from_env_store.
// If 'perm_store' is nonnull, then store the IPC sender's page permission
//	in *perm_store (this is nonzero iff a page was successfully
//	transferred to 'pg').
// If the system call fails, then store 0 in *fromenv and *perm (if
//	they're nonnull) and return the error.
// Otherwise, return the value sent by the sender
//
// Hint:
//   Use 'thisenv' to discover the value and who sent it.
//   If 'pg' is null, pass sys_ipc_recv a value that it will understand
//   as meaning "no page".  (Zero is not the right value, since that's
//   a perfectly valid place to map a page.)
int32_t
ipc_recv(envid_t *from_env_store, void *pg, int *perm_store)
{
  // LAB 4: Your code here.
  int r;

  //cprintf("[%x] enter ipc_recv \n", thisenv->env_id);
  if (pg)
    r = sys_ipc_recv(pg);
  else
    r = sys_ipc_recv((void*)UTOP);

  //cprintf("[%x] ipc_recv r %d\n", thisenv->env_id, r);

  if (from_env_store)
    *from_env_store = (r==0) ? thisenv->env_ipc_from : 0;

  if (perm_store)
    *perm_store = (r==0 && (uint32_t)pg<UTOP) ? thisenv->env_ipc_perm : 0;

  if (r == 0)
    return thisenv->env_ipc_value; 
  else
    return r;
}

// Send 'val' (and 'pg' with 'perm', if 'pg' is nonnull) to 'to_env'.
// This function keeps trying until it succeeds.
// It should panic() on any error other than -E_IPC_NOT_RECV.
//
// Hint:
//   Use sys_yield() to be CPU-friendly.
//   If 'pg' is null, pass sys_ipc_recv a value that it will understand
//   as meaning "no page".  (Zero is not the right value.)
void
ipc_send(envid_t to_env, uint32_t val, void *pg, int perm)
{
  // LAB 4: Your code here.
  int r;
  do {
    //cprintf("[%x]ipc_send\n", thisenv->env_id);
    r = sys_ipc_try_send(to_env, val, pg ? pg : (void*)UTOP, perm);
    if (r != 0 && r != -E_IPC_NOT_RECV) 
      panic("%e", r);
    /* 
     * if (r == -E_IPC_NOT_RECV)
     *   cprintf("[%x]ipc_send not received\n", thisenv->env_id);
     * if (r == 0)
     *   cprintf("[%x]ipc_send received\n", thisenv->env_id);
     */
  } while (r != 0);
}

// Find the first environment of the given type.  We'll use this to
// find special environments.
// Returns 0 if no such environment exists.
envid_t
ipc_find_env(enum EnvType type)
{
	int i;
	for (i = 0; i < NENV; i++)
		if (envs[i].env_type == type)
			return envs[i].env_id;
	return 0;
}
