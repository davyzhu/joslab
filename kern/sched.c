#include <inc/assert.h>

#include <kern/env.h>
#include <kern/pmap.h>
#include <kern/monitor.h>


// Choose a user environment to run and run it.
void
sched_yield(void)
{
	struct Env *idle;
	int i;
    uint32_t ei, ei_beg; // env number begin at

	// Implement simple round-robin scheduling.
	// case 1:
	// Search through 'envs' for an ENV_RUNNABLE environment in
	// circular fashion starting just after the env this CPU was
	// last running.  Switch to the first such environment found.
	// case 2:
	// If no envs are runnable, but the environment previously
	// running on this CPU is still ENV_RUNNING, it's okay to
	// choose that environment.
	// case 3:
	// Never choose an environment that's currently running on
	// another CPU (env_status == ENV_RUNNING) and never choose an
	// idle environment (env_type == ENV_TYPE_IDLE).  If there are
	// no runnable environments, simply drop through to the code
	// below to switch to this CPU's idle environment.

	// LAB 4: Your code here.

    ei_beg = (thiscpu->cpu_env) ? ENVX(thiscpu->cpu_env->env_id) : 0;
    ei_beg++;
    ei_beg = ei_beg % NENV;
    //cprintf("cpu env begin %x\n", (uint32_t)ei_beg);
    
    // case 1: RUNNABLE
	for (i = 0; i < NENV; i++) {
      ei = (ei_beg + i) % NENV;
      if (envs[ei].env_type != ENV_TYPE_IDLE &&
          envs[ei].env_status == ENV_RUNNABLE) {
        /* 
         * cprintf("cpu[%d] pick runnable envs[%d]\n", 
         *         cpunum(), ei, envs[ei].env_type, envs[ei].env_status);
         */
        env_run(&envs[ei]);
        break;
      }
	}
    
    // debug: print how many envs is running
    //print_envs(0);

    // case 2: RUNNING
	for (i = 0; i < NENV; i++) {
      ei = (ei_beg + i) % NENV;
      if (envs[ei].env_type != ENV_TYPE_IDLE &&
          envs[ei].env_status == ENV_RUNNING &&
          envs[ei].env_cpunum == cpunum()
          ) {
        /* 
         * cprintf("cpu[%d] pick running envs[%d]\n", 
         *         cpunum(), ei, envs[ei].env_type, envs[ei].env_status);
         */
        env_run(&envs[ei]);
        break;
      }
	}


	// For debugging and testing purposes, if there are no
	// runnable environments other than the idle environments,
	// drop into the kernel monitor.
	if (i == NENV) {
      //cprintf("cpu[%d] No more runnable/running environments!\n", cpunum());
		//while (1)
		//	monitor(NULL);
	}

	// Run this CPU's idle environment when nothing else is runnable.
    //cprintf("cpu[%d] run idle\n", cpunum());
	idle = &envs[cpunum()];
	if (!(idle->env_status == ENV_RUNNABLE || idle->env_status == ENV_RUNNING))
		panic("CPU %d: No idle environment!", cpunum());
	env_run(idle);
}
