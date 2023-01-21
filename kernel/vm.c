#include "param.h"
#include "types.h"
#include "memlayout.h"
#include "elf.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"

/*
 * the kernel's page table.
 */
pagetable_t kernel_pagetable;

extern char etext[];  // kernel.ld sets this to end of kernel code.

extern char trampoline[]; // trampoline.S

// 恒等映射
// Make a direct-map page table for the kernel.
pagetable_t
kvmmake(void)
{
  // pagetable_t代表一级页表，实际数据类型是一个指针，指向页表的物理地址
  pagetable_t kpgtbl;

  kpgtbl = (pagetable_t) kalloc();
  memset(kpgtbl, 0, PGSIZE);

  // 在申请到页表后，通过调用kvmmap函数，将物理地址中的UART0 CLINT等映射到内核页表中，完成了内核页表的初始化。
  // 低于 0x8000 0000 io设备的恒等映射(kvmmap 的前两个参数一样)
  // uart registers
  kvmmap(kpgtbl, UART0, UART0, PGSIZE, PTE_R | PTE_W);

  // virtio mmio disk interface
  kvmmap(kpgtbl, VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);

  // PLIC
  kvmmap(kpgtbl, PLIC, PLIC, 0x400000, PTE_R | PTE_W);

  // map kernel text executable and read-only.
  kvmmap(kpgtbl, KERNBASE, KERNBASE, (uint64)etext-KERNBASE, PTE_R | PTE_X);

  // map kernel data and the physical RAM we'll make use of.
  kvmmap(kpgtbl, (uint64)etext, (uint64)etext, PHYSTOP-(uint64)etext, PTE_R | PTE_W);

  // map the trampoline for trap entry/exit to
  // the highest virtual address in the kernel.
  kvmmap(kpgtbl, TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);

  // allocate and map a kernel stack for each process.
  proc_mapstacks(kpgtbl);
  
  return kpgtbl;
}

// Initialize the one kernel_pagetable
void
kvminit(void)
{
  kernel_pagetable = kvmmake();
}

// Switch h/w page table register to the kernel's page table,
// and enable paging.
void
kvminithart()
{
  // wait for any previous writes to the page table memory to finish.
  sfence_vma();

  // 使用MAKE_SATP产生SATP的值，将该值写入satp寄存器中
  // 在MAKE_SATP中使用SATP_SV39设置 MODE 域为 8，即开启 SV39 地址转换
  w_satp(MAKE_SATP(kernel_pagetable));

  // flush stale entries from the TLB.
  // 使用sfence_vma刷新 TLB，完成了虚拟地址转换的开启，之后代码中的地址就全部会通过地址转换机构进行转换
  sfence_vma();
}


// walk函数是最核心的函数，该函数通过页表pagetable将虚拟地址va转换为PTE，如果alloc为1就会分配一个新页面。
// Return the address of the PTE in page table pagetable
// that corresponds to virtual address va.  If alloc!=0,
// create any required page-table pages.
//
// The risc-v Sv39 scheme has three levels of page-table
// pages. A page-table page contains 512 64-bit PTEs.
// A 64-bit virtual address is split into five fields:
//   39..63 -- must be zero.
//   30..38 -- 9 bits of level-2 index.
//   21..29 -- 9 bits of level-1 index.
//   12..20 -- 9 bits of level-0 index.
//    0..11 -- 12 bits of byte offset within the page.
pte_t *
walk(pagetable_t pagetable, uint64 va, int alloc)
{
  if(va >= MAXVA)
    panic("walk");

  for(int level = 2; level > 0; level--) {
    // 通过虚拟地址 va 得到各个级别的pte
    pte_t *pte = &pagetable[PX(level, va)];
    // 需要注意的地方是 for 循环中 pagetable 是更新的
    if(*pte & PTE_V) {
      pagetable = (pagetable_t)PTE2PA(*pte);// 如果已存在了 pagetable 更新为下一级的 pagetable 地址值
    } else {
      if(!alloc || (pagetable = (pde_t*)kalloc()) == 0)// 如果不存在的话 先分配新空间 再赋值给 pagetable
        return 0;
      memset(pagetable, 0, PGSIZE);
      *pte = PA2PTE(pagetable) | PTE_V;
    }
  }
  return &pagetable[PX(0, va)];
}

// Look up a virtual address, return the physical address, or 0 if not mapped.
// Can only be used to look up user pages.
// !!!va is virtual address!!!
uint64
walkaddr(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  uint64 pa;

  if(va >= MAXVA)
    return 0;

  pte = walk(pagetable, va, 0);
  if(pte == 0)
    return 0;
  if((*pte & PTE_V) == 0)
    return 0;
  if((*pte & PTE_U) == 0)
    return 0;
  pa = PTE2PA(*pte);
  return pa;
}

// add a mapping to the kernel page table.
// only used when booting.
// does not flush TLB or enable paging.
void
kvmmap(pagetable_t kpgtbl, uint64 va, uint64 pa, uint64 sz, int perm)
{
  if(mappages(kpgtbl, va, sz, pa, perm) != 0)
    panic("kvmmap");
}

// Create PTEs for virtual addresses starting at va that refer to
// physical addresses starting at pa. va and size might not
// be page-aligned. Returns 0 on success, -1 if walk() couldn't
// allocate a needed page-table page.
int
mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, int perm)
{
  uint64 a, last;
  pte_t *pte;

  if(size == 0)
    panic("mappages: size");
  
  a = PGROUNDDOWN(va);
  last = PGROUNDDOWN(va + size - 1);
  for(;;){ // 通过 walk 构建 PTES
    if((pte = walk(pagetable, a, 1)) == 0)
      return -1;
    if(*pte & PTE_V)
      panic("mappages: remap");
    *pte = PA2PTE(pa) | perm | PTE_V;
    if(a == last)
      break;
    a += PGSIZE;
    pa += PGSIZE;
  }
  return 0;
}

// Remove npages of mappings starting from va. va must be
// page-aligned. The mappings must exist.
// Optionally free the physical memory.
void
uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free)
{
  uint64 a;
  pte_t *pte;

  if((va % PGSIZE) != 0)
    panic("uvmunmap: not aligned");

  for(a = va; a < va + npages*PGSIZE; a += PGSIZE){
    if((pte = walk(pagetable, a, 0)) == 0)
      panic("uvmunmap: walk");
    if((*pte & PTE_V) == 0)
      panic("uvmunmap: not mapped");
    if(PTE_FLAGS(*pte) == PTE_V)
      panic("uvmunmap: not a leaf");
    if(do_free){
      uint64 pa = PTE2PA(*pte);
      kfree((void*)pa);
    }
    *pte = 0;
  }
}

// create an empty user page table.
// returns 0 if out of memory.
pagetable_t
uvmcreate()
{
  pagetable_t pagetable;
  pagetable = (pagetable_t) kalloc();// 分配一个物理页面
  if(pagetable == 0)
    return 0;
  memset(pagetable, 0, PGSIZE);
  return pagetable;
}

// Load the user initcode into address 0 of pagetable,
// for the very first process.
// sz must be less than a page.
void
uvmfirst(pagetable_t pagetable, uchar *src, uint sz)
{
  char *mem;

  if(sz >= PGSIZE)
    panic("uvmfirst: more than a page");
  mem = kalloc();
  memset(mem, 0, PGSIZE);
  mappages(pagetable, 0, PGSIZE, (uint64)mem, PTE_W|PTE_R|PTE_X|PTE_U);
  memmove(mem, src, sz);
}

// Allocate PTEs and physical memory to grow process from oldsz to
// newsz, which need not be page aligned.  Returns new size or 0 on error.

// uvmalloc函数先计算需要申请的页面数，之后在进程地址空间顶部再申请所需的连续的页面。
// 函数通过kalloc申请物理页面，之后使用mappages函数映射到进程页表中。
uint64
uvmalloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz, int xperm)
{
  char *mem;
  uint64 a;

  if(newsz < oldsz)
    return oldsz;

  oldsz = PGROUNDUP(oldsz);
  for(a = oldsz; a < newsz; a += PGSIZE){
    mem = kalloc();
    if(mem == 0){
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
    memset(mem, 0, PGSIZE);
    if(mappages(pagetable, a, PGSIZE, (uint64)mem, PTE_R|PTE_U|xperm) != 0){
      kfree(mem);
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
  }
  return newsz;
}

// Deallocate user pages to bring the process size from oldsz to
// newsz.  oldsz and newsz need not be page-aligned, nor does newsz
// need to be less than oldsz.  oldsz can be larger than the actual
// process size.  Returns the new process size.

// uvmdealloc函数先计算需要减少的页面数，之后通过uvmunmap删除页面。
// 在uvmunmap函数内部通过walk获取对应 PTE，将PTE_V设置为0，最后通过kfree函数将该物理页面添加到空闲链表中。
uint64
uvmdealloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
  if(newsz >= oldsz)
    return oldsz;

  if(PGROUNDUP(newsz) < PGROUNDUP(oldsz)){
    int npages = (PGROUNDUP(oldsz) - PGROUNDUP(newsz)) / PGSIZE;
    uvmunmap(pagetable, PGROUNDUP(newsz), npages, 1);
  }

  return newsz;
}

// Recursively free page-table pages.
// All leaf mappings must already have been removed.
void
freewalk(pagetable_t pagetable)
{
  // there are 2^9 = 512 PTEs in a page table.
  for(int i = 0; i < 512; i++){
    pte_t pte = pagetable[i];
    if((pte & PTE_V) && (pte & (PTE_R|PTE_W|PTE_X)) == 0){
      // this PTE points to a lower-level page table.
      uint64 child = PTE2PA(pte);
      freewalk((pagetable_t)child);
      pagetable[i] = 0;
    } else if(pte & PTE_V){
      panic("freewalk: leaf");
    }
  }
  kfree((void*)pagetable);
}

// Free user memory pages,
// then free page-table pages.
void
uvmfree(pagetable_t pagetable, uint64 sz)
{
  if(sz > 0)
    uvmunmap(pagetable, 0, PGROUNDUP(sz)/PGSIZE, 1);
  freewalk(pagetable);
}

// Given a parent process's page table, copy
// its memory into a child's page table.
// Copies both the page table and the
// physical memory.
// returns 0 on success, -1 on failure.
// frees any allocated pages on failure.
int
uvmcopy(pagetable_t old, pagetable_t new, uint64 sz)
{
  pte_t *pte;
  uint64 pa, i;
  uint flags;
  char *mem;

  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walk(old, i, 0)) == 0)
      panic("uvmcopy: pte should exist");
    if((*pte & PTE_V) == 0)
      panic("uvmcopy: page not present");
    pa = PTE2PA(*pte);
    flags = PTE_FLAGS(*pte);
    if((mem = kalloc()) == 0)
      goto err;
    memmove(mem, (char*)pa, PGSIZE);
    if(mappages(new, i, PGSIZE, (uint64)mem, flags) != 0){
      kfree(mem);
      goto err;
    }
  }
  return 0;

 err:
  uvmunmap(new, 0, i / PGSIZE, 1);
  return -1;
}

// mark a PTE invalid for user access.
// used by exec for the user stack guard page.
void
uvmclear(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  
  pte = walk(pagetable, va, 0);
  if(pte == 0)
    panic("uvmclear");
  *pte &= ~PTE_U;
}

// Copy from kernel to user.
// Copy len bytes from src to virtual address dstva in a given page table.
// Return 0 on success, -1 on error.
int
copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len)
{
  uint64 n, va0, pa0;

  while(len > 0){
    va0 = PGROUNDDOWN(dstva);// dstva = 20464
    pa0 = walkaddr(pagetable, va0);// pa0 = 2280996864 va = 16384
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (dstva - va0);    // n = 16
    if(n > len)                    // len = 3  这 注意这是在 while 中的
      n = len;
    memmove((void *)(pa0 + (dstva - va0)), src, n);

    len -= n;
    src += n;
    dstva = va0 + PGSIZE;
  }
  return 0;
}

// Copy from user to kernel.
// Copy len bytes to dst from virtual address srcva in a given page table.
// Return 0 on success, -1 on error.
int
copyin(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len)
{
  uint64 n, va0, pa0;

  while(len > 0){
    va0 = PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (srcva - va0);
    if(n > len)
      n = len;
    memmove(dst, (void *)(pa0 + (srcva - va0)), n);

    len -= n;
    dst += n;
    srcva = va0 + PGSIZE;
  }
  return 0;
}

// Copy a null-terminated string from user to kernel.
// Copy bytes to dst from virtual address srcva in a given page table,
// until a '\0', or max.
// Return 0 on success, -1 on error.
int
copyinstr(pagetable_t pagetable, char *dst, uint64 srcva, uint64 max)
{
  uint64 n, va0, pa0;
  int got_null = 0;

  while(got_null == 0 && max > 0){
    va0 = PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (srcva - va0);
    if(n > max)
      n = max;

    char *p = (char *) (pa0 + (srcva - va0));
    while(n > 0){
      if(*p == '\0'){
        *dst = '\0';
        got_null = 1;
        break;
      } else {
        *dst = *p;
      }
      --n;
      --max;
      p++;
      dst++;
    }

    srcva = va0 + PGSIZE;
  }
  if(got_null){
    return 0;
  } else {
    return -1;
  }
}


/**
 * print the first process's page table
 * 
 * page_table is a physical addresses
 * 
 * 
 * page table 0x0000000087f6b000
 * ..  0: pte 0x0000000021fd9c01 pa 0x0000000087f67000
 * ..    ..0: pte 0x0000000021fd9801 pa 0x0000000087f66000
 * ..    .. ..0: pte 0x0000000021fda01b pa 0x0000000087f68000
 * ..    .. ..1: pte 0x0000000021fd9417 pa 0x0000000087f65000
 * ..    .. ..2: pte 0x0000000021fd9007 pa 0x0000000087f64000
 * ..    .. ..3: pte 0x0000000021fd8c17 pa 0x0000000087f63000
 * ..255: pte 0x0000000021fda801 pa 0x0000000087f6a000   // 这里第1级的 pte 完全可以使用相同的 物理地址
 * ..  ..511: pte 0x0000000021fda401 pa 0x0000000087f69000
 * ..   .. ..509: pte 0x0000000021fdcc13 pa 0x0000000087f73000
 * ..   .. ..510: pte 0x0000000021fdd007 pa 0x0000000087f74000
 * ..   .. ..511: pte 0x0000000020001c0b pa 0x0000000080007000
 * 
 * page_table --> satp register value
 * 
 * 打印的 pa 是页表的物理地址 共有3个页面 对应3个物理地址
 * 
 */
void 
vmprint(pagetable_t pagetable)
{
  printf("page table %p\n", pagetable);
  // 每个 page table 512个pte  pagetable_t 是一个uint64* 指针
  pagetable_t pg2 = pagetable, pg1, pg0;
  for (int L2 = 0; L2 < 512; L2++)
  {
    pte_t *pte2 = &pg2[L2];// there is not &pg2[L2 * 8]
    if (*pte2 & PTE_V)
    {
      // printf("..%d: pte %p pa %p\n", L2, *pte2, pg2 + L2 );
      printf("..%d: pte %p pa %p\n", L2, *pte2, PTE2PA(*pte2));
      pg1 = (pagetable_t)PTE2PA(*pte2);
      for (int L1 = 0; L1 < 512; L1++)
      {
        pte_t *pte1 = &pg1[L1];
        if (*pte1 & PTE_V)
        {
          // printf(".. ..%d: pte %p pa %p\n", L1, *pte1, pg1 + L1);
          printf(".. ..%d: pte %p pa %p\n", L1, *pte1, PTE2PA(*pte1) );
          pg0 = (pagetable_t)PTE2PA(*pte1);
          for (int L0 = 0; L0 < 512; L0++)
          {
            pte_t *pte0 = &pg0[L0];
            if (*pte0 & PTE_V)
              // printf(".. .. ..%d: pte %p pa %p\n", L0, *pte0, pg0 + L0);
              printf(".. .. ..%d: pte %p pa %p\n", L0, *pte0, PTE2PA(*pte0));
          }
        }
      }
    }
  }
}

// 跟实验讲义上 pa 地址没对上  pte地址对上了  应该是写的没错
// page table 0x0000000087f6b000
//  ..0: pte 0x0000000021fd9c01 pa 0x0000000087f67000
//  .. ..0: pte 0x0000000021fd9801 pa 0x0000000087f66000
//  .. .. ..0: pte 0x0000000021fda01b pa 0x0000000087f68000
//  .. .. ..1: pte 0x0000000021fd9417 pa 0x0000000087f65000
//  .. .. ..2: pte 0x0000000021fd9007 pa 0x0000000087f64000
//  .. .. ..3: pte 0x0000000021fd8c17 pa 0x0000000087f63000
//  ..255: pte 0x0000000021fda801 pa 0x0000000087f6a000
//  .. ..511: pte 0x0000000021fda401 pa 0x0000000087f69000
//  .. .. ..509: pte 0x0000000021fdcc13 pa 0x0000000087f73000
//  .. .. ..510: pte 0x0000000021fdd007 pa 0x0000000087f74000
//  .. .. ..511: pte 0x0000000020001c0b pa 0x0000000080007000