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

#include <liberio/liberio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <string.h>

#include "priv.h"
#include "log.h"
#include "kernel.h"
#include "util.h"

void __liberio_buf_release_mmap(struct liberio_buf *buf)
{
	if (buf && buf->mem)
		munmap(buf->mem, buf->len);
}

int __liberio_buf_init_mmap(struct liberio_chan *chan,
			    struct liberio_buf *buf, size_t index)
{
	struct usrp_buffer breq;
	int err;

	memset(&breq, 0, sizeof(breq));
	breq.type = __to_buf_type(chan);
	breq.index = index;
	breq.memory = USRP_MEMORY_MMAP;

	err = liberio_ioctl(chan->fd, USRPIOC_QUERYBUF, &breq);
	if (err) {
		log_warn(__func__,
			"failed to create liberio_buf for index %u", index);
		return err;
	}

	buf->index = index;
	buf->len = breq.length;
	buf->valid_bytes = buf->len;

	buf->mem = mmap(NULL, breq.length,
			PROT_READ | PROT_WRITE,
			MAP_SHARED, chan->fd,
			breq.m.offset);
	if (buf->mem == MAP_FAILED) {
		log_warn(__func__, "failed to mmap buffer with index %u",
			 index);
		return err;
	}

	return err;
}

const struct liberio_buf_ops liberio_buf_mmap_ops = {
	.init		=	__liberio_buf_init_mmap,
	.release	=	__liberio_buf_release_mmap,
};



