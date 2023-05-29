#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include "lib/pci.h"
#include <fcntl.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <errno.h>

#define FLAG(x,y) ((x & y) ? '+' : '-')

#define MSI_DATA_VECTOR_SHIFT		0
#define  MSI_DATA_VECTOR_MASK		0x000000ff
#define	 MSI_DATA_VECTOR(v)		(((v) << MSI_DATA_VECTOR_SHIFT) & \
					 MSI_DATA_VECTOR_MASK)

#define MSI_DATA_DELIVERY_MODE_SHIFT	8
#define  MSI_DATA_DELIVERY_FIXED	(0 << MSI_DATA_DELIVERY_MODE_SHIFT)
#define  MSI_DATA_DELIVERY_LOWPRI	(1 << MSI_DATA_DELIVERY_MODE_SHIFT)

#define MSI_DATA_LEVEL_SHIFT		14
#define	 MSI_DATA_LEVEL_DEASSERT	(0 << MSI_DATA_LEVEL_SHIFT)
#define	 MSI_DATA_LEVEL_ASSERT		(1 << MSI_DATA_LEVEL_SHIFT)

#define MSI_DATA_TRIGGER_SHIFT		15
#define  MSI_DATA_TRIGGER_EDGE		(0 << MSI_DATA_TRIGGER_SHIFT)
#define  MSI_DATA_TRIGGER_LEVEL		(1 << MSI_DATA_TRIGGER_SHIFT)

/*
 * Shift/mask fields for msi address
 */

#define MSI_ADDR_BASE_HI		0
#define MSI_ADDR_BASE_LO		0xfee00000

#define MSI_ADDR_DEST_MODE_SHIFT	2
#define  MSI_ADDR_DEST_MODE_PHYSICAL	(0 << MSI_ADDR_DEST_MODE_SHIFT)
#define	 MSI_ADDR_DEST_MODE_LOGICAL	(1 << MSI_ADDR_DEST_MODE_SHIFT)

#define MSI_ADDR_REDIRECTION_SHIFT	3
#define  MSI_ADDR_REDIRECTION_CPU	(0 << MSI_ADDR_REDIRECTION_SHIFT)
					/* dedicated cpu */
#define  MSI_ADDR_REDIRECTION_LOWPRI	(1 << MSI_ADDR_REDIRECTION_SHIFT)
					/* lowest priority */

#define MSI_ADDR_DEST_ID_SHIFT		12
#define	 MSI_ADDR_DEST_ID_MASK		0x00ffff0
#define  MSI_ADDR_DEST_ID(dest)		(((dest) << MSI_ADDR_DEST_ID_SHIFT) & \
					 MSI_ADDR_DEST_ID_MASK)
#define MSI_ADDR_EXT_DEST_ID(dest)	((dest) & 0xffffff00)

#define MSI_ADDR_IR_EXT_INT		(1 << 4)
#define MSI_ADDR_IR_SHV			(1 << 3)
#define MSI_ADDR_IR_INDEX1(index)	((index & 0x8000) >> 13)
#define MSI_ADDR_IR_INDEX2(index)	((index & 0x7fff) << 5)

typedef uint8_t byte;
typedef uint16_t word;

struct device {
  struct device *next;
  struct pci_dev *dev;
  unsigned int config_cnt;
  byte config[256];
};

/* Config space accesses */

static inline byte
get_conf_byte(struct device *d, unsigned int pos)
{
  return d->config[pos];
}

static word
get_conf_word(struct device *d, unsigned int pos)
{
  return d->config[pos] | (d->config[pos+1] << 8);
}

static u32
get_conf_long(struct device *d, unsigned int pos)
{
  return d->config[pos] |
    (d->config[pos+1] << 8) |
    (d->config[pos+2] << 16) |
    (d->config[pos+3] << 24);
}

static void print_msi(u32 address_hi, u32 address_lo, u32 data) {
	u32 interrupt_index;
  printf("\n");
  printf("addr hi %x addr low %x data %x \n",address_hi,address_lo,data);
  printf("remapped =%s\n", (address_lo & MSI_ADDR_IR_EXT_INT) ? "yes" : "no");
  if(address_lo & MSI_ADDR_IR_EXT_INT) {
      printf("SubHandleValid=%s\n", (address_lo & MSI_ADDR_IR_SHV) ? "1" : "0");
      interrupt_index = (address_lo & 0xfffff) >> 5;
      interrupt_index |= ((address_lo & 0x4) << 17);
      if(address_lo & MSI_ADDR_IR_SHV) {
          printf("int_index = %d\n",( interrupt_index + (data & 0xffff) ) );
      } else {
          printf("int_index = %d\n", interrupt_index );
      }
  } else {
       printf("dest_mode=%s redirection=%s dest_id=%u\n",
	 (address_lo & MSI_ADDR_DEST_MODE_LOGICAL) ? "logical" : "physical",
	 (address_lo & MSI_ADDR_REDIRECTION_LOWPRI) ? "lowpri" : "cpu",
	 (address_lo & MSI_ADDR_DEST_ID_MASK) >> MSI_ADDR_DEST_ID_SHIFT);

      printf("trigger=%s level=%s delivery_mode=%s vector=%u\n",
	 (data & MSI_DATA_TRIGGER_LEVEL) ? "level" : "edge",
	 (data & MSI_DATA_LEVEL_ASSERT) ? "assert" : "deassert",
	 (data & MSI_DATA_DELIVERY_LOWPRI) ? "lowpri" : "fixed",
	 data & MSI_DATA_VECTOR_MASK);
 }
}
static void
show_msi(struct device *d, int where, int cap)
{
  int is64;
  u32 address_hi, address_lo;
  u16 data;

  printf("Message Signalled Interrupts: 64bit%c Queue=%d/%d Enable%c\n",
	 FLAG(cap, PCI_MSI_FLAGS_64BIT),
	 (cap & PCI_MSI_FLAGS_QSIZE) >> 4,
	 (cap & PCI_MSI_FLAGS_QMASK) >> 1,
	 FLAG(cap, PCI_MSI_FLAGS_ENABLE));

  is64 = cap & PCI_MSI_FLAGS_64BIT;
  
  if (!pci_read_block(d->dev, where + PCI_MSI_ADDRESS_LO,
		      d->config + where + PCI_MSI_ADDRESS_LO,
		      (is64 ? PCI_MSI_DATA_64 : PCI_MSI_DATA_32) + 2
		      - PCI_MSI_ADDRESS_LO)) {
	  printf("Unable to read 4 bytes of configuration space.");
	  return ;
  }
  
  if (is64) {
	  address_hi = get_conf_long(d, where + PCI_MSI_ADDRESS_HI);
	  data = get_conf_word(d, where + PCI_MSI_DATA_64);
  }else{
	  address_hi = 0;
	  data = get_conf_word(d, where + PCI_MSI_DATA_32);
  }
  address_lo = get_conf_long(d, where + PCI_MSI_ADDRESS_LO);

  print_msi(address_hi, address_lo, data);
}

#define MAP_SIZE 4096

static void
show_msix(struct device *d, int where, int cap, char *argv[])
{
    unsigned int table_bir = 0x0;
    int fd;
    int *ptr;
    char file_name[100];

    u32 address_hi, address_lo;
    u32 data;
    int i=0;

    printf("Message Signalled Interrupts X:  table size=%d Enable%c\n",
	 (cap & PCI_MSIX_TABSIZE) + 1,
	 FLAG(cap, PCI_MSIX_ENABLE));

    if (!pci_read_block(d->dev, where + 0x4,
		      d->config + where + 0x4,
		      8)) {
	  printf("Unable to read 4 bytes of configuration space.");
	  return ;
    }

    table_bir = get_conf_byte(d, where + 0x4) & 0x7;

    sprintf(file_name,"/sys/bus/pci/devices/0000\:%c%c\:%c%c.%c/resource%d",argv[1][0],argv[1][1],argv[1][3],argv[1][4],argv[1][6], table_bir);

    printf("opening %s\n",file_name);
    fd = open(file_name, O_RDWR | O_SYNC);
    if (fd) {
        u32 *mptr;
        ptr = mmap(0, MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

        mptr = ptr;
	for (i=0; i<((cap & PCI_MSIX_TABSIZE) + 1); i++) {
            u32 vector_ctrl_reg;
	    address_lo = *mptr; mptr++;
	    address_hi = *mptr; mptr++;
	    data = *mptr; mptr++;
	    vector_ctrl_reg = *mptr; mptr++;
	    //printf("vcr = %x\n",vector_ctrl_reg);
	    /* check for mask bit*/
	    if(!(vector_ctrl_reg & 0x1))
                print_msi(address_hi, address_lo, data);
	}
        munmap(ptr, MAP_SIZE);
        close(fd);
    }
}

int main(int argc, char **argv)
{
	struct pci_access *pacc;
	struct pci_filter filter;	/* Device filter */
	struct pci_dev *p;
	struct device *d;
	char *msg;
	int found = 0;

	if (argc < 2) {
		printf("more args required\n");
		return -1;
	}
	
	pacc = pci_alloc();
	pci_filter_init(pacc, &filter);
	pci_init(pacc);
	if (msg = pci_filter_parse_slot(&filter, argv[1])) {
		printf("pci_filter_parse_slot %s\n", msg);
		return -1;
	}
	pci_scan_bus(pacc);
	for (p = pacc->devices; p; p = p->next) {
		if (!pci_filter_match(&filter, p))
			continue;
		else {
			found = 1;
			break;
		}
	}

	if (!found) {
		printf("device not found\n");
		return -1;
	}
	d = malloc(sizeof(struct device));
	bzero(d, sizeof(*d));
	d->dev = p;
	if (!pci_read_block(p, 0, d->config, 64)) {
		printf("Unable to read 64 bytes of configuration space.");
		return -1;
	}
	d->config_cnt = 64;
	pci_setup_cache(p, d->config, d->config_cnt);
	pci_fill_info(p, PCI_FILL_IDENT | PCI_FILL_IRQ | PCI_FILL_BASES | PCI_FILL_ROM_BASE | PCI_FILL_SIZES);
	int where = get_conf_byte(d, PCI_CAPABILITY_LIST) & ~3;
	printf("*******************************************\n");
	while (where)
	{
		int id, next, cap;

		if (!pci_read_block(d->dev, where, d->config + where, 4)) {
			printf("Unable to read 4 bytes of configuration space.");
			return -1;
		}
		id = get_conf_byte(d, where + PCI_CAP_LIST_ID);
		next = get_conf_byte(d, where + PCI_CAP_LIST_NEXT) & ~3;
		cap = get_conf_word(d, where + PCI_CAP_FLAGS);
		if (id == 0xff)
		{
			printf("cap chain broken\n");
			break;
		}
		if (id == PCI_CAP_ID_MSI) {
			show_msi(d, where, cap);
			printf("*******************************************\n");
		} else if (id == PCI_CAP_ID_MSIX) {
			show_msix(d, where, cap, argv);
			printf("*******************************************\n");
		}
		where = next;
	}
	pci_cleanup(pacc);

	return 0;
}
