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


#include "v4l2-stuff.h"
#include "dma.h"

#include <stdio.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#define RETRIES 100

static inline enum v4l2_buf_type __to_buf_type(struct usrp_dma_ctx *ctx)
{
	return (ctx->dir == TX) ? V4L2_BUF_TYPE_VIDEO_OUTPUT
		: V4L2_BUF_TYPE_VIDEO_CAPTURE;
}

static int __usrp_dma_ioctl(int fd, unsigned long req, void *arg)
{
	int r;

	do {
		r = ioctl(fd, req, arg);
	} while (-1 == r && EINTR == errno);

	return r;
}

static void __usrp_dma_buf_unmap(struct usrp_dma_buf *buf)
{
	if (buf && buf->mem)
		munmap(buf->mem, buf->len);
}

static void __usrp_dma_ctx_free(const struct ref *ref)
{
	size_t i;
	int err;
	struct usrp_dma_ctx *ctx = container_of(ref, struct usrp_dma_ctx,
						refcnt);
	if (!ctx)
		return;

	for (i = ctx->nbufs; i > 0; i--)
		__usrp_dma_buf_unmap(ctx->bufs + i);

	/*
	for (i = 0; i < RETRIES; i++) {
		err = usrp_dma_request_buffers(ctx, 0);
		if (!err)
			break;
		sleep(0.5);
	}
	*/

	close(ctx->fd);
	free(ctx);
}

struct usrp_dma_ctx *usrp_dma_ctx_alloc(const char *file,
					const enum usrp_dma_direction dir)
{
	struct usrp_dma_ctx *ctx;

	ctx = malloc(sizeof(*ctx));
	if (!ctx)
		return NULL;

	ctx->fd = open(file, O_RDWR);
	if (ctx->fd < 0) {
		fprintf(stderr, "Failed to open device\n");
		goto out_free;
	}

	ctx->dir = dir;
	ctx->bufs = NULL;
	ctx->nbufs = 0;
	ctx->refcnt = (struct ref){__usrp_dma_ctx_free, 1};

	return ctx;

out_free:
	free(ctx);

	return NULL;
}


static int __usrp_dma_buf_init(struct usrp_dma_ctx *ctx,
			       struct usrp_dma_buf *buf, size_t index)
{
	struct v4l2_buffer breq;
	int err;

	memset(&breq, 0, sizeof(breq));
	breq.type = __to_buf_type(ctx);
	breq.index = index;
	breq.memory = V4L2_MEMORY_MMAP;

	err = __usrp_dma_ioctl(ctx->fd, VIDIOC_QUERYBUF, &breq);
	if (err) {
		fprintf(stderr,
			"failed to create usrp_dma_buf for index %u\n",
			index);
		return err;
	}

	buf->index = index;
	buf->len = breq.length;
	buf->valid_bytes = 4096;

	buf->mem = mmap(NULL, breq.length,
			PROT_READ | PROT_WRITE,
			MAP_SHARED, ctx->fd,
			breq.m.offset);
	if (buf->mem == MAP_FAILED) {
		fprintf(stderr, "failed to mmap buffer with index %u", index);
		return err;
	}

	return err;
}


int usrp_dma_request_buffers(struct usrp_dma_ctx *ctx, size_t num_buffers)
{
	struct v4l2_requestbuffers req;
	int err;
	size_t i;
	enum v4l2_buf_type type = (ctx->dir == TX) ?
		V4L2_BUF_TYPE_VIDEO_OUTPUT :
		V4L2_BUF_TYPE_VIDEO_CAPTURE;

	memset(&req, 0, sizeof(req));
	req.type = type;
	req.memory = V4L2_MEMORY_MMAP;
	req.count = num_buffers;

	err = __usrp_dma_ioctl(ctx->fd, VIDIOC_REQBUFS, &req);
	if (err) {
		fprintf(stderr, "failed to request buffers (num_buffers was %u): %s\n", num_buffers, strerror(errno));
		return err;
	}

	ctx->bufs = calloc(req.count, sizeof(struct usrp_dma_buf));
	if (!ctx->bufs) {
		fprintf(stderr, "failed to allo mem for buffers\n");
		return -ENOMEM;
	}

	for (i = 0; i < req.count; i++) {
		err = __usrp_dma_buf_init(ctx, ctx->bufs + i, i);
		if (err) {
			fprintf(stderr, "failed to init buffer (%u/%u)\n", i,
				req.count);
			goto out_free;
		}
	}

	ctx->nbufs = i;

	return 0;

out_free:
	while (i) {
		__usrp_dma_buf_unmap(ctx->bufs + i);
		i--;
	}

	free(ctx->bufs);

	return err;
}

int usrp_dma_buf_enqueue(struct usrp_dma_ctx *ctx, struct usrp_dma_buf *buf)
{
	struct v4l2_buffer breq;
	fd_set fds;
	struct timeval tv;

	memset(&breq, 0, sizeof(breq));
	breq.type = __to_buf_type(ctx);
	breq.memory = V4L2_MEMORY_MMAP;
	breq.index = buf->index;
	breq.bytesused = buf->valid_bytes;

	return __usrp_dma_ioctl(ctx->fd, VIDIOC_QBUF, &breq);
}

struct usrp_dma_buf *usrp_dma_buf_dequeue(struct usrp_dma_ctx *ctx)
{
	struct v4l2_buffer breq;
	struct usrp_dma_buf *buf;
	int err;
	fd_set fds;
	struct timeval tv;

	FD_ZERO(&fds);
	FD_SET(ctx->fd, &fds);

	tv.tv_sec = 2;
	tv.tv_usec = 0;

	//printf("-- Waiting for buffer (%s)\n", ctx->dir == TX ? "TX" : "RX");
	if (ctx->dir == RX)
		err = select(ctx->fd + 1, &fds, NULL, NULL, &tv);
	else
		err = select(ctx->fd + 1, NULL, &fds, NULL, &tv);

	if (!err) {
		fprintf(stderr, "select timeout\n");
		return NULL;
	}

	if (-1 == err) {
		fprintf(stderr, "select failed: %s\n", strerror(errno));
		return NULL;
	}

	memset(&breq, 0, sizeof(breq));
	breq.type = __to_buf_type(ctx);
	breq.memory = V4L2_MEMORY_MMAP;

	err = __usrp_dma_ioctl(ctx->fd, VIDIOC_DQBUF, &breq);
	if (err)
		return NULL;

	buf = ctx->bufs + breq.index;
	if (ctx->dir == RX)
		buf->valid_bytes = breq.bytesused;

	return buf;
}

/*
 * usrp_dma_buf_export() - Export usrp dma buffer as dmabuf fd
 * that can be shared between processes
 * @ctx: context
 * @buf: buffer
 * @dmafd: dma file descriptor output
 */
int usrp_dma_buf_export(struct usrp_dma_ctx *ctx, struct usrp_dma_buf *buf,
			int *dmafd)
{
	struct v4l2_exportbuffer breq;
	int err;

	memset(&breq, 0, sizeof(breq));
	breq.type = __to_buf_type(ctx);
	breq.index = buf->index;

	err = __usrp_dma_ioctl(ctx->fd, VIDIOC_EXPBUF, &breq);
	if (err) {
		fprintf(stderr, "failed to export buffer\n");
		return err;
	}

	*dmafd = breq.fd;

	return 0;
}

int usrp_dma_ctx_start_streaming(struct usrp_dma_ctx *ctx)
{
	enum v4l2_buf_type type = __to_buf_type(ctx);
	return __usrp_dma_ioctl(ctx->fd, VIDIOC_STREAMON, (void *)type);
}

int usrp_dma_ctx_stop_streaming(struct usrp_dma_ctx *ctx)
{
	enum v4l2_buf_type type = __to_buf_type(ctx);
	return __usrp_dma_ioctl(ctx->fd, VIDIOC_STREAMOFF, (void *)type);
}

