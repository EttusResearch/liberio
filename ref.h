#ifndef REF_H
#define REF_H

#include <stddef.h>

#define container_of(ptr, type, member) \
	    ((type *)((char *)(ptr) - offsetof(type, member)))

struct ref {
	void (*free)(const struct ref *);
	int count;
};

static inline void ref_inc(const struct ref *ref)
{
	__sync_add_and_fetch((int *)&ref->count, 1);
}

static inline void ref_dec(const struct ref *ref)
{
	if (__sync_sub_and_fetch((int *)&ref->count, 1) == 0)
		ref->free(ref);
}


#endif /* REF_H */
