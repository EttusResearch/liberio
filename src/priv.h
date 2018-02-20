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

#ifndef LIBERIO_PRIV_H
#define LIBERIO_PRIV_H

#include <pthread.h>
#include <liberio/liberio.h>
#include "kernel.h"

struct liberio_ctx {
	struct udev *udev;
	struct ref refcnt;
};

struct liberio_buf {
	uint32_t index;
	void *mem;
	size_t len;
	size_t valid_bytes;
	struct list_head node;
};

struct liberio_chan;

struct liberio_buf_ops {
	int (*init)(struct liberio_chan *, struct liberio_buf *, size_t);
	void (*release)(struct liberio_buf *);
};

struct liberio_chan {
	struct liberio_ctx *ctx;

	int port;

	pthread_spinlock_t lock;
	struct list_head node;

	struct udev_device *dev;

	int fd;
	enum liberio_direction dir;

	struct liberio_buf *bufs;
	size_t nbufs;
	struct list_head free_bufs;

	struct ref refcnt;

	const struct liberio_buf_ops *ops;

	enum usrp_memory mem_type;

	int fix_broken_chdr;
};

static inline enum usrp_buf_type __to_buf_type(struct liberio_chan *chan)
{
	return (chan->dir == TX) ? USRP_BUF_TYPE_OUTPUT : USRP_BUF_TYPE_INPUT;
}

#endif /* LIBERIO_PRIV_H */
