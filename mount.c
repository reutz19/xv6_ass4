#include "param.h"
#include "fcntl.h"
#include "syscall.h"
#include "traps.h"
#include "memlayout.h"
#include "x86.h"
#include "fs.h"
#include "user.h"
#include "stat.h"
#include "types.h"

int main(int argc, char *argv[])
{

  mount(argv[1], atoi(argv[2]));

  exit();		
  return 0;
}
