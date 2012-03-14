#ifndef JOS_INC_SYSCALL_H
#define JOS_INC_SYSCALL_H

/* system call numbers */
enum {
	SYS_cputs = 0,
	SYS_cgetc, 
	SYS_getenvid,
	SYS_env_destroy,
	SYS_page_alloc,
	SYS_page_map,
	SYS_page_unmap,
	SYS_exofork,
	SYS_env_set_status,
	SYS_env_set_trapframe,
	SYS_env_set_pgfault_upcall,
	SYS_yield,
	SYS_ipc_try_send,
	SYS_ipc_recv,
	NSYSCALLS
};

/* 
 * enum {
 * 0 SYS_cputs = 0,
 * 1 SYS_cgetc,  
 * 2 SYS_getenvid, 
 * 3 SYS_env_destroy,
 * 4 SYS_page_alloc,
 * 5 SYS_page_map,
 * 6 SYS_page_unmap,
 * 7 SYS_exofork,
 * 8 SYS_env_set_status,
 * 9 SYS_env_set_pgfault_upcall,
 * a SYS_yield,
 * b SYS_ipc_try_send,
 * c SYS_ipc_recv,
 * 	NSYSCALLS
 * };
 */


#endif /* !JOS_INC_SYSCALL_H */
