#ifndef JOS_KERN_PCI_H
#define JOS_KERN_PCI_H

#include <inc/types.h>

extern volatile uint32_t *pci; //PCI MMIO VA 
extern struct tx_desc *pci_tdlist; //PCI transmit descriptor list VA
extern uint32_t *pci_pbuf; // PCI packet buffer VA

#define N_TX_DESC 32
#define TX_DESC_SIZE 16
#define TX_DESC_LEN (N_TX_DESC*TX_DESC_SIZE)

#define PKT_BUF_SIZE PGSIZE //packet size is one page for ease
#define PKT_BUF_LEN (N_TX_DESC*PKT_BUF_SIZE)

// PCI subsystem interface
enum { pci_res_bus, pci_res_mem, pci_res_io, pci_res_max };

struct pci_bus;

struct pci_func {
    struct pci_bus *bus;	// Primary bus for bridges

    uint32_t dev;
    uint32_t func;

    uint32_t dev_id;
    uint32_t dev_class;

    uint32_t reg_base[6];
    uint32_t reg_size[6];
    uint8_t irq_line;
};

struct pci_bus {
    struct pci_func *parent_bridge;
    uint32_t busno;
};

// transmit descriptor
/* Transmit Descriptor */
struct tx_desc {
    union {
        uint64_t all; 
        struct {
            uint32_t low;    /* low address */
            uint32_t high;    /* high address */
        } fields;
    } addr;
    union {
        uint32_t data;
        struct {
            uint16_t length;    /* Data buffer length */
            uint8_t cso;        /* Checksum offset */
            uint8_t cmd;        /* Descriptor control */
        } flags;
    } lower;
    union {
        uint32_t data;
        struct {
            uint8_t status;     /* Descriptor status */
            uint8_t css;        /* Checksum start */
            uint16_t special;
        } fields;
    } upper;
} __attribute__((packed));

int  pci_init(void);
void pci_func_enable(struct pci_func *f);
int pci_send_pkt(void* va, size_t len);

#endif
