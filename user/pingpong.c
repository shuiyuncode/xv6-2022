#include "kernel/types.h"
#include "user/user.h"

/**
 * Use pipe to create a pipe.
 * Use fork to create a child.
 * Use read to read from a pipe, and write to write to a pipe.
 * Use getpid to find the process ID of the calling process.
 * Add the program to UPROGS in Makefile.
 * User programs on xv6 have a limited set of library functions available to them. 
 *     You can see the list in user/user.h; the source (other than for system calls) is 
 *     in user/ulib.c, user/printf.c, and user/umalloc.c.
 * 
 * 子进程写入后不能再进行读取了 否则会把刚写入的数据 读走，这样父进程就读取不到数据了。
 * 可以使用条件变量睡眠 然后等父进程读完后再唤醒，不过这仅仅是lab1 没必要搞条件变量
 * 
 * 参考了网友的想法 用两个管道来连接子父进程，这想法无敌 妙哉。
 * 
 * 管道代码修正  read 管道时如果读取不到数据会自动阻塞，不需要使用while来一直读。
 * 
 */
int main(int argc, char *argv[])
{
    //  p[0] is read, p[1] is write
    int p2c[2]; // pipe 会复制到子进程中 复制是引用 与父进程的的p[0] p[1] 相同
    pipe(p2c);  // parent to child
    int c2p[2];
    pipe(c2p);  // child to parent

     char buff[5];// fork之后 字符进程的buff是连个不同的变量
    if (fork() == 0)
    {   // child
        close(p2c[1]);
        read(p2c[0], buff, sizeof(buff));
        close(p2c[0]);
        
        close(c2p[0]);
        printf("%d: received ping\n", getpid());
        write(c2p[1], "pong", 5);
        close(c2p[1]);
        exit(0);
         
    }
    else
    {   // parent
        close(p2c[0]);
        write(p2c[1], "ping", 5);
        close(p2c[1]);

        close(c2p[1]);
        read(c2p[0], buff, sizeof(buff));
        printf("%d: received pong\n", getpid());
        close(c2p[0]);
        exit(0);
    }
}
