#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fs.h"
#include "user/user.h"

static int
findname(uint inum, char *name)
{
  int fd;
  struct dirent de;

  fd = open(".", 0);
  if(fd < 0)
    return -1;

  while(read(fd, &de, sizeof(de)) == sizeof(de)){
    if(de.inum == inum){
      memmove(name, de.name, DIRSIZ);
      name[DIRSIZ] = 0;
      close(fd);
      return 0;
    }
  }

  close(fd);
  return -1;
}

static int
buildpath(char *buf, int nbuf)
{
  struct stat st, pst;
  char name[DIRSIZ + 1];

  if(stat(".", &st) < 0 || stat("..", &pst) < 0)
    return -1;

  if(st.ino == pst.ino){
    buf[0] = 0;
    return 0;
  }

  if(chdir("..") < 0)
    return -1;
  if(findname(st.ino, name) < 0)
    return -1;
  if(buildpath(buf, nbuf) < 0)
    return -1;

  if(strlen(buf) + 1 + strlen(name) + 1 > nbuf)
    return -1;
  if(strlen(buf) == 0)
    strcpy(buf, "/");
  else
    strcpy(buf + strlen(buf), "/");
  strcpy(buf + strlen(buf), name);

  if(chdir(name) < 0)
    return -1;
  return 0;
}

int
main(void)
{
  char buf[512];

  if(buildpath(buf, sizeof(buf)) < 0){
    fprintf(2, "pwd: failed\n");
    exit(1);
  }

  if(strlen(buf) == 0)
    printf("/\n");
  else
    printf("%s\n", buf);

  exit(0);
}
