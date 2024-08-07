// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem[NCPU]; // 为每个 CPU 分配一个内存链表

void
kinit()
{
  // 为每个 CPU 初始化内存锁
  for (int i = 0; i < NCPU; i++) 
    initlock(&kmem[i].lock, "kmem");
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  // 释放内存到当前 CPU 的链表中
  push_off();
  int hartid = cpuid();
  acquire(&kmem[hartid].lock);
  r->next = kmem[hartid].freelist;
  kmem[hartid].freelist = r;
  release(&kmem[hartid].lock);
  pop_off();
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

//-------------------------------
  push_off();

  // 从当前 CPU 的内存链表中获取内存
  int hartid = cpuid();

  acquire(&kmem[hartid].lock);
  r = kmem[hartid].freelist;
  if(r)
    kmem[hartid].freelist = r->next;
  release(&kmem[hartid].lock);

  // 当前 CPU 的内存链表为空就从其他 CPU 的内存链表里获取内存
  // 释放当前 CPU 内存锁后再去获取其他 CPU 内存锁，防止死锁
  for (int i = 0; i < NCPU && !r; i++) {
    if (i == hartid) continue;
    acquire(&kmem[i].lock);
    r = kmem[i].freelist;
    if (r) 
      kmem[i].freelist = r->next;
    release(&kmem[i].lock);
  }
  
  pop_off();
//--------------------------------

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
