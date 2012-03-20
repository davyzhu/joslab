#include "ns.h"

extern union Nsipc nsipcbuf;

void
output(envid_t ns_envid)
{
	binaryname = "ns_output";
    
	// LAB 6: Your code here:
	// 	- read a packet from the network server
	//	- send the packet to the device driver
    envid_t from_envid;
    struct jif_pkt *pkt = (struct jif_pkt*)REQVA;
    int perm;
    int r;

    cprintf("enter output\n");
    while(1) {
      r = ipc_recv(&from_envid, pkt, &perm); 
      sys_pci_send_pkt(0, pkt->jp_data, pkt->jp_len);
    }
}
