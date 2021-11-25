#include "gc.h"
#include <string.h>
#include <stdio.h>

int main()
{
  GC_INIT();
  for (size_t i = 0; i < 10000000; ++i)
  {
    char* buffer = gc_malloc(1000);
    strcpy(buffer, "hello, world");
  }
  puts("done");
}
