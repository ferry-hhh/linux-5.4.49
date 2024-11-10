#ifndef _CXL_MEM_DRIVER_H
#define _CXL_MEM_DRIVER_H

/* Device numbers (major and minor) allocated for the device */
#define DEVICE_MAJOR               500
#define DEVICE_MINOR               0
#define DEVICE_NR                  1
#define MM_SIZE                    4096
/* Vendor ID and Device ID of the device */
#define CXL_MEM_VENDOR_ID   0x8086
#define CXL_MEM_DEVICE_ID   0x7890
/* Define the maximum number of devices */
#define MAX_NRs             8
/* The position of the BAR space register to be read */
#define BAR_POS             0
/* Define the status of a continuous memory region */
#define BUSY true
#define FREE false

#define KB 1024
#define MB (1024*KB) 

int cur_count=0;
struct cxl_mem* cxl_memPs[MAX_NRs] = {NULL};

/**
 * Composition of ioctlCmd:
 * Device type 8bit | Sequence number 8bit | Direction 2bit User<->Kernel | Data size 8-14bit
 */


/* Define the magic number, representing a class of devices */
#define CXL_MEM_TYPE '0xF7'

/* Obtain the current resource information */
#define CXL_MEM_GET_INFO _IO(CXL_MEM_TYPE,0)

/* Memory region structure */
struct area
{
    int pid;    // Partition number
    int size;   // Partition size
    int offset; // Offset/address
    bool state; // Usage state, BUSY(true) means occupied, FREE(false) means free
};

/* Node in the list representing a region, containing forward and backward pointers */
struct areaNode
{
    struct area area;
    struct areaNode *prior;
    struct areaNode *next;
};

static struct pci_device_id ids[] = {
   {PCI_DEVICE(CXL_MEM_VENDOR_ID,CXL_MEM_DEVICE_ID) },
   {PCI_DEVICE(0x10ec,0x8852)},
   { 0,}  // The last group is 0, indicating the end.

};

struct bar_info
{
   resource_size_t base;
   resource_size_t len;
   long flags;
};

struct cxl_mem
{
    char dev_name[20];
	struct pci_dev  *pci_dev;
	struct 	cdev 	cdev;
	int    		    devno;
    struct class 	*cxl_mem_class;
    struct bar_info *bar_infoP;
    unsigned int irq;
    struct areaNode* headP;
    struct areaNode* tailP;
    struct mutex mtx;
};

#endif
