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

#include "v4l2-stuff.h"
#include <liberio/chan.h>
#include <liberio/liberio.h>
#include <liberio/buf.h>

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
#include <sys/stat.h>

#include <libudev.h>

#include "priv.h"

#define RETRIES 100
#define TIMEOUT 1
#define USRP_MAX_FRAMES 128

static struct liberio_chan *
__liberio_chan_alloc(struct liberio_ctx *ctx,
		     const char *file,
		     const enum liberio_direction dir,
		     enum usrp_memory mem_type);

static void __liberio_ctx_free(const struct ref *ref)
{
	struct liberio_ctx *ctx = container_of(ref, struct liberio_ctx,
					       refcnt);
	if (!ctx)
		return;

	udev_unref(ctx->udev);

	free(ctx);
}

struct liberio_ctx *liberio_ctx_new(void)
{
	struct liberio_ctx *ctx;
	struct udev *udev;
	int err;

	ctx = calloc(1, sizeof(*ctx));
	if (!ctx)
		return NULL;

	ctx->udev = udev_new();
	if (!ctx->udev)
		goto err_udev;

	ctx->refcnt = (struct ref){__liberio_ctx_free, 1};

	return ctx;

err_udev:
	free(ctx);
	return NULL;
}

inline void liberio_ctx_put(struct liberio_ctx *ctx)
{
	ref_dec(&ctx->refcnt);
}

inline void liberio_ctx_get(struct liberio_ctx *ctx)
{
	ref_inc(&ctx->refcnt);
}

struct liberio_chan *liberio_ctx_alloc_chan(struct liberio_ctx *ctx,
					    const char *file,
					    const enum liberio_direction dir,
					    enum usrp_memory mem_type)
{
	struct liberio_chan *chan;

	liberio_ctx_get(ctx);
	chan = __liberio_chan_alloc(ctx, file, dir, mem_type);
	if (!chan)
		return NULL;

	return chan;
}

void *liberio_buf_get_mem(const struct liberio_buf *buf, size_t plane)
{
	return buf->mem;
}

void liberio_buf_set_payload(struct liberio_buf *buf, size_t plane, size_t len)
{
	buf->valid_bytes = len;
}

size_t liberio_buf_get_payload(const struct liberio_buf *buf, size_t plane)
{
	return buf->valid_bytes;
}

size_t liberio_buf_get_len(const struct liberio_buf *buf, size_t plane)
{
	return buf->len;
}

size_t liberio_buf_get_index(const struct liberio_buf *buf)
{
	return buf->index;
}

void liberio_ctx_set_loglevel(struct liberio_ctx *ctx, int loglevel)
{
	(void) ctx;
	log_init(loglevel, "usrp_dma");
}

void liberio_ctx_register_logger(struct liberio_ctx *ctx, void (*cb)(int, const char *, void*),
				 void *priv)
{
	(void) ctx;
	log_register(cb, priv);
}

static void __liberio_buf_release_mmap(struct liberio_buf *buf)
{
	if (buf && buf->mem)
		munmap(buf->mem, buf->len);
}

static void __liberio_chan_free(const struct ref *ref)
{
	size_t i;
	int err;
	struct liberio_chan *chan = container_of(ref, struct liberio_chan,
						refcnt);
	if (!chan)
		return;

	if (!chan->bufs)
		goto out;

	for (i = chan->nbufs - 1; i > 0; i--)
		chan->ops->release(chan->bufs + i);

out:
	liberio_ctx_put(chan->ctx);
	close(chan->fd);
	free(chan);
}

inline void liberio_chan_put(struct liberio_chan *chan)
{
	ref_dec(&chan->refcnt);
}

inline void liberio_chan_get(struct liberio_chan *chan)
{
	ref_inc(&chan->refcnt);
}

static int __liberio_buf_init_mmap(struct liberio_chan *chan,
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

static int __liberio_buf_init_dmabuf(struct liberio_chan *chan,
				       struct liberio_buf *buf, size_t index)
{
	log_crit(__func__, "Not implemented");

	return -ENOTTY;
}

static void __liberio_buf_release_dmabuf(struct liberio_buf *buf)
{
	log_crit(__func__, "Not implemented");
}

static const struct liberio_buf_ops __liberio_buf_mmap_ops = {
	.init		=	__liberio_buf_init_mmap,
	.release	=	__liberio_buf_release_mmap,
};

static const struct liberio_buf_ops __liberio_buf_userptr_ops = {
	.init		=	__liberio_buf_init_userptr,
	.release	=	__liberio_buf_release_userptr,
};

static const struct liberio_buf_ops __liberio_buf_dmabuf_ops = {
	.init		=	__liberio_buf_init_dmabuf,
	.release	=	__liberio_buf_release_dmabuf,
};

static const char *const memstring[] = {
	[USRP_MEMORY_MMAP] = "MMAP",
	[USRP_MEMORY_USERPTR] = "USERPTR",
	[USRP_MEMORY_DMABUF] = "DMABUF",
};

const char *liberio_chan_get_type(const struct liberio_chan *chan)
{
	if ((chan->mem_type < USRP_MEMORY_MMAP) ||
	    (chan->mem_type > USRP_MEMORY_DMABUF))
		return "UNKNOWN";

	return memstring[chan->mem_type];
}

static int __liberio_get_api_from_file(const char *file, int *maj, int *min)
{
	struct udev_device *dev;
	struct udev *udev;
	struct stat statbuf;
	int err;

	err = stat(file, &statbuf);
	if (err < 0)
		return err;

	if (!S_ISCHR(statbuf.st_mode))
		return -ENODEV;

	udev = udev_new();

	dev = udev_device_new_from_devnum(udev, 'c', statbuf.st_rdev);
	*maj = strtol(udev_device_get_sysattr_value(dev, "api_maj"), NULL, 10);
	*min = strtol(udev_device_get_sysattr_value(dev, "api_min"), NULL, 10);

	udev_device_unref(dev);
	udev_unref(udev);

	return 0;
}

static int __liberio_get_chan_attr_int(struct liberio_chan *chan,
				       const char *attr,
				       int base)
{
	struct udev_device *dev;
	struct udev *udev;
	struct stat statbuf;
	int ret;

	if (!chan)
		return -ENODEV;

	ret = fstat(chan->fd, &statbuf);
	if (ret < 0)
		return ret;

	if (!S_ISCHR(statbuf.st_mode))
		return -EINVAL;

	udev = udev_new();
	if (!udev)
		return -ENOMEM;

	dev = udev_device_new_from_devnum(udev, 'c', statbuf.st_rdev);
	if (!dev)
		goto err_val;

	ret = strtol(udev_device_get_sysattr_value(dev, attr), NULL, base);

	udev_device_unref(dev);

err_val:
	udev_unref(udev);
	return ret;
}

static struct liberio_chan *
__liberio_chan_alloc(struct liberio_ctx *ctx,
		     const char *file,
		     const enum liberio_direction dir,
		     enum usrp_memory mem_type)
{
	struct liberio_chan *chan;
	int maj, min;
	int err;

	chan = calloc(1, sizeof(*chan));
	if (!chan)
		return NULL;

	chan->fd = open(file, O_RDWR);
	if (chan->fd < 0) {
		log_warn(__func__, "Failed to open device");
		goto out_free;
	}

	chan->ctx = ctx;
	chan->dir = dir;
	chan->bufs = NULL;
	chan->nbufs = 0;
	chan->mem_type = mem_type;
	chan->fix_broken_chdr = 0;
	INIT_LIST_HEAD(&chan->free_bufs);

	if (mem_type == USRP_MEMORY_MMAP) {
		chan->ops = &__liberio_buf_mmap_ops;
	} else if (mem_type == USRP_MEMORY_USERPTR) {
		chan->ops = &__liberio_buf_userptr_ops;
	} else {
		log_crit(__func__, "Invalid memory type specified");
		return NULL;
	}

	chan->refcnt = (struct ref){__liberio_chan_free, 1};
	liberio_chan_stop_streaming(chan);
	liberio_chan_request_buffers(chan, 0);

	return chan;

out_free:
	liberio_ctx_put(ctx);
	free(chan);

	return NULL;
}

int liberio_chan_request_buffers(struct liberio_chan *chan, size_t num_buffers)
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

	err = liberio_ioctl(chan->fd, USRPIOC_REQBUFS, &req);
	if (err) {
		log_crit(__func__, "failed to request buffers (num_buffers was %u) %d",
			 num_buffers, err);
		return err;
	}

	/* if we're cleaning up, no need to initialize anything, just bail */
	if (!num_buffers)
		return 0;

	chan->bufs = calloc(req.count, sizeof(struct liberio_buf));
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

int liberio_chan_set_fixed_size(struct liberio_chan *chan, size_t plane,
				size_t size)
{
	struct usrp_fmt breq;

	memset(&breq, 0, sizeof(breq));
	breq.type = USRP_FMT_CHDR_FIXED_BLOCK;
	breq.length = size;

	return liberio_ioctl(chan->fd, USRPIOC_SET_FMT, &breq);
}

size_t liberio_chan_get_num_bufs(const struct liberio_chan *chan)
{
	return chan->nbufs;
}

static uint16_t __liberio_buf_extract_chdr_length(struct liberio_buf *buf)
{
	const uint32_t *tmp_buf = buf->mem;

	return (((uint32_t *)buf->mem)[0]) & 0xffff;
}

struct liberio_buf *liberio_chan_get_buf_at_index(const struct liberio_chan *chan, size_t index)
{
	return chan->bufs + index;
}

/*
 * liberio_chan_buf_enqueue - Enqueue a buffer to the driver
 * @chan: the liberio channel to use
 * @buf: the liberio buffer to use
 */
int liberio_chan_buf_enqueue(struct liberio_chan *chan, struct liberio_buf *buf)
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

	/* For the broken_chdr case, we need to tell driver the size */
	if (chan->dir == TX || (chan->dir == RX && chan->fix_broken_chdr))
		breq.bytesused = buf->valid_bytes;

	return liberio_ioctl(chan->fd, USRPIOC_QBUF, &breq);
}

int liberio_chan_enqueue_all(struct liberio_chan *chan)
{
	size_t i;
	int err;

	if (chan->dir != RX)
		return -EINVAL;

	for (i = 0; i < chan->nbufs; i++) {
		err = liberio_chan_buf_enqueue(chan, chan->bufs + i);
		if (err)
			return err;
	}

	return 0;
}

/*
 * liberio_buf_dequeue - Dequeue a buffer from the driver
 * @chan: the liberio channel to dequeue from
 * @timeout: the timeout to use in us
 */
struct liberio_buf *liberio_chan_buf_dequeue(struct liberio_chan *chan,
					     int timeout)
{
	struct usrp_buffer breq;
	struct liberio_buf *buf;
	int err, i;
	fd_set fds;
	struct timeval tv;
	struct timeval *tv_ptr = &tv;

	// Should only happen with chan->dir == TX (see liberio_chan_request_buffers)
	if (!list_empty(&chan->free_bufs)) {
		buf = list_first_entry(&chan->free_bufs, struct liberio_buf,
				       node);
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

	err = liberio_ioctl(chan->fd, USRPIOC_DQBUF, &breq);
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

	if (chan->dir == RX && chan->fix_broken_chdr)
		buf->valid_bytes = __liberio_buf_extract_chdr_length(buf);
	else
		buf->valid_bytes = breq.bytesused;

	return buf;
}

/*
 * liberio_buf_export() - Export usrp dma buffer as dmabuf fd
 * that can be shared between processes
 * @chan: context
 * @buf: buffer
 * @dmafd: dma file descriptor output
 */
int liberio_chan_buf_export(struct liberio_chan *chan, struct liberio_buf *buf,
			    int *dmafd)
{
	struct usrp_exportbuffer breq;
	int err;

	memset(&breq, 0, sizeof(breq));
	breq.type = __to_buf_type(chan);
	breq.index = buf->index;

	err = liberio_ioctl(chan->fd, USRPIOC_EXPBUF, &breq);
	if (err) {
		log_warn(__func__, "failed to export buffer");
		return err;
	}

	*dmafd = breq.fd;

	return 0;
}

int liberio_chan_start_streaming(struct liberio_chan *chan)
{
	enum usrp_buf_type type = __to_buf_type(chan);

	return liberio_ioctl(chan->fd, USRPIOC_STREAMON, (void *)type);
}

int liberio_chan_stop_streaming(struct liberio_chan *chan)
{
	enum usrp_buf_type type = __to_buf_type(chan);

	return liberio_ioctl(chan->fd, USRPIOC_STREAMOFF, (void *)type);
}

