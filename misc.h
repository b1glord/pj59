#ifndef misc_h
#define misc_h

#include "parser.h"

struct long_array {
    long * array;
    size_t count;
};

int long_compare(void *, void *);
struct long_array * long_array_store(struct store *, size_t);

int script_store(struct string *, struct store *,  struct string **);
int string_store(struct string *, struct store *,  struct string **);
int string_strtol(struct string *, long *);
int string_strtol_split(struct string *, char, struct store *, struct long_array **);
int string_strtol_splitv(struct string *, int, ...);

#endif
