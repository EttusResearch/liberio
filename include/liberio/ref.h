/*
 * Copyright (c) 2015, National Instruments Corp.
 *
 * USRP DMA helper library
 *
 * SPDX-License-Identifier: GPL-2.0
 *
 */

#ifndef LIBERIO_REF_H
#define LIBERIO_REF_H

#include <stddef.h>

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


#endif /* LIBERIO_REF_H */
