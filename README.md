# The Chaotic Evil Garbage Collector

The Chaotic Evil Garbage Collector is a tiny library that you can add to your C project to instantly gain the benefits of garbage collection. Simply ```#include "gc.h"``` at the top of your source file, and ```GC_INIT()``` at the start of ```main()```. Then simply add ```gc.c``` to your compiler arguments and you're good to go.

Look at the included ```example.c``` that shows the garbage collector in use. If it were using normal malloc(), the program would leak gigabytes of memory and then crash. However with GC it uses hardly any memory at all, and finishes in less time than the malloc() implementation took to crash.

This header file provides the following functions:
```
void* gc_malloc(size_t);
void* gc_realloc(void*, size_t);
char* gc_strdup();
char* gc_strndup();
```

Each of these functions behaves in the same way as its non-gc'd equivilent. There is also a ```gc_collect()``` function to explicitly trigger a collection.

## How is this chaotic or evil???

This is a conservative copying garbage collector. What this means is that the GC has no idea what values on the stack or the heap are pointers or not, It just kinda guesses based on where they appear to point to. Traditional conservative collectors are forbidden from moving data, as it would involve modifying these maybe-pointers. This GC is chaotic and evil in that it modifies them anyway - even if they're on the stack. Please don't actually use this. While I haven't encountered any bugs caused by this memory management strategy, I'm fairly sure it could be exploited by an attacker to read arbitrary heap values.

