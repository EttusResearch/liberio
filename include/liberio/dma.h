/*
 * Copyright (c) 2015, National Instruments Corp.
 *
 * USRP DMA helper library
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

enum usrp_dma_direction {
	TX = 0,
	RX
};

struct usrp_dma_buf {
	uint32_t index;
	void *mem;
	size_t len;
	size_t valid_bytes;
	struct list_head node;
};

struct usrp_dma_chan {
	int fd;
	enum usrp_dma_direction dir;

	struct usrp_dma_buf *bufs;
	size_t nbufs;
	struct list_head free_bufs;

	struct ref refcnt;

	const struct usrp_dma_buf_ops *ops;

	enum usrp_memory mem_type;

	int fix_broken_chdr;
};

void usrp_dma_init(int loglevel);

void usrp_dma_register_logger(void (*cb)(int, const char *, void*), void *priv);

const char *usrp_dma_chan_get_type(const struct usrp_dma_chan *chan);

struct usrp_dma_chan *usrp_dma_chan_alloc(const char *file,
					const enum usrp_dma_direction dir,
					enum usrp_memory mem_type);

static inline void usrp_dma_chan_put(struct usrp_dma_chan *chan)
{
	ref_dec(&chan->refcnt);
}

static inline void usrp_dma_chan_get(struct usrp_dma_chan *chan)
{
	ref_inc(&chan->refcnt);
}

int usrp_dma_request_buffers(struct usrp_dma_chan *chan, size_t num_buffers);

/**
 * @param chan USRP DMA Context
 * @param timeout Timeout to dequeue buffer in microseconds
 */
struct usrp_dma_buf *usrp_dma_buf_dequeue(struct usrp_dma_chan *chan,
					  int timeout);

int usrp_dma_buf_enqueue(struct usrp_dma_chan *chan,
			 struct usrp_dma_buf *buf);

int usrp_dma_chan_start_streaming(struct usrp_dma_chan *chan);

int usrp_dma_chan_stop_streaming(struct usrp_dma_chan *chan);

#ifdef __cplusplus
}
#endif
#endif /* LIBERIO_DMA_H */
