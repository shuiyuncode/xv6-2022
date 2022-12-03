#include "kernel/types.h"
#include "user/user.h"


/**
 * 主要逻辑
 *     创建1个新的管道
 *     父进程 处理当前管道中数据，凡是第一个数的整数倍的数字过滤掉，不是整数倍的数字放入到新创建的的管道中
 *     子进程 调用递归处理父进程放入新管道的数字
 * 
 * 踩坑的地方
 *    当父进程写入0时 子进程才能开始处理管道中的数据，不能从管道中取出数据看下是否为0 然后再放回管道中
 *    因为此时父进程还在向管道中写入数据 所以子进程读取完成后再次放入进去时 可能就不是原来的顺序位置了。
 * 
 * 可以分开使用两个管道来处理，一个管道用来存数据，一个管道用来通知子进程是否可以开始处理管道中数据了。
 * 
 * 
 * 每次以管道的第一个数字位参照来进行筛选，子进程在父进程筛选的结果的基础上再次进行筛选
 * 第一个进程以2位参照 筛选掉2的整数倍的数字 剩下  3 5 7 9 11 13 ...
 * 第二个进程以3位参照 筛选掉2的整数倍的数字 剩下  5 7 11 13 ... 
 * 第三个进程以5位参照 筛选掉2的整数倍的数字 剩下  7 11 13 ... 
 * ...
 * 
 * 
 * 需要修复的代码
 * 1 printf 加锁处理 保证线程安全
 * 2 close 后  read 要能从阻塞中退出，这个待定
 * 
 * 踩坑的地方是
 *   父进程和子进程同时持有 管道 p[0] p[1] 我以为 父进程close(p[0]) 相当于关闭了子进程的 p[0]
 *   实际效果参考图片
 */
void primes(int fd)
{
    int first;
    read(fd, &first, 4);
    if (first == 0)
        return;
    printf("prime %d\n", first);

    int p[2];
    pipe(p);
    if (fork() > 0)
    {
        int tmp = 0;
        read(fd, &tmp, 4);
        while (tmp)
        {
            if (tmp % first != 0)
            {
                write(p[1], &tmp, 4);
            }
            read(fd, &tmp, 4);
        }
        int end = 0;
        write(p[1], &end, 4);

        close(p[1]);
        close(p[0]);
        close(fd);
        wait(0);
    }
    else
    {
        // 这里必须要关闭p[1] 否则后面的while 循环结束不了
        // 管道的fork模型参考 图片
        close(p[1]);   
        primes(p[0]);
        close(p[0]);
    }
}



/**
 * 主要逻辑 参照primes.jpg
 *
 * 要保证子进程读取到0时 才能开始处理管道中的数据
 * 
 * 调试版本
 */

int main(int argc, char *argv[])
{
    int p[2];
    pipe(p);
    for (int i = 2; i < 35; i++)
        write(p[1], &i, 4);
    int end = 0;
    write(p[1], &end, 4);
    close(p[1]);
    primes(p[0]);
    close(p[0]);

    exit(1);
}