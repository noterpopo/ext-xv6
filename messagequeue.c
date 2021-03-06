#include "types.h"
#include "defs.h"
#include "param.h"
#include "mmu.h"
#include "proc.h"
#include "spinlock.h"
#include "sleeplock.h"

struct msg {
    struct msg *next;
    long type;
    char *dataaddr;
    int  dataszie;
};

struct mq {
    int key;
    int status;
    struct msg *msgs;
    int maxbytes;
    int curbytes;
    int refcount;
};

struct spinlock mqlock;
struct spinlock mqrlock;
struct spinlock mqwlock;
struct mq mqs[MQMAX];
struct proc* wqueue[NPROC];
int    wstart=0, wend=0;

struct proc* rqueue[NPROC];
int    rstart=0, rend=0;

void
mqinit()
{
    cprintf("mqinit.\n");
    initlock(&mqlock,"mqlock");
    initlock(&mqrlock,"mqrlock");
    initlock(&mqwlock,"mqwlock");
    for(int i =0;i<MQMAX;++i){
        mqs[i].status = 0;
    }
}

int findkey(int key)
{
    int idx =-1;
    for(int i = 0;i<MQMAX;++i){
        if(mqs[i].status != 0 && mqs[i].key == key){
            idx = i;
            break;
        }
    }
    return idx;
}

int newmq(int key)
{
    //cprintf("newmq.\n");
    int idx =-1;
    for(int i=0;i<MQMAX;++i){
        if(mqs[i].status == 0){
            idx = i;
            break;
        }
    }
    if(idx == -1){
        cprintf("newmq failed: can not get idx.\n");
        return -1;
    }
    mqs[idx].key = key;
    mqs[idx].status = 1;
    mqs[idx].msgs = (struct msg*)kalloc();
    if(mqs[idx].msgs == 0){
        cprintf("newmq failed: can not alloc page.\n");
        return -1;
    }
    memset(mqs[idx].msgs,0,PGSIZE);
    mqs[idx].msgs -> next = 0;
    mqs[idx].msgs -> dataszie = 0;
    mqs[idx].maxbytes = PGSIZE;
    mqs[idx].curbytes = 16;
    mqs[idx].refcount = 1;
    myproc()->mqmask |= 1 << idx;
    return idx;
}

int
mqget(uint key)
{
    //cprintf("mqget: key is %d.\n",key);
    acquire(&mqlock);
    int idx = findkey(key);
    if(idx != -1){
        if(!(myproc()->mqmask >> idx & 1)){
            myproc()->mqmask |= 1 << idx;
            mqs[idx].refcount++;
        }
        release(&mqlock);
        return idx;
    }
    idx = newmq(key);
    release(&mqlock);
    return idx;
}
int
msgsnd(uint mqid, void* msg, int sz)
{
    //cprintf("msgsnd: mqid is %d, msg is %x, sz is %d.\n",mqid,msg,sz);
    if(mqid<0 || MQMAX<=mqid || mqs[mqid].status == 0){
        return -1;
    }

    char *data = (char *)(*((int *) (msg + 4)));
    int  *type = (int *)msg;

    if(mqs[mqid].msgs == 0){
        cprintf("msgsnd failed: msgs == 0.\n");
        return -1;
    }

    acquire(&mqlock);

    while(1){
        if(mqs[mqid].curbytes + sz + 16 <= mqs[mqid].maxbytes){
            struct msg *m = mqs[mqid].msgs;
            while(m->next != 0){
                m = m -> next;
            }
            m->next = (void *)m + m->dataszie + 16;
            m = m -> next;
            m->type = *(type);
            m->next = 0;
            m->dataaddr = (void*)m + 16;
            m->dataszie = sz;
            memmove(m -> dataaddr, data, sz);
            mqs[mqid].curbytes += (sz+16);
            //cprintf("type is %d,msg at %x, dataaddr at %s, datasz is %d.\n",m->type, m,m->dataaddr,m->dataszie);
            while(wstart != wend){
                wakeup(rqueue[rstart]);
                rqueue[rstart] = 0;
                rstart = (rstart +1) % NPROC;
            }
            release(&mqlock);
            return 0;
        } else {
            cprintf("msgsnd: can not alloc: pthread: %d sleep.\n",myproc()->pid);
            wqueue[wend] = myproc();
            wend = (wend + 1) % NPROC;
            sleep(myproc(),&mqlock);
        }
        
    }

    return -1;
}

int reloc(int mqid)
{
    char *pages = (char *)((int)mqs[mqid].msgs);
    struct msg *m  = (struct msg *)pages;
    while (m != 0)
    {
        //cprintf("reloc: pages is %x, m is %x, sz is %d.\n",pages,m,m->dataszie+16);
        memmove(pages,m,m->dataszie+16);
        struct msg *t = m->next;
        ((struct msg *)pages)->next = (struct msg *)(pages + m->dataszie+16);
        pages += m->dataszie+16;
        m = t;
        
    }
    //cprintf("alloc is %d, cur is %d.\n",(int)pages - (int)mqs[mqid].msgs,mqs[mqid].curbytes);
    return 0;
}

int
msgrcv(uint mqid, void* msg, int sz)
{
    //cprintf("msgrcv: mqid is %d, msg is %x, sz is %d.\n",mqid,msg,sz);
    if(mqid<0 || MQMAX<=mqid || mqs[mqid].status ==0){
        return -1;
    }
    int *type = msg;
    int *data = msg + 4;
    acquire(&mqlock);
    
    while(1){
        struct msg *m = mqs[mqid].msgs->next;
        struct msg *pre = mqs[mqid].msgs;
        while (m != 0)
        {
            if(m->type == *type){
                memmove((char *)*data, m->dataaddr, sz);
                pre->next = m->next;
                mqs[mqid].curbytes -= (m->dataszie + 16);
                reloc(mqid);
                while(wstart != wend){
                    wakeup(wqueue[wstart]);
                    wqueue[wstart] = 0;
                    wstart = (wstart +1) % NPROC;
                }
                release(&mqlock);
                return 0;
            }
            pre = m;
            m = m->next;
        }
        cprintf("msgrcv: can not read: pthread: %d sleep.\n",myproc()->pid);
        rqueue[rend] = myproc();
        rend = (rend + 1) % NPROC;
        sleep(myproc(),&mqlock);

    }

    return -1;
}

void
rmmq(int mqid)
{
    //cprintf("rmmq: %d.\n",mqid);
    kfree((char *)mqs[mqid].msgs);
    mqs[mqid].status = 0;
}

void
releasemq(int mask)
{
    //cprintf("releasemq: %x.\n",mask);
    acquire(&mqlock);
    for(int id = 0;id<MQMAX;++id){
        if( mask >> id & 0x1){
            mqs[id].refcount--;
            if(mqs[id].refcount == 0){
                rmmq(id);
            }
        }
    }
    release(&mqlock);
}

void
addmqcount(uint mask)
{
    //cprintf("addcount: %x.\n",mask);
    acquire(&mqlock);
    for (int key = 0; key < MQMAX; key++)
    {
        if(mask >> key & 1){
            mqs[key].refcount++;
        }
    }
    release(&mqlock);
}