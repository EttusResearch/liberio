#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#define __u32 uint32_t
#define __s32 int32_t
#define __u8 uint8_t

struct v4l2_timecode {
	__u32	type;
	__u32	flags;
	__u8	frames;
	__u8	seconds;
	__u8	minutes;
	__u8	hours;
	__u8	userbits[4];
};


enum v4l2_buf_type {
	V4L2_BUF_TYPE_VIDEO_CAPTURE        = 1,
	V4L2_BUF_TYPE_VIDEO_OUTPUT         = 2,
	V4L2_BUF_TYPE_VIDEO_OVERLAY        = 3,
	V4L2_BUF_TYPE_VBI_CAPTURE          = 4,
	V4L2_BUF_TYPE_VBI_OUTPUT           = 5,
	V4L2_BUF_TYPE_SLICED_VBI_CAPTURE   = 6,
	V4L2_BUF_TYPE_SLICED_VBI_OUTPUT    = 7,
	V4L2_BUF_TYPE_VIDEO_OUTPUT_OVERLAY = 8,
	V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE = 9,
	V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE  = 10,
	V4L2_BUF_TYPE_SDR_CAPTURE          = 11,
	V4L2_BUF_TYPE_SDR_OUTPUT           = 12,
	/* Deprecated, do not use */
	V4L2_BUF_TYPE_PRIVATE              = 0x80,
};

enum v4l2_memory {
	V4L2_MEMORY_MMAP             = 1,
	V4L2_MEMORY_USERPTR          = 2,
	V4L2_MEMORY_OVERLAY          = 3,
	V4L2_MEMORY_DMABUF           = 4,
};

struct v4l2_requestbuffers {
	__u32			count;
	__u32			type;		/* enum v4l2_buf_type */
	__u32			memory;		/* enum v4l2_memory */
	__u32			reserved[2];
};

struct v4l2_plane {
	__u32			bytesused;
	__u32			length;
	union {
		__u32		mem_offset;
		unsigned long	userptr;
		__s32		fd;
	} m;
	__u32			data_offset;
	__u32			reserved[11];
};

struct v4l2_buffer {
	__u32			index;
	__u32			type;
	__u32			bytesused;
	__u32			flags;
	__u32			field;
	struct timeval		timestamp;
	struct v4l2_timecode	timecode;
	__u32			sequence;

	/* memory location */
	__u32			memory;
	union {
		__u32           offset;
		unsigned long   userptr;
		struct v4l2_plane *planes;
		__s32		fd;
	} m;
	__u32			length;
	__u32			reserved2;
	__u32			reserved;
};


#define VIDIOC_REQBUFS		_IOWR('V',  8, struct v4l2_requestbuffers)
#define VIDIOC_QUERYBUF		_IOWR('V',  9, struct v4l2_buffer)
#define VIDIOC_QBUF		_IOWR('V', 15, struct v4l2_buffer)
#define VIDIOC_EXPBUF		_IOWR('V', 16, struct v4l2_exportbuffer)
#define VIDIOC_DQBUF		_IOWR('V', 17, struct v4l2_buffer)
#define VIDIOC_STREAMON		 _IOW('V', 18, int)
#define VIDIOC_STREAMOFF	 _IOW('V', 19, int)

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
};

void usrp_dma_buf_unmap(struct usrp_dma_buf *buf);

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

	return ctx;

out_free:
	free(ctx);

	return NULL;
}

void usrp_ctx_free(struct usrp_dma_ctx *ctx)
{
	size_t i;

	if (!ctx)
		return;

	for (i = ctx->nbufs; i > 0; i--)
		usrp_dma_buf_unmap(ctx->bufs + i);

	close(ctx->fd);
	free(ctx);
}

int usrp_dma_buf_init(struct usrp_dma_ctx *ctx, struct usrp_dma_buf *buf,
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

	buf->mem = mmap(NULL, breq.length,
			PROT_READ | PROT_WRITE,
			MAP_SHARED, ctx->fd,
			breq.m.offset);
	if (buf->mem == MAP_FAILED) {
		fprintf(stderr, "failed to mmap buffer with index %u", index);
		return err;
	}

	printf("%s: usrp_dma_buf with index %u, mapped at %p\n",
	       __func__, index, buf->mem);

	return err;
}

void usrp_dma_buf_unmap(struct usrp_dma_buf *buf)
{
	printf("%s: usrp_dma_buf unmapping index %u\n",
	       __func__, buf->index);

	if (buf && buf->mem)
		munmap(buf->mem, buf->len);
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


#define NBUFS 24

void fill_buf(struct usrp_dma_buf *buf, uint64_t val)
{
	uint64_t *vals = buf->mem;

	//printf("filling buffer %u\n", buf->index);

	for (size_t i = 0; i < buf->len / 8; i++)
		vals[i] = val;

	buf->valid_bytes = buf->len;
}

int main(int argc, char *argv[])
{
	struct usrp_dma_ctx *ctx;
	int fd;
	int err = 0;

	ctx = usrp_dma_ctx_alloc("/dev/tx-dma", TX);
	if (!ctx)
		return EXIT_FAILURE;

	err = usrp_dma_request_buffers(ctx, NBUFS);
	if (err < 0) {
		fprintf(stderr, "failed to request buffers\n");
		goto out_free;
	}

	for (size_t i = 0; i < ctx->nbufs; i++) {
		fill_buf(ctx->bufs + i, i);
		err = usrp_dma_buf_enqueue(ctx, ctx->bufs + i);
		if (err) {
			fprintf(stderr, "failed to get buffer\n");
			goto out_free;
		}
	}

	printf("-- Before streamon\n");
	err = usrp_dma_ctx_start_streaming(ctx);
	if (err) {
		fprintf(stderr, "failed to start streaming\n");
		goto out_free;
	}

	for (size_t i = 0; i < 1000000; ++i) {
		struct usrp_dma_buf *buf;

		buf = usrp_dma_buf_dequeue(ctx);
		if (!buf) {
			fprintf(stderr, "failed to get buffer\n");
			goto out_free;
		}
		fill_buf(buf, i);

		err = usrp_dma_buf_enqueue(ctx, buf);
		if (err) {
			fprintf(stderr, "failed to get buffer\n");
			goto out_free;
		}
	}

	err = usrp_dma_ctx_stop_streaming(ctx);
	if (err) {
		fprintf(stderr, "failed to start streaming\n");
		goto out_free;
	}

	usrp_ctx_free(ctx);

	return 0;

	struct v4l2_requestbuffers req;
out_free:
	req.count = 0;
	err = ioctl(ctx->fd, VIDIOC_REQBUFS, &req);
	if (err)
		fprintf(stderr, "failed to free buffers\n");

out_close:
	usrp_ctx_free(ctx);

	return err;
}
