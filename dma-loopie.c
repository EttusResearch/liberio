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
#include "crc32.h"

#define NBUFS 128
#define NITER 1

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


int main(int argc, char *argv[])
{
	struct usrp_dma_ctx *tx_ctx, *rx_ctx;
	int err;

	/* request contexts & buffers ... */
	tx_ctx = usrp_dma_ctx_alloc("/dev/tx-dma", TX);
	if (!tx_ctx)
		return EXIT_FAILURE;

	err = usrp_dma_request_buffers(tx_ctx, NBUFS);
	if (err < 0) {
		fprintf(stderr, "failed to request buffers\n");
		goto out_free_tx;
	}

	/* request contexts & buffers ... */
	rx_ctx = usrp_dma_ctx_alloc("/dev/rx-dma", RX);
	if (!rx_ctx)
		return EXIT_FAILURE;

	err = usrp_dma_request_buffers(rx_ctx, NBUFS);
	if (err < 0) {
		fprintf(stderr, "failed to request buffers\n");
		goto out_free_rx;
	}
	printf("-- Requested buffers ...\n");


	/* queue up buffers */
	/*
	for (size_t i = 0; i < rx_ctx->nbufs; i++) {
		err = usrp_dma_buf_enqueue(rx_ctx, rx_ctx->bufs + i);
		if (err) {
			fprintf(stderr, "failed to get buffer\n");
			goto out_free_rx;
		}
	}
	*/

	//printf("-- Enqueued rx buffers ...\n");

	/* start streaming ... */
	err = usrp_dma_ctx_start_streaming(rx_ctx);
	if (err) {
		fprintf(stderr, "failed to start streaming\n");
		goto out_free_rx;
	}
	printf("-- Started rx streaming ...\n");

	err = usrp_dma_ctx_start_streaming(tx_ctx);
	if (err) {
		fprintf(stderr, "failed to start streaming\n");
		goto out_free_rx;
	}
	printf("-- Started tx streaming ...\n");

	for (size_t i = 0; i < NITER; ++i) {
		struct usrp_dma_buf *buf;

		/* all buffers start out in dequeued state */
		if (i >= NBUFS) {
			buf = usrp_dma_buf_dequeue(tx_ctx);
			if (!buf) {
				fprintf(stderr, "failed to get TX buffer\n");
				goto out_stop_streaming;
			}
		} else {
			buf = tx_ctx->bufs + i;
		}

		fill_buf(buf, 0xace0ba5e);

		if (i < NITER - 1) {
			err = usrp_dma_buf_enqueue(tx_ctx, buf);
			if (err) {
				fprintf(stderr, "failed to put TX buffer\n");
				goto out_stop_streaming;
			}
		}
	}

	for (size_t i = 0; i < NITER; ++i) {
		struct usrp_dma_buf *buf;

		buf = usrp_dma_buf_dequeue(rx_ctx);
		if (!buf) {
			fprintf(stderr, "failed to get RX buffer\n");
			goto out_stop_streaming;
		}

		check_buf(buf);

		if (i < NITER - 1) {
			err = usrp_dma_buf_enqueue(rx_ctx, buf);
			if (err) {
				fprintf(stderr, "failed to put RX buffer\n");
				goto out_stop_streaming;
			}
		}
	}

	usrp_dma_ctx_stop_streaming(tx_ctx);
	usrp_dma_ctx_stop_streaming(rx_ctx);

	usrp_dma_ctx_put(rx_ctx);
	usrp_dma_ctx_put(tx_ctx);

	return 0;

out_stop_streaming:
	usrp_dma_ctx_stop_streaming(tx_ctx);
	usrp_dma_ctx_stop_streaming(rx_ctx);

out_free_rx:
	usrp_dma_ctx_put(rx_ctx);

out_free_tx:
	usrp_dma_ctx_put(tx_ctx);

	return err;
}

