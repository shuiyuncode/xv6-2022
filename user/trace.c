#include "kernel/param.h"
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"


// trace 32 grep hello README
// ./grade-lab-syscall trace
// still has bug
int
main(int argc, char *argv[])
{
  int i;
  char *nargv[MAXARG];// xargs 里面用到的最大参数个数

  if(argc < 3 || (argv[1][0] < '0' || argv[1][0] > '9')){
    fprintf(2, "Usage: %s mask command\n", argv[0]);
    exit(1);
  }

  if (trace(atoi(argv[1])) < 0) {
    fprintf(2, "%s: trace failed\n", argv[0]);
    exit(1);
  }
  
  for(i = 2; i < argc && i < MAXARG; i++){
    nargv[i-2] = argv[i];
  }
  exec(nargv[0], nargv);// xargs 实验中使用了
  exit(0);
}
