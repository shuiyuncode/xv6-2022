#include "types.h"
#include "riscv.h"
#include "param.h"
#include "defs.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  argaddr(0, &p);
  return wait(p);
}

// 用于增加或减少物理内存，当参数为正数时增加，负数时减少
// sbrk实际通过growproc进行，growproc调用uvmalloc 或uvmdealloc完成工作。
uint64
sys_sbrk(void)
{
  uint64 addr;
  int n;

  argint(0, &n);
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;


  argint(0, &n);
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(killed(myproc())){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

#ifdef LAB_PGTBL
int
sys_pgaccess(void)
{
  // lab pgtbl: your code here.
  uint64 va;      // first arg  在 pgtbltest 中 print("%p\n", buf); --> 0x5010  内核中得到 va_addr = 20496
                  // 不是去va地址值处的值 va 地址处的值为 buf[PGSIZE * 1] += 1;(pgtbltest.c)
  int size;       // second arg
  uint64 addr;    // third arg user pointer to arg of abits

  argaddr(0, &va);
  argint(1, &size);
  argaddr(2, &addr);
  pagetable_t pagetable = myproc()->pagetable;

  // printf("pgtbltest-----\n");
  // vmprint(pagetable);

  if (size > MAXACCESS || size < 1)
    return -1;

  int bitmask = 0;         // 这里是终极大坑  不能用uint64 草！！！  应为调用参数size为int 如果用 uint64 在执行copyout时 报错
  // va = PGROUNDDOWN(va); // 求pte是以 PGSIZE单位的 会自动过滤va的offset 所以不需要页面向下取整  
  for (int i = 0; i < size && va < MAXVA; i++)
  {
    pte_t *pte = walk(pagetable, va, 0);
    if (pte !=0 && *pte & PTE_A)
    {
      bitmask = bitmask | (1 << i);
      *pte &= ~PTE_A; // 清除ACCESSED标志位
    }
    va += 4096;
  }
  
  // 参考 int fstat(int fd, struct stat *st) Place info about an open file into *st
  // sysfile 中 copyout 函数原型
  if (copyout(pagetable, addr, (char *)&bitmask, sizeof(bitmask)) < 0)
  {
    return -1;
  }
  return 0;
}
#endif



uint64
sys_kill(void)
{
  int pid;

  argint(0, &pid);
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}
