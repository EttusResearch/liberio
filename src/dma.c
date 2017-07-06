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
#include <liberio/dma.h>
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
	int(*init)(struct usrp_dma_chan *, struct usrp_dma_buf *, size_t);
	void(*release)(struct usrp_dma_buf *);
};

void usrp_dma_init(int loglevel)
{
	log_init(loglevel, "usrp_dma");
}

static inline enum usrp_buf_type __to_buf_type(struct usrp_dma_chan *chan)
{
	return (chan->dir == TX) ? USRP_BUF_TYPE_OUTPUT : USRP_BUF_TYPE_INPUT;
}

static int __usrp_dma_ioctl(int fd, unsigned long req, void *arg)
{
	int r;

	do {
		r = ioctl(fd, req, arg);
	} while (-1 == r && EINTR == errno);

	return r;
}

static void __usrp_dma_buf_release_mmap(struct usrp_dma_buf *buf)
{
	if (buf && buf->mem)
		munmap(buf->mem, buf->len);
}

static void __usrp_dma_chan_free(const struct ref *ref)
{
	size_t i;
	int err;
	struct usrp_dma_chan *chan = container_of(ref, struct usrp_dma_chan,
						refcnt);
	if (!chan)
		return;

	for (i = chan->nbufs - 1; i > 0; i--)
		chan->ops->release(chan->bufs + i);

	/*
	for (i = 0; i < RETRIES; i++) {
		err = usrp_dma_request_buffers(chan, 0);
		if (!err)
			break;
		sleep(0.5);
	}
	*/

	close(chan->fd);
	free(chan);
}


static int __usrp_dma_buf_init_mmap(struct usrp_dma_chan *chan,
				    struct usrp_dma_buf *buf, size_t index)
{
	struct usrp_buffer breq;
	int err;

	memset(&breq, 0, sizeof(breq));
	breq.type = __to_buf_type(chan);
	breq.index = index;
	breq.memory = USRP_MEMORY_MMAP;

	err = __usrp_dma_ioctl(chan->fd, USRPIOC_QUERYBUF, &breq);
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
			MAP_SHARED, chan->fd,
			breq.m.offset);
	if (buf->mem == MAP_FAILED) {
		log_warn(__func__, "failed to mmap buffer with index %u", index);
		return err;
	}

	return err;
}

static int __usrp_dma_buf_init_userptr(struct usrp_dma_chan *chan,
				       struct usrp_dma_buf *buf, size_t index)
{
	struct usrp_buffer_breq;
	int err;

	/* until we have scatter gather capabilities, be happy
	 * with page sized chunks */
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

static void __usrp_dma_buf_release_userptr(struct usrp_dma_buf *buf)
{
	if (buf && buf->mem)
		free(buf->mem);
}

static int __usrp_dma_buf_init_dmabuf(struct usrp_dma_chan *chan,
				       struct usrp_dma_buf *buf, size_t index)
{
	log_crit(__func__, "Not implemented");

	return -ENOTTY;
}

static void __usrp_dma_buf_release_dmabuf(struct usrp_dma_buf *buf)
{
	log_crit(__func__, "Not implemented");
}

static const struct usrp_dma_buf_ops __usrp_dma_buf_mmap_ops = {
	.init		=	__usrp_dma_buf_init_mmap,
	.release	=	__usrp_dma_buf_release_mmap,
};

static const struct usrp_dma_buf_ops __usrp_dma_buf_userptr_ops = {
	.init		=	__usrp_dma_buf_init_userptr,
	.release	=	__usrp_dma_buf_release_userptr,
};

static const struct usrp_dma_buf_ops __usrp_dma_buf_dmabuf_ops = {
	.init		=	__usrp_dma_buf_init_dmabuf,
	.release	=	__usrp_dma_buf_release_dmabuf,
};

static const char * memstring[] = {
	[USRP_MEMORY_MMAP] = "MMAP",
	[USRP_MEMORY_USERPTR] = "USERPTR",
	[USRP_MEMORY_DMABUF] = "DMABUF",
};

const char *usrp_dma_chan_get_type(const struct usrp_dma_chan *chan)
{
	if ((chan->mem_type < USRP_MEMORY_MMAP) ||
	    (chan->mem_type > USRP_MEMORY_DMABUF))
		return "UNKNOWN";

	return memstring[chan->mem_type];
}

struct usrp_dma_chan *usrp_dma_chan_alloc(const char *file,
					const enum usrp_dma_direction dir,
					enum usrp_memory mem_type)
{
	struct usrp_dma_chan *chan;

	chan = malloc(sizeof(*chan));
	if (!chan)
		return NULL;

	chan->fd = open(file, O_RDWR);
	if (chan->fd < 0) {
		log_warn(__func__, "Failed to open device");
		goto out_free;
	}

	chan->dir = dir;
	chan->bufs = NULL;
	chan->nbufs = 0;
	chan->mem_type = mem_type;
	INIT_LIST_HEAD(&chan->free_bufs);

	if (mem_type == USRP_MEMORY_MMAP) {
		chan->ops = &__usrp_dma_buf_mmap_ops;
	} else if (mem_type == USRP_MEMORY_USERPTR){
		chan->ops = &__usrp_dma_buf_userptr_ops;
	} else {
		log_crit(__func__, "Invalid memory type specified");
		return NULL;
	}

	chan->refcnt = (struct ref){__usrp_dma_chan_free, 1};
	usrp_dma_chan_stop_streaming(chan);
	usrp_dma_request_buffers(chan, 0);

	return chan;

out_free:
	free(chan);

	return NULL;
}

int usrp_dma_request_buffers(struct usrp_dma_chan *chan, size_t num_buffers)
{
	struct usrp_requestbuffers req;
	int err;
	size_t i;
	enum usrp_buf_type type = __to_buf_type(chan);

	if (num_buffers > USRP_MAX_FRAMES) {
		log_warnx(__func__, "tried to allocate %u buffers, max is %u"
		                    " proceeding with: %u",
			 num_buffers, USRP_MAX_FRAMES, USRP_MAX_FRAMES);
		num_buffers = USRP_MAX_FRAMES;
	}

	memset(&req, 0, sizeof(req));
	req.type = type;
	req.memory = chan->mem_type;
	req.count = num_buffers;

	err = __usrp_dma_ioctl(chan->fd, USRPIOC_REQBUFS, &req);
	if (err) {
		log_crit(__func__, "failed to request buffers (num_buffers was %u) %d", num_buffers, err);
		perror("foo");
		return err;
	}

	chan->bufs = calloc(req.count, sizeof(struct usrp_dma_buf));
	if (!chan->bufs) {
		log_crit(__func__, "failed to alloc mem for buffers");
		return -ENOMEM;
	}

	for (i = 0; i < req.count; i++) {
		err = chan->ops->init(chan, chan->bufs + i, i);
		if (err) {
			log_crit(__func__, "failed to init buffer (%u/%u)", i,
				req.count);
			goto out_free;
		}
		if (chan->dir == TX)
			list_add(&chan->bufs[i].node, &chan->free_bufs);
	}

	chan->nbufs = i;

	return 0;

out_free:
	while (i) {
		chan->ops->release(chan->bufs + i);
		i--;
	}

	free(chan->bufs);

	return err;
}

int usrp_dma_buf_enqueue(struct usrp_dma_chan *chan, struct usrp_dma_buf *buf)
{
	struct usrp_buffer breq;
	fd_set fds;

	memset(&breq, 0, sizeof(breq));
	breq.type = __to_buf_type(chan);
	breq.memory = chan->mem_type;
	breq.index = buf->index;

	if (chan->mem_type == USRP_MEMORY_USERPTR) {
		breq.m.userptr = (unsigned long)buf->mem;
		breq.length = buf->len;
	}

	/* HACK: Using valid_bytes for RX too
	if (chan->dir == TX) */
		breq.bytesused = buf->valid_bytes;

	return __usrp_dma_ioctl(chan->fd, USRPIOC_QBUF, &breq);
}

struct usrp_dma_buf *usrp_dma_buf_dequeue(struct usrp_dma_chan *chan, int timeout)
{
	struct usrp_buffer breq;
	struct usrp_dma_buf *buf;
	int err, i;
	fd_set fds;
	struct timeval tv;
	struct timeval *tv_ptr = &tv;

	if (!list_empty(&chan->free_bufs)) { // Should only happen with chan->dir == TX (see usrp_dma_request_buffers)
		buf = list_first_entry(&chan->free_bufs, struct usrp_dma_buf, node);
		list_del(&buf->node);
		return buf;
	}

	FD_ZERO(&fds);
	FD_SET(chan->fd, &fds);

	if (timeout >= 0) {
		tv.tv_sec = timeout / 1000000;
		tv.tv_usec = timeout % 1000000;
	} else {
		tv_ptr = NULL;
	}

	if (chan->dir == RX)
		err = select(chan->fd + 1, &fds, NULL, NULL, tv_ptr);
	else
		err = select(chan->fd + 1, NULL, &fds, NULL, tv_ptr);

	if (!err) {
		log_warnx(__func__, "select timeout");
		return NULL;
	}

	if (-1 == err) {
		log_warn(__func__, "select failed");
		return NULL;
	}

	memset(&breq, 0, sizeof(breq));
	breq.type = __to_buf_type(chan);

	breq.memory = chan->mem_type;

	err = __usrp_dma_ioctl(chan->fd, USRPIOC_DQBUF, &breq);
	if (err)
		return NULL;

	if (chan->mem_type == USRP_MEMORY_MMAP) {
		buf = chan->bufs + breq.index;
	} else if (chan->mem_type == USRP_MEMORY_USERPTR) {
		for (i = 0; i < chan->nbufs; i++)
			if (breq.m.userptr == (unsigned long)chan->bufs[i].mem
			    && breq.length == chan->bufs[i].len)
				break;
		buf = chan->bufs + i;
	}

	if (chan->dir == RX)
		buf->valid_bytes = breq.bytesused;

	return buf;
}

/*
 * usrp_dma_buf_export() - Export usrp dma buffer as dmabuf fd
 * that can be shared between processes
 * @chan: context
 * @buf: buffer
 * @dmafd: dma file descriptor output
 */
int usrp_dma_buf_export(struct usrp_dma_chan *chan, struct usrp_dma_buf *buf,
			int *dmafd)
{
	struct usrp_exportbuffer breq;
	int err;

	memset(&breq, 0, sizeof(breq));
	breq.type = __to_buf_type(chan);
	breq.index = buf->index;

	err = __usrp_dma_ioctl(chan->fd, USRPIOC_EXPBUF, &breq);
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

int usrp_dma_chan_start_streaming(struct usrp_dma_chan *chan)
{
	enum usrp_buf_type type = __to_buf_type(chan);
	return __usrp_dma_ioctl(chan->fd, USRPIOC_STREAMON, (void *)type);
}

int usrp_dma_chan_stop_streaming(struct usrp_dma_chan *chan)
{
	enum usrp_buf_type type = __to_buf_type(chan);
	return __usrp_dma_ioctl(chan->fd, USRPIOC_STREAMOFF, (void *)type);
}

