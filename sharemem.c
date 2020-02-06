#include "types.h"
#include "defs.h"
#include "param.h"
#include "mmu.h"
#include "proc.h"
#include "spinlock.h"
#include "sleeplock.h"

#define SHMSIZE (4096 - sizeof(struct spinlock) / 4)

struct sharemem
{
    struct spinlock lock;
    void*  vaddr[SHMSIZE];
} *sharedmem;


void
sharememinit()
{
    if((sharedmem = (struct sharemem*)kalloc()) == 0)
        panic("sharememinit failed : Can not alloc kernel page.");
    initlock(&sharedmem->lock, "shm");
    cprintf("shm init finished.\n");
}

void*
shmgetat(uint key, uint num)
{
    cprintf("key is %d, num is %d.\n",key, num);
    return 0;
}


