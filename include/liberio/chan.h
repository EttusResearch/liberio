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

#ifndef LIBERIO_CHAN_H
#define LIBERIO_CHAN_H

#ifdef __cplusplus
extern "C"
{
#endif

#include <liberio/ref.h>
#include <liberio/list.h>
#include <stdint.h>

enum usrp_memory {
	USRP_MEMORY_MMAP             = 1,
	USRP_MEMORY_USERPTR          = 2,
	USRP_MEMORY_OVERLAY          = 3,
	USRP_MEMORY_DMABUF           = 4,
};


/* Channel API */
struct liberio_chan;

void liberio_chan_put(struct liberio_chan *chan);
void liberio_chan_get(struct liberio_chan *chan);

const char *liberio_chan_get_type(const struct liberio_chan *chan);

int liberio_chan_set_fixed_size(struct liberio_chan *chan, size_t plane,
				size_t size);

int liberio_chan_request_buffers(struct liberio_chan *chan, size_t num_buffers);


int liberio_chan_enqueue_all(struct liberio_chan *chan);

struct liberio_buf *liberio_chan_get_buf_at_index(const struct liberio_chan *chan,
						  size_t index);

size_t liberio_chan_get_num_bufs(const struct liberio_chan *chan);

struct liberio_buf *liberio_chan_buf_dequeue(struct liberio_chan *chan,
		int timeout);

int liberio_chan_buf_enqueue(struct liberio_chan *chan,
			struct liberio_buf *buf);

int liberio_chan_start_streaming(struct liberio_chan *chan);

int liberio_chan_stop_streaming(struct liberio_chan *chan);

#ifdef __cplusplus
}
#endif
#endif /* LIBERIO_CHAN_H */
