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
#include "util.h"
#include "log.h"

#define NBUFS 32
#define NITER 2500000
//#define NITER 1


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
	uint64_t transmitted;

	usrp_dma_init(3);

	ctx = usrp_dma_ctx_alloc("/dev/tx-dma", TX, USRP_MEMORY_MMAP);
	if (!ctx)
		return EXIT_FAILURE;

	err = usrp_dma_request_buffers(ctx, NBUFS);
	if (err < 0) {
		log_crit(__func__, "failed to request buffers");
		goto out_free;
	}

	log_info(__func__, "Starting streaming (%s)",
		 usrp_dma_ctx_get_type(ctx));

	err = usrp_dma_ctx_start_streaming(ctx);
	if (err) {
		log_crit(__func__, "failed to start streaming");
		goto out_free;
	}

	transmitted = 0;
	start = get_time();

	for (size_t i = 0; i < NITER; ++i) {
		struct usrp_dma_buf *buf;

		/* buffers start out in dequeued state,
		 * so first ctx->nbufs times we don't need to deq */
		if (i >= ctx->nbufs) {
			buf = usrp_dma_buf_dequeue(ctx);
			if (!buf) {
				log_warn(__func__, "failed to get buffer");
				goto out_free;
			}
		} else {
			buf = ctx->bufs + i;
		}

		fill_buf(buf, i);
		transmitted += buf->len;

		err = usrp_dma_buf_enqueue(ctx, buf);
		if (err) {
			log_warn(__func__, "failed to get buffer");
			goto out_free;
		}
	}

	end = get_time();

	log_info(__func__, "Stopping streaming");
	err = usrp_dma_ctx_stop_streaming(ctx);
	if (err) {
		log_crit(__func__, "failed to stop streaming");
		goto out_free;
	}

	usrp_dma_ctx_put(ctx);

	log_info(__func__, "Transmitted %llu bytes in %llu ns -> %f MB/s",
	       transmitted, (end - start),
	       ((double) transmitted / (double) (end-start) * 1e9) / 1024.0 / 1024.0);

	return 0;

out_free:
out_close:
	usrp_dma_ctx_stop_streaming(ctx);
	usrp_dma_ctx_put(ctx);

	return err;
}
