/* Forks and waits for a single child process. */

#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void) 
{

  printf('여기 0\n');
  int pid  = fork("child");
  printf("pid: %d\n", pid);
  if ((pid)){
    int status = wait (pid);
    msg ("Parent: child exit status is %d", status);
  } else {
    msg ("child run");
    exit(81);
  }
}
