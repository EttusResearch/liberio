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

#include "log.h"

#define NBUFS 128
#define NITER 2500000
//#define NITER 3

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

static void print_buf(struct usrp_dma_buf *buf)
{
	uint64_t *vals = buf->mem;

	log_debug(__func__, "-- Printing buffer %u --", buf->index);

	for (size_t i = 0; i < 10; i++)
		log_debug(__func__, "%llx", vals[i]);

	log_debug(__func__, "[...]");

	for (size_t i = 500; i < 512; i++)
		log_debug(__func__, "%llx", vals[i]);

	//buf->valid_bytes = buf->len;
}

int main(int argc, char *argv[])
{
	struct usrp_dma_ctx *ctx;
	int fd;
	int err;
	uint64_t received;
	uint64_t start, end;
	uint64_t last;
	uint64_t *vals;

	usrp_dma_init();

	ctx = usrp_dma_ctx_alloc("/dev/rx-dma", RX);
	if (!ctx)
		return EXIT_FAILURE;

	err = usrp_dma_request_buffers(ctx, NBUFS);
	if (err < 0) {
		fprintf(stderr, "failed to request buffers\n");
		goto out_free;
	}

	/* queue up all the buffers, as they start out owned
	 * by the application ... */
	/*
	for (size_t i = 0; i < ctx->nbufs; i++) {
		//printf("-- Queing up buffer %u\n", i);
		err = usrp_dma_buf_enqueue(ctx, ctx->bufs + i);
		if (err) {
			fprintf(stderr, "failed to get buffer\n");
			goto out_free;
		}
	}
	*/

	log_info(__func__, "Starting streaming");
	err = usrp_dma_ctx_start_streaming(ctx);
	if (err) {
		fprintf(stderr, "failed to start streaming\n");
		goto out_free;
	}

	//getchar();

	received = 0;
	start = get_time();

	for (size_t i = 0; i < NITER; ++i) {
		struct usrp_dma_buf *buf;

		if (i < ctx->nbufs)
			usrp_dma_buf_enqueue(ctx, ctx->bufs + i);

		buf = usrp_dma_buf_dequeue(ctx);
		if (!buf) {
			fprintf(stderr, "failed to get buffer\n");
			goto out_free;
		}

		vals = buf->mem;
		if (!i) {
			last = vals[0];
		} else {
			if ((vals[0] - last) != 1)
				log_warn(__func__,
					 "Missed a packet, gap = %llu",
					 vals[0] - last);
			last = vals[0];
		}

		received += buf->len;


		if (i < NITER - 1) {
			err = usrp_dma_buf_enqueue(ctx, buf);
			if (err) {
				fprintf(stderr, "failed to get buffer\n");
				goto out_free;
			}
		}
	}

	end = get_time();

	log_info(__func__, "Stopping streaming");
	err = usrp_dma_ctx_stop_streaming(ctx);
	if (err) {
		fprintf(stderr, "failed to start streaming\n");
		goto out_free;
	}

	usrp_dma_ctx_put(ctx);

	log_info(__func__, "Received %llu bytes in %llu ns -> %f MB/s",
	       received, (end - start),
	       ((double) received / (double) (end-start) * 1e9) / 1024.0 / 1024.0);

	return 0;

out_free:
out_close:
	end = get_time();

	usrp_dma_ctx_stop_streaming(ctx);
	usrp_dma_ctx_put(ctx);

	log_info(__func__, "Received %llu bytes in %llu ns -> %f MB/s",
	       received, (end - start),
	       ((double) received / (double) (end-start) * 1e9) / 1024.0 / 1024.0);


	return err;
}
