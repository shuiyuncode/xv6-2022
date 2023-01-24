//
// formatted console output -- printf, panic.
//

#include <stdarg.h>

#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"
#include "proc.h"

volatile int panicked = 0;

// lock to avoid interleaving concurrent printf's.
static struct {
  struct spinlock lock;
  int locking;
} pr;

static char digits[] = "0123456789abcdef";

static void
printint(int xx, int base, int sign)
{
  char buf[16];
  int i;
  uint x;

  if(sign && (sign = xx < 0))
    x = -xx;
  else
    x = xx;

  i = 0;
  do {
    buf[i++] = digits[x % base];
  } while((x /= base) != 0);

  if(sign)
    buf[i++] = '-';

  while(--i >= 0)
    consputc(buf[i]);
}

static void
printptr(uint64 x)
{
  int i;
  consputc('0');
  consputc('x');
  for (i = 0; i < (sizeof(uint64) * 2); i++, x <<= 4)
    consputc(digits[x >> (sizeof(uint64) * 8 - 4)]);
}

// Print to the console. only understands %d, %x, %p, %s.
void
printf(char *fmt, ...)
{
  va_list ap;
  int i, c, locking;
  char *s;

  locking = pr.locking;
  if(locking)
    acquire(&pr.lock);

  if (fmt == 0)
    panic("null fmt");

  va_start(ap, fmt);
  for(i = 0; (c = fmt[i] & 0xff) != 0; i++){
    if(c != '%'){
      consputc(c);
      continue;
    }
    c = fmt[++i] & 0xff;
    if(c == 0)
      break;
    switch(c){
    case 'd':
      printint(va_arg(ap, int), 10, 1);
      break;
    case 'x':
      printint(va_arg(ap, int), 16, 1);
      break;
    case 'p':
      printptr(va_arg(ap, uint64));
      break;
    case 's':
      if((s = va_arg(ap, char*)) == 0)
        s = "(null)";
      for(; *s; s++)
        consputc(*s);
      break;
    case '%':
      consputc('%');
      break;
    default:
      // Print unknown % sequence to draw attention.
      consputc('%');
      consputc(c);
      break;
    }
  }
  va_end(ap);

  if(locking)
    release(&pr.lock);
}

// 参考 https://mit-public-courses-cn-translatio.gitbook.io/mit6-s081/lec05-calling-conventions-and-stack-frames-risc-v/5.5-stack
// Your backtrace() will need a way to recognize that it has seen the last stack frame, 
// and should stop. A useful fact is that the memory allocated for each kernel stack consists 
// of a single page-aligned page, so that all the stack frames for a given stack are on the same page.
// You can use PGROUNDDOWN(fp) (see kernel/riscv.h) to identify the page that a frame pointer
// refers to.
void 
backtrace(void)
{

  uint64 fp = r_fp(); // 注意这里得到的是r_fp函数的 fp

  printf("fp = %p\n",fp);   // fp is va

  struct proc* p = myproc();
  // printf("kernel stack = %p\n",p->kstack);   // fp is va

  // extern pagetable_t kernel_pagetable;
  // pte_t* pte = walk(kernel_pagetable, fp, 0);
  // uint64 pa = PTE2PA(*pte) + OFFSET(fp);
  // printf("pa=%p\n",pa);   // fp is va

  uint64 stack = p->kstack;    // 为什么不用kstack 来处理边界
     printf("stack = %p\n",stack);   // fp is va

  uint64 boundary = PGROUNDDOWN(fp);  // 因为用的是 PGROUNDDOWN 所以要用物理地址作为参数
  if (fp <= boundary)
  {
    // uint64 ret_addr = fp - 8;
    // // walkaddr(p->pagetable, )
    // printf("-------%p\n", ret_addr);
    // fp = (uint64)(*(uint64*) (fp -16));
  }
}


void
panic(char *s)
{
  pr.locking = 0;
  printf("panic: ");
  printf(s);
  printf("\n");

  backtrace(); // print call stack

  panicked = 1; // freeze uart output from other CPUs
  for(;;)
    ;
}

void
printfinit(void)
{
  initlock(&pr.lock, "pr");
  pr.locking = 1;
}
