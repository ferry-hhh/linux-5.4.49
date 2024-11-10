#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/signal.h>
#include <linux/init.h>
#include <linux/cdev.h>
#include <linux/delay.h>
#include <asm-generic/ioctl.h>
#include <linux/device.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/uaccess.h>
#include <asm/current.h>
#include <linux/string.h>
#include<linux/mutex.h>
#include "cxl_mem_driver.h"
#include "bf.h"
MODULE_DEVICE_TABLE(pci, ids);

static int device_open(struct inode *ip, struct file *fp)
{
    /**
    * There may be multiple devices of the same type, and the driver needs to know which device the user is operating on;
    * container_of(): Obtain the address of the entire structure based on the address of a member within the structure;
    * @ptr: The address of the known structure member.
    * @type: The type of the structure to be obtained.
    * @member: The name of the known structure member.
    */
    struct cxl_mem *dev = container_of(ip->i_cdev, struct cxl_mem, cdev);
    fp->private_data = dev;
    return 0;
}
/**
 * When the application closes the device file, it needs to free the allocated memory and modify the list.
 */
static int device_release(struct inode *ip, struct file *fp)
{   
    struct cxl_mem *p=fp->private_data;

    int pid = current->pid;

    if(mutex_lock_interruptible(&p->mtx))   //-EINTR
		return -ERESTARTSYS;
    if(bf_recycle(ip, fp, pid)>=0){
        printk("Memory allocated by process %d on device %s has been freed!\n",pid,p->dev_name);
        display(p);
    }
    mutex_unlock(&p->mtx);

    return 0;
}

static int device_mmap(struct file *fp, struct vm_area_struct *vma)
{
    int pid = current->pid;
    struct cxl_mem *p;
    int ret = 0;
    p = fp->private_data;
    vma->vm_flags |= VM_IO | VM_SHARED | VM_DONTEXPAND | VM_DONTDUMP; // reserved memory area
    if(mutex_lock_interruptible(&p->mtx))   //-EINTR
		return -ERESTARTSYS;
    /* Call the memory allocation function, returning the starting address of the allocated contiguous space */
    long offset=bf_allocate(fp, vma, pid);
    mutex_unlock(&p->mtx);

    printk("Process %d is requesting %ldKB of memory resources......\n", pid,(vma->vm_end - vma->vm_start)/KB);
    if(offset<0){
        printk("Allocation failed, no available space!\n");
        return offset;
    }
    else{
        resource_size_t phyAddr=offset* PAGE_SIZE + p->bar_infoP->base;
        ret = remap_pfn_range(vma,                           // Map the virtual memory space
                              vma->vm_start,                 // Starting address of the mapped virtual memory space
                              phyAddr >> PAGE_SHIFT,         // Page frame number corresponding to physical memory, physical address right-shifted by 12 bits
                              (vma->vm_end - vma->vm_start), // Size of the mapped virtual memory space, in multiples of page size
                              vma->vm_page_prot);            // Protection attributes

        printk("Memory has been allocated for process %d on %s device, starting at address 0x%llx, starting device memory block is %ld.\n", pid,p->dev_name,phyAddr,offset);
        display(p);
        return ret;            
    }
}

/* Currently no application scenario */
static long device_ioctl(struct file *fp, unsigned int cmd, unsigned long arg)
{
    switch (cmd)
    {
    case CXL_MEM_GET_INFO:
        break;
    default:
        return -EINVAL;
    }
    return 0;
}

static struct file_operations cxl_mem_ops = {
    .owner = THIS_MODULE,
    .unlocked_ioctl = device_ioctl,
    .open = device_open,
    .mmap = device_mmap,
    .release = device_release,
};

static void init_linklist(struct cxl_mem *p)
{
    p->headP = kmalloc(sizeof(struct areaNode), GFP_KERNEL);
    p->tailP = kmalloc(sizeof(struct areaNode), GFP_KERNEL);

    p->headP->prior = NULL;
    p->headP->next = p->tailP;
    p->tailP->prior = p->headP;
    p->tailP->next = NULL;
    p->headP->area.pid = -1;
    // The first node will not be used and is defined as occupied to prevent partition merging failure.
    p->headP->area.state = BUSY;
    p->tailP->area.offset = 0;
    // Allocate memory in units of pages.
    p->tailP->area.size = p->bar_infoP->len / PAGE_SIZE;
    p->tailP->area.pid = -1;
    p->tailP->area.state = FREE;
}

static int pci_device_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
    printk("Detected the %d-th cxl_mem device: \n",cur_count);
    
    /* Retrieve device PCI information */
    int i, result;
    struct cxl_mem *cxl_memP = kmalloc(sizeof(struct cxl_mem), GFP_KERNEL);
    /* Set the device name */
    sprintf(cxl_memP->dev_name, "cxl_mem%d", cur_count);
    if (pci_enable_device(pdev))
    {
        printk("IO pci_enable_device()failed.\n");
        return -EIO;
    }
    cxl_memP->pci_dev = pdev;
    cxl_memP->irq = pdev->irq;
    /* Dynamically allocate space to store BAR information */
    struct bar_info *bar_infoP = kmalloc(sizeof(struct bar_info) , GFP_KERNEL);
    
    bar_infoP->base = pci_resource_start(pdev, BAR_POS);
    bar_infoP->len = pci_resource_end(pdev, BAR_POS) - bar_infoP->base + 1;
    bar_infoP->flags = pci_resource_flags(pdev, BAR_POS);
    printk("\t\tDevice memory base address: 0x%llx Device memory size: 0x%llx (%lldMB)", bar_infoP->base, bar_infoP->len,bar_infoP->len/MB);
    printk("\t\tThe device memory space is %s io\n", (bar_infoP->flags & IORESOURCE_MEM) ? "mem" : "port");
    
    cxl_memP->bar_infoP = bar_infoP;
    cxl_memP->pci_dev = pdev;
    /* Mark the PCI region to indicate that the area has been allocated */
    if (unlikely(pci_request_regions(pdev, cxl_memP->dev_name)))
    {
        printk("failed:pci_request_regions\n");
        return -EIO;
    }
    pci_set_drvdata(pdev, cxl_memP);

    /* Create a character device file for each device */
    cxl_memP->devno = MKDEV(DEVICE_MAJOR, cur_count);
 
    /* 1. Register device numbers */
    result = register_chrdev_region(cxl_memP->devno, 1, cxl_memP->dev_name);
    if (result < 0)
    {
        printk("register_chrdev_region fail\n");
        return result;
    }
    printk("\t\tRegistered major device number: %d, minor device number: %d\n",MAJOR(cxl_memP->devno),MINOR(cxl_memP->devno));
    /* 2. Establish a connection between cdev and file_operations */
    cdev_init(&cxl_memP->cdev, &cxl_mem_ops);
    /* 3. Add a cdev to the system to complete the registration */
    result = cdev_add(&cxl_memP->cdev, cxl_memP->devno, 1);
    if (result < 0)
    {
        printk("cdev_add fail!\n");
        unregister_chrdev_region(cxl_memP->devno, 1);
        return result;
    }
     /* 4. Create a device node under /dev */
    cxl_memP->cxl_mem_class = class_create(THIS_MODULE, "cxl_mem_class");
    if (IS_ERR(cxl_memP->cxl_mem_class))
    {
        printk(KERN_ERR "class_create()failed\n");
        result = PTR_ERR(cxl_memP->cxl_mem_class);
        return result;
    }

    device_create(cxl_memP->cxl_mem_class, NULL, cxl_memP->devno, NULL, cxl_memP->dev_name);
    printk("\t\tThe device file /dev/cxl_mem%d has been created.\n",cur_count);
    /* 5. Initialize the memory allocation list for device files */
    init_linklist(cxl_memP);
    display(cxl_memP);
    /* 6. Initialize the mutex */
    mutex_init(&cxl_memP->mtx);   
    cxl_memPs[cur_count] = cxl_memP;
    cur_count++;
    return result;
}
/* Called when the PCI driver is unregistered or a hot-pluggable device is removed */
static void pci_device_remove(struct pci_dev *pdev)
{
    pci_release_regions(pdev);
    pci_disable_device(pdev);
    printk("The PCI device driver has been removed!\n");
}

static struct pci_driver pci_driver = {
    .name = "cxl_mem driver",
    .id_table = ids,
    .probe = pci_device_probe,
    .remove = pci_device_remove,
};

static int __init cxl_mem_init(void)
{
    printk("The cxl_mem driver module has been initialized successfully!\n");
    pci_register_driver(&pci_driver);

    return 0;
}
/**
 * 1. Unregister device numbers
 * 2. Unregister pci driver
 */
static void __exit cxl_mem_exit(void)
{
    printk("Driver module is exiting......\n");
    /* Unregister the pci driver */
    pci_unregister_driver(&pci_driver);
    int i;
    for (i = 0; i < cur_count; i++)
    {
        struct cxl_mem *p = cxl_memPs[i];
        device_destroy(p->cxl_mem_class, p->devno);
        class_destroy(p->cxl_mem_class);
        /* Unregister the device and release the device number */
        cdev_del(&p->cdev);
        unregister_chrdev_region(p->devno, 1);
        /* Release resources */
        p->cxl_mem_class = NULL;
        kfree(p);
        p = NULL;
        printk("Resources for cxl_mem%d device have been released......\n", i);
    }
    return;
}

MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("cxl_mem driver");
module_init(cxl_mem_init);
module_exit(cxl_mem_exit);