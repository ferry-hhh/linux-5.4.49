#ifndef _BF_H
#define _BF_H
#include "cxl_mem_driver.h"

static long bf_allocate(struct file *fp, struct vm_area_struct *vma, int pid)
{
    struct cxl_mem *cxl_memP = fp->private_data;
    /* Number of requested pages/blocks */
    long reqSize = (vma->vm_end - vma->vm_start) / PAGE_SIZE;

    int surplus; // Record the difference between available memory and required memory
    struct areaNode *temp = kmalloc(sizeof(struct areaNode), GFP_KERNEL);
    struct areaNode *p = cxl_memP->headP->next;
    struct areaNode *q = NULL; // Record the best position

    temp->area.size  = reqSize;
    temp->area.state = BUSY;
    temp->area.pid   = pid;

    // Traverse the list to find the first available free interval and assign it to q
    while (p)
    {
        if (p->area.state == FREE && p->area.size >= reqSize)
        {  
            q = p;
            surplus = p->area.size - reqSize;
            break;
        }
        p = p->next;
    }
    // Continue traversing to find a suitable position
    while (p)
    {   
        // The partition size is exactly the size of the job application
        if (p->area.state == FREE && p->area.size == reqSize)
        {
            p->area.state = BUSY;
            p->area.pid = pid;
            return p->area.offset;
        }
        // The difference between available memory and required memory is smaller
        if (p->area.state == FREE && p->area.size > reqSize)
        {
            if (surplus > p->area.size - reqSize)
            {
                surplus = p->area.size - reqSize;
                q = p;
            }
        }
        p = p->next;
    }
    if (q == NULL)  return -ENOSPC;
    // Found the best position
    else 
    {
        // Insert temp before node q
        temp->next = q;
        temp->prior = q->prior;
        temp->area.offset = q->area.offset;

        q->prior->next = temp;
        q->prior = temp;

        q->area.size = surplus;
        q->area.offset += reqSize;
        
        return temp->area.offset;
    }
}

static int bf_recycle(struct inode *ip, struct file *fp, int pid)
{
    struct cxl_mem *cxl_memP = fp->private_data;
    struct areaNode *p = cxl_memP->headP->next;
    int ret=-EIO;
    while (p)
    {
        if (p->area.pid == pid)
        {   
            ret=EIO;
            p->area.pid = 0;
            p->area.state = FREE;
            // If adjacent to the previous free area, merge them
            if (!p->prior->area.state && p->next->area.state) 
            {
                p->prior->area.size += p->area.size;
                p->prior->next = p->next;
                p->next->prior = p->prior;
            }
            // If adjacent to the following free area, merge them
            if (!p->next->area.state && p->prior->area.state)
            {
                p->area.size += p->next->area.size;
                if (p->next->next)
                {
                    p->next->next->prior = p;
                    p->next = p->next->next;
                }
                else
                    p->next = p->next->next;
            }
            // Both the preceding and following free areas are empty
            if (!p->prior->area.state && !p->next->area.state)
            {
                p->prior->area.size += p->area.size + p->next->area.size;
                if (p->next->next)
                {
                    p->next->next->prior = p->prior;
                    p->prior->next = p->next->next;
                }
                else
                    p->prior->next = p->next->next;
            }
            
        }
        p = p->next;
    }
    return ret;
}

void display(struct cxl_mem *cxl_memP)
{
    struct areaNode *p = cxl_memP->headP->next;
    printk("\t\t\t\t\t%s Memory Allocation Table\n", cxl_memP->dev_name);
    printk("\t\t----------------------------------------------------\n");
    printk("\t\tProcess PID\t\tStart Block Number\t\tPartition Size\t\tState");
    while (p)
    {
        printk("\t\t\t %d\t\t\t\t\t\t\t\t %d\t\t\t\t\t\t %d\t\t\t\t\t%s", p->area.pid, p->area.offset, p->area.size, p->area.state ? "Allocated" : "Free");
        p = p->next;
    }
    printk("\t\t----------------------------------------------------\n");
}

#endif