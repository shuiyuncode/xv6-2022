#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"


/**
 * 在获取文件名的函数（fmtname ）中，需要设置 buf[strlen(p)] = 0; 否则比较函数 strcmp 会失效。
 */
char* fmtname(char *path) {
  static char buf[DIRSIZ + 1];
  char *p;
  for (p = path + strlen(path); p >= path && *p != '/';p--);
  p++;
  if (strlen(p) >= DIRSIZ) return p;
  memmove(buf, p, strlen(p));
  memset(buf+strlen(p), ' ', DIRSIZ-strlen(p));
  buf[strlen(p)] = 0;
  return buf;
}


void find(char *path, char *fileName) {
  char buf[512], *p;
  int fd;
  // 结构体 stat 和 dirent 分别存储文件的信息和目录的信息
  struct stat st;
  struct dirent de;

  if ((fd = open(path, 0)) < 0 ) {
    fprintf(2, "find: cannot open %s\n", path);
    return;
  }
  if (fstat(fd, &st) < 0) {
    fprintf(2, "find: cannot stat %s\n", path);
    close(fd);
    return;
  }
  //printf("%s %s\n",path, fmtname(path));
  switch(st.type) {
  case T_FILE:
    //printf("%s\n", fmtname(path));
    if (strcmp(fmtname(path), fileName) == 0) {
      printf("%s\n", path);
    }
    break;
  case T_DIR:
    //printf("%s %s\n", path, fmtname(path));
    if(strlen(path) + 1 + DIRSIZ + 1 > sizeof buf) {
      printf("find: path too long\n");
      break;
    }
    strcpy(buf, path);
    p = buf + strlen(buf);
    *p++ = '/';
    while (read(fd, &de, sizeof(de)) == sizeof(de)) {
      if (de.inum == 0 || strcmp(de.name, ".") == 0 || strcmp(de.name, "..") == 0) {
        continue;
      }
      memmove(p, de.name, DIRSIZ);
      p[DIRSIZ] = 0;
      find(buf, fileName);
    }
    break;
  }
  close(fd);
}



/** ls 代码基础上调整得到 */
int main(int argc, char *argv[]) {
  if (argc < 3) {
    printf("error\n");
    exit(1);
  }
  find(argv[1], argv[2]);
  exit(0);
}