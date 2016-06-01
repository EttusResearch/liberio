#include "v4l2-stuff.h"
#include "dma.h"

#include <stdio.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

static void usrp_dma_buf_unmap(struct usrp_dma_buf *buf)
{
	printf("%s: usrp_dma_buf unmapping index %u\n",
	       __func__, buf->index);

	if (buf && buf->mem)
		munmap(buf->mem, buf->len);
}

static void usrp_dma_ctx_free(const struct ref *ref)
{
	size_t i;
	struct usrp_dma_ctx *ctx = container_of(ref, struct usrp_dma_ctx,
						refcnt);

	if (!ctx)
		return;

	for (i = ctx->nbufs; i > 0; i--)
		usrp_dma_buf_unmap(ctx->bufs + i);

	close(ctx->fd);
	free(ctx);
}

struct usrp_dma_ctx *usrp_dma_ctx_alloc(const char *file, enum
					usrp_dma_direction dir)
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
	ctx->refcnt = (struct ref){usrp_dma_ctx_free, 1};

	return ctx;

out_free:
	free(ctx);

	return NULL;
}


static int usrp_dma_buf_init(struct usrp_dma_ctx *ctx, struct usrp_dma_buf *buf,
			     size_t index)
{
	struct v4l2_buffer breq;
	int err;

	memset(&breq, 0, sizeof(breq));
	breq.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	breq.index = index;
	breq.memory = V4L2_MEMORY_MMAP;

	err = ioctl(ctx->fd, VIDIOC_QUERYBUF, &breq);
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

	//printf("%s: usrp_dma_buf with index %u, mapped at %p\n",
	//       __func__, index, buf->mem);

	return err;
}


int usrp_dma_request_buffers(struct usrp_dma_ctx *ctx, size_t num_buffers)
{
	struct v4l2_requestbuffers req;
	int err;
	size_t i;

	memset(&req, 0, sizeof(req));
	req.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	req.memory = V4L2_MEMORY_MMAP;
	req.count = num_buffers;

	err = ioctl(ctx->fd, VIDIOC_REQBUFS, &req);
	if (err) {
		fprintf(stderr, "failed to request buffers (num_buffers was %u\n", num_buffers);
		return err;
	}

	ctx->bufs = calloc(req.count, sizeof(struct usrp_dma_buf));
	if (!ctx->bufs) {
		fprintf(stderr, "failed to allo mem for buffers\n");
		return -ENOMEM;
	}

	for (i = 0; i < req.count; i++) {
		err = usrp_dma_buf_init(ctx, ctx->bufs + i, i);
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
		usrp_dma_buf_unmap(ctx->bufs + i);
		i--;
	}

	free(ctx->bufs);

	return err;
}

int usrp_dma_buf_enqueue(struct usrp_dma_ctx *ctx,
			 struct usrp_dma_buf *buf)
{
	struct v4l2_buffer breq;
	enum v4l2_buf_type type = (ctx->dir == TX) ?
		V4L2_BUF_TYPE_VIDEO_OUTPUT :
		V4L2_BUF_TYPE_VIDEO_CAPTURE;


	memset(&breq, 0, sizeof(breq));
	breq.type = type;
	breq.memory = V4L2_MEMORY_MMAP;
	breq.index = buf->index;
	breq.bytesused = buf->valid_bytes;

	return ioctl(ctx->fd, VIDIOC_QBUF, &breq);
}

struct usrp_dma_buf *usrp_dma_buf_dequeue(struct usrp_dma_ctx *ctx)
{
	struct v4l2_buffer breq;
	struct usrp_dma_buf *buf;
	int err;

	enum v4l2_buf_type type = (ctx->dir == TX) ?
		V4L2_BUF_TYPE_VIDEO_OUTPUT :
		V4L2_BUF_TYPE_VIDEO_CAPTURE;

	memset(&breq, 0, sizeof(breq));
	breq.type = type;
	breq.memory = V4L2_MEMORY_MMAP;

	err = ioctl(ctx->fd, VIDIOC_DQBUF, &breq);
	if (err)
		return NULL;

	buf = ctx->bufs + breq.index;
	buf->valid_bytes = breq.bytesused;

	return buf;
}



int usrp_dma_ctx_start_streaming(struct usrp_dma_ctx *ctx)
{
	enum v4l2_buf_type type = (ctx->dir == TX) ?
		V4L2_BUF_TYPE_VIDEO_OUTPUT :
		V4L2_BUF_TYPE_VIDEO_CAPTURE;
	return ioctl(ctx->fd, VIDIOC_STREAMON, type);
}

int usrp_dma_ctx_stop_streaming(struct usrp_dma_ctx *ctx)
{
	enum v4l2_buf_type type = (ctx->dir == TX) ?
		V4L2_BUF_TYPE_VIDEO_OUTPUT :
		V4L2_BUF_TYPE_VIDEO_CAPTURE;

	return ioctl(ctx->fd, VIDIOC_STREAMOFF, type);
}

