#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <time.h>
#include <pthread.h>

#include "v4l2-stuff.h"
#include "dma.h"
#include "crc32.h"

#define NBUFS 32
#define NITER 10

static void fill_buf(struct usrp_dma_buf *buf, uint64_t val)
{
	uint32_t *vals = buf->mem;
	uint32_t crc;


	for (size_t i = 0; i < buf->len / 4 - 1; i++)
		vals[i] = val + i;

	crc = crc32(0, buf->mem, ((buf->len / 4) - sizeof(*vals)));
	vals[((buf->len -1 )/ 4)] = crc;

	buf->valid_bytes = buf->len;
}

static void check_buf(struct usrp_dma_buf *buf)
{
	uint32_t *vals = buf->mem;
	uint32_t crc;

	printf("-- Got buffer idx %zu\n", buf->index);
	crc = crc32(0, buf->mem, ((buf->len / 4) - sizeof(*vals)));

	if (crc == vals[(buf->len - 1) / 4])
		printf("-- CRC OK\n");
	else
		for (size_t i = 0; i< 20; i++)
		     printf("-- %08x\n", vals[i]);
}

struct thread_args {
	struct usrp_dma_ctx *ctx;
	pthread_barrier_t *barr;
	long tid;
};

void *rx_thread(void *args)
{
	struct thread_args *targs = args;
	struct usrp_dma_ctx *rx_ctx = targs->ctx;
	int err;

	err = usrp_dma_request_buffers(rx_ctx, NBUFS);
	if (err < 0) {
		fprintf(stderr, "failed to request rx buffers\n");
		goto out_free;
	}

	/* queue up receive buffers ... */
	for (size_t i = 0; i < rx_ctx->nbufs; i++) {
		err = usrp_dma_buf_enqueue(rx_ctx, rx_ctx->bufs + i);
		if (err) {
			fprintf(stderr, "failed to get buffer\n");
			goto out_free;
		}
	}

	pthread_barrier_wait(targs->barr);

	err = usrp_dma_ctx_start_streaming(rx_ctx);
	if (err) {
		fprintf(stderr, "failed to start streaming\n");
		goto out_free;
	}

	for (size_t i = 0; i < NITER; ++i) {
		struct usrp_dma_buf *buf;

		buf = usrp_dma_buf_dequeue(rx_ctx);
		if (!buf) {
			fprintf(stderr, "failed to get RX buffer\n");
			continue;
		}

		check_buf(buf);

		err = usrp_dma_buf_enqueue(rx_ctx, buf);
		if (err) {
			fprintf(stderr, "failed to put RX buffer\n");
			goto out_stop_streaming;
		}
	}

out_stop_streaming:
	usrp_dma_ctx_stop_streaming(rx_ctx);

out_free:
	usrp_dma_ctx_put(targs->ctx);
	pthread_exit((void*) targs->tid);
}

void *tx_thread(void *args)
{
	struct thread_args *targs = args;
	struct usrp_dma_ctx *tx_ctx = targs->ctx;
	int err;

	err = usrp_dma_request_buffers(tx_ctx, NBUFS);
	if (err < 0) {
		fprintf(stderr, "failed to request tx buffers\n");
		goto out_free;
	}

	err = usrp_dma_ctx_start_streaming(tx_ctx);
	if (err) {
		fprintf(stderr, "failed to start streaming\n");
		goto out_free;
	}

	pthread_barrier_wait(targs->barr);

	for (size_t i = 0; i < NITER; ++i) {
		struct usrp_dma_buf *buf;

		/* all buffers start out in dequeued state */
		if (i >= tx_ctx->nbufs) {
			buf = usrp_dma_buf_dequeue(tx_ctx);
			if (!buf) {
				fprintf(stderr, "failed to get TX buffer\n");
				continue;
			}
		} else {
			buf = tx_ctx->bufs + i;
		}

		fill_buf(buf, 0xace0ba5e);

		err = usrp_dma_buf_enqueue(tx_ctx, buf);
		if (err) {
			fprintf(stderr, "failed to put TX buffer\n");
			goto out_stop_streaming;
		}
	}

out_stop_streaming:
	usrp_dma_ctx_stop_streaming(tx_ctx);

out_free:
	usrp_dma_ctx_put(targs->ctx);
	pthread_exit((void*) targs->tid);
}

int main(int argc, char *argv[])
{
	pthread_t threads[2];
	void *status;
	struct thread_args args[2];
	pthread_barrier_t barr;

	pthread_barrier_init(&barr, NULL, 2);

	args[0].ctx = usrp_dma_ctx_alloc("/dev/tx-dma", TX);
	if (!args[0].ctx)
		return EXIT_FAILURE;
	args[0].barr = &barr;

	args[1].ctx = usrp_dma_ctx_alloc("/dev/rx-dma", RX);
	if (!args[1].ctx)
		goto out_err_rx;
	args[1].barr = &barr;

	/* grab ref before handing off ... */
	usrp_dma_ctx_get(args[0].ctx);
	pthread_create(&threads[0], NULL, tx_thread, (void *)&args[0]);

	/* grab ref before handing off ... */
	usrp_dma_ctx_get(args[1].ctx);
	pthread_create(&threads[1], NULL, rx_thread, (void *)&args[1]);

	for (size_t i = 0; i < 2; i++)
		pthread_join(threads[i], &status);

	pthread_exit(NULL);

	usrp_dma_ctx_put(args[1].ctx);
	usrp_dma_ctx_put(args[0].ctx);

out_err_rx:
	usrp_dma_ctx_put(args[0].ctx);

	return EXIT_SUCCESS;
}
