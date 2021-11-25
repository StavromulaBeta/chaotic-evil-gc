# The Chaotic Evil Garbage Collector

The Chaotic Evil Garbage Collector is a tiny library (<100 sloc) that you can add to your C project to instantly gain the benefits of garbage collection. Simply ```#include "gc.h"``` at the top of your source file, and ```GC_INIT()``` at the start of ```main()```. Then simply add ```gc.c``` to your compiler arguments and you're good to go.

Look at the included ```example.c``` that shows the garbage collector in use. If it were using normal malloc(), the program would leak gigabytes of memory and then crash. However with GC it uses hardly any memory at all, and finishes in less time than the malloc() implementation took to crash.

This header file provides the following functions:
```
void* gc_malloc(size_t);
void* gc_realloc(void*, size_t);
char* gc_strdup();
char* gc_strndup();
```
Each of these functions behaves in the same way as its non-gc'd equivilent.

Also provided is the ```gc_collect()``` function to explicitely trigger a collection, and the boolean variable ```gc_enabled``` which can be used to disable and enable automatic collection.
