#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

/*
find(path, target_name) quick review:

1) open(path) + fstat(fd, &st):
   - fstat is for an already-open fd.
   - Used to determine what "current path" is (T_DIR/T_FILE/T_DEVICE).

2) If current path is a directory (T_DIR):
   - read each dirent from fd
   - build child path into buf: "<path>/<entry>"
   - stat(buf, &st) to inspect each child entry type
     (stat is for a path string, not an fd)

3) For each child:
   - skip "." and ".." to avoid infinite recursion
   - if child is T_DIR: recurse find(buf, target_name)
   - if child is T_FILE and name matches: print full path

4) Close fd before return in all paths.
*/

void
find(char *path, char *target_name)
{
  char buf[512], *p;
  int fd;
  struct dirent de;
  struct stat st;

  if((fd = open(path, 0)) < 0){
    fprintf(2, "find: cannot open %s\n", path);
    return;
  }

  if(fstat(fd, &st) < 0){
    fprintf(2, "find: cannot stat %s\n", path);
    close(fd);
    return;
  }

  switch(st.type){
  case T_DEVICE:
  case T_FILE:
    break;

  case T_DIR:
    if(strlen(path) + 1 + DIRSIZ + 1 > sizeof buf){
      printf("find: path too long\n");
      break;
    }
    strcpy(buf, path);
    p = buf+strlen(buf);
    *p++ = '/';
    while(read(fd, &de, sizeof(de)) == sizeof(de)){
      if(de.inum == 0)
        continue;
      memmove(p, de.name, DIRSIZ);
      p[DIRSIZ] = 0;
      if(stat(buf, &st) < 0){
        printf("find: cannot stat %s\n", buf);
        continue;
      }

      if(strcmp(de.name,".")!=0  && strcmp(de.name,"..")!=0){
        if(st.type == T_DIR) 
            find(buf,target_name);
        else if (st.type == T_FILE)
            if(strcmp(de.name,target_name)==0)
                printf("%s\n", buf); 
      }
    }
    break;
  }
  close(fd);
}

int
main(int argc, char *argv[])
{
  if(argc == 3){
    find(argv[1],argv[2]);
    exit(0);
  }
  printf("Usage: find <path> <target_name>\n");
  exit(0);
}
