/*
 * Copyright (c) 2017, National Instruments Corp.
 *
 * "That dude over there just punched Merica into that guy,
 *  he must be a Liberio!"
 * 	- Urban Dictionary
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 */

#ifndef LIBERIO_REF_H
#define LIBERIO_REF_H

#include <stddef.h>

/* struct ref - Used to implement refernce counting
 *
 * @free: Callback function that gets called once last reference
 *        got dropped.
 * @count: The reference count
 */
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
