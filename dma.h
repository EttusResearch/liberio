/*
 * Copyright (c) 2015, National Instruments Corp.
 *
 * USRP DMA helper library
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#ifndef DMA_H
#define DMA_H

#include "ref.h"

enum usrp_dma_direction {
	TX = 0,
	RX
};

struct usrp_dma_buf {
	uint32_t index;
	void *mem;
	size_t len;
	size_t valid_bytes;
};

struct usrp_dma_ctx {
	int fd;
	enum usrp_dma_direction dir;

	struct usrp_dma_buf *bufs;
	size_t nbufs;

	struct ref refcnt;
};

struct usrp_dma_ctx *usrp_dma_ctx_alloc(const char *file,
					const enum usrp_dma_direction dir);

static inline void usrp_dma_ctx_put(struct usrp_dma_ctx *ctx)
{
	ref_dec(&ctx->refcnt);
}

static inline void usrp_dma_ctx_get(struct usrp_dma_ctx *ctx)
{
	ref_inc(&ctx->refcnt);
}

int usrp_dma_request_buffers(struct usrp_dma_ctx *ctx, size_t num_buffers);

struct usrp_dma_buf *usrp_dma_buf_dequeue(struct usrp_dma_ctx *ctx);

int usrp_dma_buf_enqueue(struct usrp_dma_ctx *ctx,
			 struct usrp_dma_buf *buf);

int usrp_dma_ctx_start_streaming(struct usrp_dma_ctx *ctx);

int usrp_dma_ctx_stop_streaming(struct usrp_dma_ctx *ctx);

#endif /* DMA_H */
