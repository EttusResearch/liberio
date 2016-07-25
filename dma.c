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
#include "util.h"
#include "log.h"

#include <stdio.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>

#define RETRIES 100
#define TIMEOUT 1
#define USRP_MAX_FRAMES 128

struct usrp_dma_buf_ops {
	int(*init)(struct usrp_dma_ctx *, struct usrp_dma_buf *, size_t);
	void(*release)(struct usrp_dma_buf *);
};

void usrp_dma_init(int loglevel)
{
	log_init(loglevel, "usrp_dma");
}

static inline enum usrp_buf_type __to_buf_type(struct usrp_dma_ctx *ctx)
{
	return (ctx->dir == TX) ? USRP_BUF_TYPE_OUTPUT : USRP_BUF_TYPE_INPUT;
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


static int __usrp_dma_buf_init(struct usrp_dma_ctx *ctx,
			       struct usrp_dma_buf *buf, size_t index)
{
	struct usrp_buffer breq;
	int err;

	memset(&breq, 0, sizeof(breq));
	breq.type = __to_buf_type(ctx);
	breq.index = index;
	breq.memory = USRP_MEMORY_MMAP;

	err = __usrp_dma_ioctl(ctx->fd, USRPIOC_QUERYBUF, &breq);
	if (err) {
		log_warn(__func__,
			"failed to create usrp_dma_buf for index %u", index);
		return err;
	}

	buf->index = index;
	buf->len = breq.length;
	buf->valid_bytes = buf->len;

	buf->mem = mmap(NULL, breq.length,
			PROT_READ | PROT_WRITE,
			MAP_SHARED, ctx->fd,
			breq.m.offset);
	if (buf->mem == MAP_FAILED) {
		log_warnx(__func__, "failed to mmap buffer with index %u", index);
		return err;
	}

	return err;
}

struct usrp_dma_ctx *usrp_dma_ctx_alloc(const char *file,
					const enum usrp_dma_direction dir,
					enum usrp_memory mem_type)
{
	struct usrp_dma_ctx *ctx;

	ctx = malloc(sizeof(*ctx));
	if (!ctx)
		return NULL;

	ctx->fd = open(file, O_RDWR);
	if (ctx->fd < 0) {
		log_warn(__func__, "Failed to open device");
		goto out_free;
	}

	ctx->dir = dir;
	ctx->bufs = NULL;
	ctx->nbufs = 0;
	ctx->mem_type = mem_type;

	ctx->refcnt = (struct ref){__usrp_dma_ctx_free, 1};

	return ctx;

out_free:
	free(ctx);

	return NULL;
}

int usrp_dma_request_buffers(struct usrp_dma_ctx *ctx, size_t num_buffers)
{
	struct usrp_requestbuffers req;
	int err;
	size_t i;
	enum usrp_buf_type type = __to_buf_type(ctx);

	if (num_buffers > USRP_MAX_FRAMES) {
		log_warnx(__func__, "tried to allocate %u buffers, max is %u"
		                    " proceeding with: %u",
			 num_buffers, USRP_MAX_FRAMES, USRP_MAX_FRAMES);
		num_buffers = USRP_MAX_FRAMES;
	}

	memset(&req, 0, sizeof(req));
	req.type = type;
	req.memory = ctx->mem_type;
	req.count = num_buffers;

	err = __usrp_dma_ioctl(ctx->fd, USRPIOC_REQBUFS, &req);
	if (err) {
		log_crit(__func__, "failed to request buffers (num_buffers was %u)", num_buffers);
		return err;
	}

	ctx->bufs = calloc(req.count, sizeof(struct usrp_dma_buf));
	if (!ctx->bufs) {
		log_crit(__func__, "failed to alloc mem for buffers");
		return -ENOMEM;
	}

	for (i = 0; i < req.count; i++) {
		err = __usrp_dma_buf_init(ctx, ctx->bufs + i, i);
		if (err) {
			log_crit(__func__, "failed to init buffer (%u/%u)", i,
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
	struct usrp_buffer breq;
	fd_set fds;
	struct timeval tv;

	memset(&breq, 0, sizeof(breq));
	breq.type = __to_buf_type(ctx);
	breq.memory = USRP_MEMORY_MMAP;
	breq.index = buf->index;
	if (ctx->dir == TX)
		breq.bytesused = buf->valid_bytes;

	return __usrp_dma_ioctl(ctx->fd, USRPIOC_QBUF, &breq);
}

struct usrp_dma_buf *usrp_dma_buf_dequeue(struct usrp_dma_ctx *ctx)
{
	struct usrp_buffer breq;
	struct usrp_dma_buf *buf;
	int err;
	fd_set fds;
	struct timeval tv;

	FD_ZERO(&fds);
	FD_SET(ctx->fd, &fds);

	tv.tv_sec = 0;
	tv.tv_usec = 250000;

	if (ctx->dir == RX)
		err = select(ctx->fd + 1, &fds, NULL, NULL, &tv);
	else
		err = select(ctx->fd + 1, NULL, &fds, NULL, &tv);

	if (!err) {
		log_warnx(__func__, "select timeout");
		return NULL;
	}

	if (-1 == err) {
		log_warn(__func__, "select failed");
		return NULL;
	}

	memset(&breq, 0, sizeof(breq));
	breq.type = __to_buf_type(ctx);
	breq.memory = USRP_MEMORY_MMAP;

	err = __usrp_dma_ioctl(ctx->fd, USRPIOC_DQBUF, &breq);
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
	struct usrp_exportbuffer breq;
	int err;

	memset(&breq, 0, sizeof(breq));
	breq.type = __to_buf_type(ctx);
	breq.index = buf->index;

	err = __usrp_dma_ioctl(ctx->fd, USRPIOC_EXPBUF, &breq);
	if (err) {
		log_warn(__func__, "failed to export buffer");
		return err;
	}

	*dmafd = breq.fd;

	return 0;
}

int usrp_dma_buf_send_fd(int sockfd, int fd)
{
	struct msghdr message;
	struct iovec iov[1];
	struct cmsghdr *control_message = NULL;
	char ctrl_buf[CMSG_SPACE(sizeof(int))];
	char data[1];

	memset(&message, 0, sizeof(struct msghdr));
	memset(ctrl_buf, 0, CMSG_SPACE(sizeof(int)));

	data[0] = '!';
	iov[0].iov_base = data;
	iov[0].iov_len = sizeof(data);

	message.msg_name = NULL;
	message.msg_namelen = 0;
	message.msg_iov = iov;
	message.msg_iovlen = 1;
	message.msg_controllen =  CMSG_SPACE(sizeof(int));
	message.msg_control = ctrl_buf;

	control_message = CMSG_FIRSTHDR(&message);
	control_message->cmsg_level = SOL_SOCKET;
	control_message->cmsg_type = SCM_RIGHTS;
	control_message->cmsg_len = CMSG_LEN(sizeof(int));

	*((int *) CMSG_DATA(control_message)) = fd;

	return sendmsg(sockfd, &message, 0);
}

int usrp_dma_buf_recv_fd(int sockfd)
{
	int sent_fd;
	struct msghdr message;
	struct iovec iov[1];
	struct cmsghdr *cmsg = NULL;
	char ctrl_buf[CMSG_SPACE(sizeof(int))];
	char data[1];
	int res;

	memset(&message, 0, sizeof(struct msghdr));
	memset(ctrl_buf, 0, CMSG_SPACE(sizeof(int)));

	/* For the dummy data */
	iov[0].iov_base = data;
	iov[0].iov_len = sizeof(data);

	message.msg_name = NULL;
	message.msg_namelen = 0;
	message.msg_control = ctrl_buf;
	message.msg_controllen = CMSG_SPACE(sizeof(int));
	message.msg_iov = iov;
	message.msg_iovlen = 1;

	if ((res = recvmsg(sockfd, &message, 0)) <= 0)
		return res;

	/* Iterate through header to find if there is a file
	 * descriptor */
	for (cmsg = CMSG_FIRSTHDR(&message); cmsg != NULL;
	     cmsg = CMSG_NXTHDR(&message,cmsg))
		if ((cmsg->cmsg_level == SOL_SOCKET) && (cmsg->cmsg_type == SCM_RIGHTS))
			return *((int*)CMSG_DATA(cmsg));

	return -EINVAL;
}

int usrp_dma_ctx_start_streaming(struct usrp_dma_ctx *ctx)
{
	enum usrp_buf_type type = __to_buf_type(ctx);
	return __usrp_dma_ioctl(ctx->fd, USRPIOC_STREAMON, (void *)type);
}

int usrp_dma_ctx_stop_streaming(struct usrp_dma_ctx *ctx)
{
	enum usrp_buf_type type = __to_buf_type(ctx);
	return __usrp_dma_ioctl(ctx->fd, USRPIOC_STREAMOFF, (void *)type);
}

