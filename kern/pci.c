#include <inc/x86.h>
#include <inc/assert.h>
#include <inc/string.h>
#include <kern/pci.h>
#include <kern/pcireg.h>
#include <kern/e1000.h>
#include <kern/pmap.h>

#define debug 1
volatile uint32_t *pci;
struct tx_desc *pci_tdlist;
uint32_t *pci_pbuf;

// Flag to do "lspci" at bootup
static int pci_show_devs = 1;
static int pci_show_addrs = 1;

// PCI "configuration mechanism one"
static uint32_t pci_conf1_addr_ioport = 0x0cf8;
static uint32_t pci_conf1_data_ioport = 0x0cfc;


// Forward declarations
static int pci_bridge_attach(struct pci_func *pcif);

// PCI driver table
struct pci_driver {
	uint32_t key1, key2;
	int (*attachfn) (struct pci_func *pcif);
};

// pci_attach_class matches the class and subclass of a PCI device
struct pci_driver pci_attach_class[] = {
	{ PCI_CLASS_BRIDGE, PCI_SUBCLASS_BRIDGE_PCI, &pci_bridge_attach },
	{ 0, 0, 0 },
};

static int pci_attach_fun(struct pci_func *pcif);
// pci_attach_vendor matches the vendor ID and device ID of a PCI device
struct pci_driver pci_attach_vendor[] = {
	{ 0x8086, 0x100e, pci_attach_fun },
};

static int pci_attach_fun(struct pci_func *pcif) {
  pci_func_enable(pcif);
  // map base address register to VA in kern_pgdir
  boot_map_region(kern_pgdir,
                  KPCI_MMIO,
                  ROUNDUP(pcif->reg_size[0], PGSIZE),
                  ROUNDDOWN(pcif->reg_base[0], PGSIZE),
                  (PTE_P | PTE_W | PTE_PCD| PTE_PWT)
                  );

  pci = (uint32_t*)KPCI_MMIO;
  pci_tdlist = (struct tx_desc*)(KPCI_MMIO + ROUNDUP(pcif->reg_size[0], PGSIZE));
  pci_pbuf = (uint32_t*)((uint32_t)pci_tdlist + ROUNDUP(TX_DESC_LEN, PGSIZE));
  
  if(debug)
    cprintf("pci 0x%x pci_tdlist 0x%x pci_pb 0x%x\n",
            (uint32_t)pci, (uint32_t)pci_tdlist, (uint32_t)pci_pbuf);

  // Ex6.4 test
  //uint32_t reg_status_addr = pci[E1000_STATUS];
  //cprintf("reg_status_addr 0x%x\n", reg_status_addr);
  if (pci[E1000_STATUS] != 0x80080783) {
    panic("reg status 0x%x\n", pci[E1000_STATUS]);
  }
  
  // transmit init: 8254x spec 14.5
  // address in transmit descriptor lists and regs should be 
  // physical address.
  struct Page *pg;
  int i, r;
  // allocate memory for transmit descriptor list
  if((pg = page_alloc(ALLOC_ZERO)) == NULL)
    panic("out of memory");
  if ((r = page_insert(kern_pgdir, pg, pci_tdlist, PTE_KRW)) != 0)
    panic("cannot insert page");
  // set TDBAL
  pci[E1000_TDBAL] = page2pa(pg);
  // set TDLEN
  pci[E1000_TDLEN] = TX_DESC_LEN;

  cprintf("TDBAL 0x%x, TDLEN 0x%x\n", pci[E1000_TDBAL], pci[E1000_TDLEN]);

  // allocate memory for packet buffer
  void *pci_pbuf_i = (void*)pci_pbuf;
  for (i=0; i<N_TX_DESC; i++) {
  //for (i=0; i<2; i++) {
    if((pg = page_alloc(ALLOC_ZERO)) == NULL)
      panic("out of memory");
    if ((r = page_insert(kern_pgdir, pg, pci_pbuf_i, PTE_KRW)) != 0)
      panic("cannot insert page");
    // assign pa of packet buffer to descriptor address
    pci_tdlist[i].addr.fields.low = page2pa(pg);
    /* cprintf("pg @pa 0x%x\n", page2pa(pg)); */
    pci_tdlist[i].lower.flags.length = 0x100; // length
    pci_tdlist[i].lower.data |= E1000_TXD_CMD_RS | E1000_TXD_CMD_EOP; 
    pci_tdlist[i].upper.data |= E1000_TXD_STAT_DD;
    pci_pbuf_i += PGSIZE;
    /* 
     * cprintf("VA DATA: addr @0x%x 0x%x, len @0x%x 0x%x, status @0x%x 0x%x\n",
     *         &pci_tdlist[4*i], pci_tdlist[4*i], 
     *         &pci_tdlist[4*i+2], pci_tdlist[4*i+2],
     *         &pci_tdlist[4*i+3], pci_tdlist[4*i+3]);
     */
  }
  
  // clear TDH and TDT
  pci[E1000_TDH] = 0;
  pci[E1000_TDT] = 0;
  
  // FD is full duplex
  // set TCTL
  pci[E1000_TCTL] = E1000_TCTL_EN | E1000_TCTL_PSP | 
    E1000_TCTL_CT_ETH | E1000_TCTL_COLD_FD;

  // set TIPG (spec 13.4.34, IEEE802.3)
  // IPGR2 = 6(0000000110), IPGR1 = 8(0000001000), IPGT = 10(0000001010), 
  pci[E1000_TIPG] = E1000_TIPG_FD;

  return 0;
}

int pci_send_pkt(void* srcva, size_t len) {
  int tdt = pci[E1000_TDT];

  // check DD bit to make sure packets have been transported
  while((pci_tdlist[tdt].upper.data & E1000_TXD_STAT_DD) == 0)
    ;

  void* dstva = (void*)((uint32_t)pci_pbuf + tdt*PGSIZE);  
  memmove(dstva, srcva, len);
  if (pci_tdlist[tdt].upper.data & E1000_TXD_STAT_DD) {
    pci_tdlist[tdt].lower.flags.length = len;    
    pci[E1000_TDT] = 
      (tdt == N_TX_DESC - 1) ? 0 : tdt + 1; // tdt should be only used
                                            // by one process at one time
  }
  return 0;
}

/* 
 * int pci_send_pkts() {
 *   int i;
 *   for (i=0; i<35; i++)
 *     //cprintf("send pkt %d\n", i);
 *     pci_send_pkt(i);
 *   return 0;
 * }
 */

static void
pci_conf1_set_addr(uint32_t bus,
		   uint32_t dev,
		   uint32_t func,
		   uint32_t offset)
{
	assert(bus < 256);
	assert(dev < 32);
	assert(func < 8);
	assert(offset < 256);
	assert((offset & 0x3) == 0);

	uint32_t v = (1 << 31) |		// config-space
		(bus << 16) | (dev << 11) | (func << 8) | (offset);
	outl(pci_conf1_addr_ioport, v);
}

static uint32_t
pci_conf_read(struct pci_func *f, uint32_t off)
{
	pci_conf1_set_addr(f->bus->busno, f->dev, f->func, off);
	return inl(pci_conf1_data_ioport);
}

static void
pci_conf_write(struct pci_func *f, uint32_t off, uint32_t v)
{
	pci_conf1_set_addr(f->bus->busno, f->dev, f->func, off);
	outl(pci_conf1_data_ioport, v);
}

static int __attribute__((warn_unused_result))
pci_attach_match(uint32_t key1, uint32_t key2,
		 struct pci_driver *list, struct pci_func *pcif)
{
	uint32_t i;

	for (i = 0; list[i].attachfn; i++) {
		if (list[i].key1 == key1 && list[i].key2 == key2) {
			int r = list[i].attachfn(pcif);
			if (r > 0)
				return r;
			if (r < 0)
				cprintf("pci_attach_match: attaching "
					"%x.%x (%p): %e\n",
					key1, key2, list[i].attachfn, r);
		}
	}
	return 0;
}

static int
pci_attach(struct pci_func *f)
{
	return
		pci_attach_match(PCI_CLASS(f->dev_class),
				 PCI_SUBCLASS(f->dev_class),
				 &pci_attach_class[0], f) ||
		pci_attach_match(PCI_VENDOR(f->dev_id),
				 PCI_PRODUCT(f->dev_id),
				 &pci_attach_vendor[0], f);
}

static const char *pci_class[] =
{
	[0x0] = "Unknown",
	[0x1] = "Storage controller",
	[0x2] = "Network controller",
	[0x3] = "Display controller",
	[0x4] = "Multimedia device",
	[0x5] = "Memory controller",
	[0x6] = "Bridge device",
};

static void
pci_print_func(struct pci_func *f)
{
	const char *class = pci_class[0];
	if (PCI_CLASS(f->dev_class) < sizeof(pci_class) / sizeof(pci_class[0]))
		class = pci_class[PCI_CLASS(f->dev_class)];

	cprintf("PCI: %02x:%02x.%d: %04x:%04x: class: %x.%x (%s) irq: %d\n",
		f->bus->busno, f->dev, f->func,
		PCI_VENDOR(f->dev_id), PCI_PRODUCT(f->dev_id),
		PCI_CLASS(f->dev_class), PCI_SUBCLASS(f->dev_class), class,
		f->irq_line);
}

static int
pci_scan_bus(struct pci_bus *bus)
{
	int totaldev = 0;
	struct pci_func df;
	memset(&df, 0, sizeof(df));
	df.bus = bus;

	for (df.dev = 0; df.dev < 32; df.dev++) {
		uint32_t bhlc = pci_conf_read(&df, PCI_BHLC_REG);
		if (PCI_HDRTYPE_TYPE(bhlc) > 1)	    // Unsupported or no device
			continue;

		totaldev++;

		struct pci_func f = df;
		for (f.func = 0; f.func < (PCI_HDRTYPE_MULTIFN(bhlc) ? 8 : 1);
		     f.func++) {
			struct pci_func af = f;

			af.dev_id = pci_conf_read(&f, PCI_ID_REG);
			if (PCI_VENDOR(af.dev_id) == 0xffff)
				continue;

			uint32_t intr = pci_conf_read(&af, PCI_INTERRUPT_REG);
			af.irq_line = PCI_INTERRUPT_LINE(intr);

			af.dev_class = pci_conf_read(&af, PCI_CLASS_REG);
			if (pci_show_devs)
				pci_print_func(&af);
			pci_attach(&af);
		}
	}

	return totaldev;
}

static int
pci_bridge_attach(struct pci_func *pcif)
{
	uint32_t ioreg  = pci_conf_read(pcif, PCI_BRIDGE_STATIO_REG);
	uint32_t busreg = pci_conf_read(pcif, PCI_BRIDGE_BUS_REG);

	if (PCI_BRIDGE_IO_32BITS(ioreg)) {
		cprintf("PCI: %02x:%02x.%d: 32-bit bridge IO not supported.\n",
			pcif->bus->busno, pcif->dev, pcif->func);
		return 0;
	}

	struct pci_bus nbus;
	memset(&nbus, 0, sizeof(nbus));
	nbus.parent_bridge = pcif;
	nbus.busno = (busreg >> PCI_BRIDGE_BUS_SECONDARY_SHIFT) & 0xff;

	if (pci_show_devs)
		cprintf("PCI: %02x:%02x.%d: bridge to PCI bus %d--%d\n",
			pcif->bus->busno, pcif->dev, pcif->func,
			nbus.busno,
			(busreg >> PCI_BRIDGE_BUS_SUBORDINATE_SHIFT) & 0xff);

	pci_scan_bus(&nbus);
	return 1;
}

// External PCI subsystem interface

void
pci_func_enable(struct pci_func *f)
{
	pci_conf_write(f, PCI_COMMAND_STATUS_REG,
		       PCI_COMMAND_IO_ENABLE |
		       PCI_COMMAND_MEM_ENABLE |
		       PCI_COMMAND_MASTER_ENABLE);

	uint32_t bar_width;
	uint32_t bar;
	for (bar = PCI_MAPREG_START; bar < PCI_MAPREG_END;
	     bar += bar_width)
	{
		uint32_t oldv = pci_conf_read(f, bar);

		bar_width = 4;
		pci_conf_write(f, bar, 0xffffffff);
		uint32_t rv = pci_conf_read(f, bar);

		if (rv == 0)
			continue;

		int regnum = PCI_MAPREG_NUM(bar);
		uint32_t base, size;
		if (PCI_MAPREG_TYPE(rv) == PCI_MAPREG_TYPE_MEM) {
			if (PCI_MAPREG_MEM_TYPE(rv) == PCI_MAPREG_MEM_TYPE_64BIT)
				bar_width = 8;

			size = PCI_MAPREG_MEM_SIZE(rv);
			base = PCI_MAPREG_MEM_ADDR(oldv);
			if (pci_show_addrs)
				cprintf("  mem region(reg[%d]): 0x%x bytes at 0x%x\n",
					regnum, size, base);
		} else {
			size = PCI_MAPREG_IO_SIZE(rv);
			base = PCI_MAPREG_IO_ADDR(oldv);
			if (pci_show_addrs)
				cprintf("  io region(reg[%d]): 0x%x bytes at 0x%x\n",
					regnum, size, base);
		}

		pci_conf_write(f, bar, oldv);
		f->reg_base[regnum] = base;
		f->reg_size[regnum] = size;

		if (size && !base)
			cprintf("PCI device %02x:%02x.%d (%04x:%04x) "
				"may be misconfigured: "
				"region %d: base 0x%x, size %d\n",
				f->bus->busno, f->dev, f->func,
				PCI_VENDOR(f->dev_id), PCI_PRODUCT(f->dev_id),
				regnum, base, size);
	}

	cprintf("PCI function %02x:%02x.%d (%04x:%04x) enabled\n",
		f->bus->busno, f->dev, f->func,
		PCI_VENDOR(f->dev_id), PCI_PRODUCT(f->dev_id));
}


int
pci_init(void)
{
	static struct pci_bus root_bus;
	int r;
  memset(&root_bus, 0, sizeof(root_bus));

  r = pci_scan_bus(&root_bus);
  //pci_send_pkts();
  return r;
}
