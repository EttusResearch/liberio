#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <time.h>

#include "v4l2-stuff.h"
#include "dma.h"

#define NBUFS 32
#define NITER 1000000
//#define NITER 10

static uint64_t get_time(void)
{
	struct timespec ts;
	int err;

	err = clock_gettime(CLOCK_MONOTONIC, &ts);
	if (err) {
		fprintf(stderr, "failed to get time\n");
	}

	return ((uint64_t)ts.tv_sec) * 1000 * 1000 * 1000 + ts.tv_nsec;
}

static void fill_buf(struct usrp_dma_buf *buf, uint64_t val)
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
	uint64_t start, end;

	ctx = usrp_dma_ctx_alloc("/dev/tx-dma", TX);
	if (!ctx)
		return EXIT_FAILURE;

	err = usrp_dma_request_buffers(ctx, NBUFS);
	if (err < 0) {
		fprintf(stderr, "failed to request buffers\n");
		goto out_free;
	}

	printf("-- Starting streaming\n");
	err = usrp_dma_ctx_start_streaming(ctx);
	if (err) {
		fprintf(stderr, "failed to start streaming\n");
		goto out_free;
	}

	start = get_time();

	for (size_t i = 0; i < NITER; ++i) {
		struct usrp_dma_buf *buf;

		/* buffers start out in dequeued state,
		 * so first ctx->nbufs times we don't need to deq */
		if (i >= ctx->nbufs) {
			buf = usrp_dma_buf_dequeue(ctx);
			if (!buf) {
				fprintf(stderr, "failed to get buffer\n");
				goto out_free;
			}
		} else {
			buf = ctx->bufs + i;
		}

		//fill_buf(buf, i);

		err = usrp_dma_buf_enqueue(ctx, buf);
		if (err) {
			fprintf(stderr, "failed to get buffer\n");
			goto out_free;
		}
	}

	end = get_time();

	printf("-- Stopping streaming\n");
	err = usrp_dma_ctx_stop_streaming(ctx);
	if (err) {
		fprintf(stderr, "failed to stop streaming\n");
		goto out_free;
	}

	usrp_dma_ctx_put(ctx);

	printf("-- Took %llu ns per loop\n", (end - start) / NITER);

	return 0;

out_free:
out_close:
	usrp_dma_ctx_put(ctx);

	return err;
}
