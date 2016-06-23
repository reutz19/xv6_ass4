#include "types.h"
#include "stat.h"
#include "user.h"

int main(int argc, char *argv[])
{

  mount(argv[1], atoi(argv[2]));

  exit();		
  return 0;
}
