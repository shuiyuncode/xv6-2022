#include "kernel/types.h"
#include "kernel/param.h"
#include "user/user.h"

#define MSGSIZE 16
/**
 * xargs 參考 https://zhuanlan.zhihu.com/p/157758410
 *  
 * 將标准输入转换为命令行参数
 * 
 * echo hello too | xargs echo bye
 * sh < xargstest.sh
 * find . b | xargs grep hello
 */
int main(int argc, char *argv[])
{
    sleep(10);

    char buf[MSGSIZE];
    read(0, buf, MSGSIZE);

    char *xargv[MAXARG];
    int xargc = 0;
    for (int i = 1; i < argc; i++)
    {
        xargv[xargc] = argv[i];
        xargc++;
    }

    char *p = buf;
    for (int i = 1; i < MSGSIZE; i++)
    {
        if (buf[i] == '\n')
        {
            int pid = fork();
            if (pid > 0)
            {
                p = &buf[i + 1];
                wait(0);
            }
            else
            {
                // exec 调用系统函数
                buf[i] = 0;
                xargv[xargc] = p;
                xargc++;
                xargv[xargc] = 0;
                xargc++;

                exec(xargv[0], xargv);
                exit(0);
            }
        }
    }

    wait(0);
    exit(0);
}
