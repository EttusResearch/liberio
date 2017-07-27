/*
 * Copyright (c) 2017, National Instruments Corp.
 *
 * "That dude over there just punched Merica into that guy,
 *  he must be a Liberio!"
 * 	- Urban Dictionary
 *
 * SPDX-License-Identifier: GPL-2.0
 *
 */

#include <liberio/liberio.h>
#include <stdlib.h>

#include "priv.h"
#include "log.h"

static int __liberio_buf_init_userptr(struct liberio_chan *chan,
				      struct liberio_buf *buf, size_t index)
{
	struct usrp_buffer_breq;
	int err;

	/* until we have scatter gather capabilities, be happy
	 * with page sized chunks
	 */
	err = posix_memalign(&buf->mem, getpagesize(), getpagesize());
	if (err || !buf->mem)
		log_crit(__func__, "failed to allocate memory for buf %u",
			 index);

	buf->index = index;

	/* see above */
	buf->len = getpagesize();
	buf->valid_bytes = buf->len;

	return 0;
}

static void __liberio_buf_release_userptr(struct liberio_buf *buf)
{
	if (buf && buf->mem)
		free(buf->mem);
}

const struct liberio_buf_ops liberio_buf_userptr_ops = {
	.init		=	__liberio_buf_init_userptr,
	.release	=	__liberio_buf_release_userptr,
};

