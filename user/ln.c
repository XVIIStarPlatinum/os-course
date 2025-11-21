#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  int is_hardlink = 1;
  // God forgive me for what I'm about to do
  if(argc >= 2 && (argv[1][0] == '-' && argv[1][1] == 's' && argv[1][2] == '\0')) 
  {
    is_hardlink = 0;
    argc--;
    argv++;
  }

  if(argc != 3)
  {
    fprintf(2, "Usage: ln old new\n");
    exit(1);
  }

  if(is_hardlink) 
  {
    if(link(argv[1], argv[2]) < 0) 
    {
      fprintf(2, "link %s %s: failed\n", argv[1], argv[2]);
      exit(1);
    }
  } 
  else 
  {
    if(symlink(argv[1], argv[2]) < 0) 
    {
      fprintf(2, "link (-s) %s %s: failed\n", argv[1], argv[2]); 
      exit(1);
    }
  }
  exit(0);
}
