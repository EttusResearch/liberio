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

#ifndef LIBERIO_DMA_H
#define LIBERIO_DMA_H

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

enum liberio_direction {
	TX = 0,
	RX
};

struct liberio_buf {
	uint32_t index;
	void *mem;
	size_t len;
	size_t valid_bytes;
	struct list_head node;
};

struct liberio_chan {
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

void liberio_init(int loglevel);

void liberio_register_logger(void (*cb)(int, const char *, void*), void *priv);

const char *liberio_chan_get_type(const struct liberio_chan *chan);

struct liberio_chan *liberio_chan_alloc(const char *file,
					const enum liberio_direction dir,
					enum usrp_memory mem_type);

static inline void liberio_chan_put(struct liberio_chan *chan)
{
	ref_dec(&chan->refcnt);
}

static inline void liberio_chan_get(struct liberio_chan *chan)
{
	ref_inc(&chan->refcnt);
}

int liberio_chan_set_fixed_size(struct liberio_chan *chan, size_t size);

int liberio_request_buffers(struct liberio_chan *chan, size_t num_buffers);

/**
 * @param chan USRP DMA Context
 * @param timeout Timeout to dequeue buffer in microseconds
 */
struct liberio_buf *liberio_buf_dequeue(struct liberio_chan *chan,
					  int timeout);

int liberio_buf_enqueue(struct liberio_chan *chan,
			 struct liberio_buf *buf);

int liberio_chan_start_streaming(struct liberio_chan *chan);

int liberio_chan_stop_streaming(struct liberio_chan *chan);

#ifdef __cplusplus
}
#endif
#endif /* LIBERIO_DMA_H */
