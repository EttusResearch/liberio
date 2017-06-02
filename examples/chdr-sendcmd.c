#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <time.h>

#include <liberio/dma.h>
#include "../src/log.h"

#include <arpa/inet.h>

#define NBUFS 32
#define NITER 250
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

static void fill_buf(struct usrp_dma_buf *buf, uint16_t seqno)
{
	uint32_t *vals = buf->mem;

	//printf("filling buffer %u\n", buf->index);
       	vals[0]	= 0x00000250;
	vals[1] = 0x80010010;
	vals[2] = 0x00000000;
       	vals[3] = 0x0000007f;
	//vals[0] = 0x8001001000000230 | (((uint64_t) seqno & 0xFFF) << 48);
	/*vals[0] = htonl(0x00000230);
	vals[0] <<= 32;
	vals[0] |= htonl(0x80000010 | ((seqno & 0xFFF) << 16));
	vals[1] = htonl(0x0000007F);
	vals[1] <<= 32;*/
	//vals[1] = 127;

	buf->valid_bytes = 16;
}

int main(int argc, char *argv[])
{
	struct usrp_dma_ctx *ctx;
	int fd;
	int err = 0;
	uint64_t start, end;
	uint64_t transmitted;

	usrp_dma_init(3);

	ctx = usrp_dma_ctx_alloc("/dev/tx-dma0", TX, USRP_MEMORY_MMAP);
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

//	for (size_t i = 0; i < NITER; ++i) {
	size_t i = 0;
		struct usrp_dma_buf *buf;

		/* buffers start out in dequeued state,
		 * so first ctx->nbufs times we don't need to deq */
		if (i >= ctx->nbufs) {
			buf = usrp_dma_buf_dequeue(ctx, 250000);
			if (!buf) {
				log_warn(__func__, "failed to get buffer");
				goto out_free;
			}
		} else {
			buf = ctx->bufs + i;
		}

		fill_buf(buf, i);
		transmitted += buf->valid_bytes;

		err = usrp_dma_buf_enqueue(ctx, buf);
		if (err) {
			log_warn(__func__, "failed to get buffer");
			goto out_free;
		}
//	}

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
