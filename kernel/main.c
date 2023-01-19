#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"

volatile static int started = 0;

// start() jumps here in supervisor mode on all CPUs.
void
main()
{
  if(cpuid() == 0){
    consoleinit();
    printfinit();
    printf("\n");
    printf("xv6 kernel is booting\n");
    printf("\n");
    kinit();         // physical page allocator
    
    // kvminit函数用于初始化内核页表，该函数在内核启动开启分页机制前被调用，
    // 因此是直接对物理地址进行操作的。函数首先通过kalloc申请了一个页面用于保存一级页表。
    // kalloc函数就简单地从kmem.freelist中取出一个空闲页面。
    kvminit();       // create kernel page table 
   
    kvminithart();   // turn on paging
    
    procinit();      // process table

    trapinit();      // trap vectors 向量
    trapinithart();  // install kernel trap vector
   
    plicinit();      // set up interrupt controller
    plicinithart();  // ask PLIC for device interrupts
    binit();         // buffer cache
    iinit();         // inode table
    fileinit();      // file table
    virtio_disk_init(); // emulated hard disk
    
    // 初始化所有进程结构体，对每个进程结构体申请两个页面作为内核栈，
    // 之后将该页面映射到内核地址空间的高位上。最后再次调用kvminithart函数，
    // 刷新 TLB，使硬件知道新 PTEs 的加入，防止使用旧的 TLB 项
    userinit();      // first user process
   
    __sync_synchronize();
    started = 1;
  } else {
    while(started == 0)
      ;
    __sync_synchronize();
    printf("hart %d starting\n", cpuid());
    kvminithart();    // turn on paging
    trapinithart();   // install kernel trap vector
    plicinithart();   // ask PLIC for device interrupts
  }

  scheduler();        
}
