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
#include "log.h"

uint32_t nbufs = 128;
uint32_t niter = 25000;

int done;
pthread_spinlock_t lock;

static inline int is_tx_done(void)
{
	int tx_done;

	pthread_spin_lock(&lock);
	tx_done = done;
	pthread_spin_unlock(&lock);

	return tx_done;
}

void tx_set_done(void)
{
	pthread_spin_lock(&lock);
	done = 1;
	pthread_spin_unlock(&lock);
}

static void print_usage(void)
{
	printf("dma-loopback: USRP DMA Benchmark tool\n\n"
	       "Options:\n"
	       "        -n (number of iterations) e.g. 25000\n"
	       "        -b (number of buffers) e.g. 128\n"
	       "        -h (print this help message)\n\n"
	       "Example:\n"
	       "$ ./dma-loopack -n 25000 -b 128\n");
}

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
	uint32_t *vals = buf->mem;
	uint32_t crc;


	for (size_t i = 0; i < buf->len / 4 - 1; i++)
		vals[i] = val + i;

	//crc = crc32(0, buf->mem, ((buf->len / 4) - sizeof(*vals)));
	//vals[((buf->len -1 )/ 4)] = crc;

	buf->valid_bytes = buf->len;
}

static void check_buf(struct usrp_dma_buf *buf)
{
	uint32_t *vals = buf->mem;
	uint32_t crc;

	//printf("-- Got buffer idx %zu\n", buf->index);
	crc = crc32(0, buf->mem, ((buf->len / 4) - sizeof(*vals)));

	if (crc != vals[(buf->len - 1) / 4])
		printf("-- CRC FAILED\n");
	/*
	else
		for (size_t i = 0; i< 20; i++)
		     printf("-- %08x\n", vals[i]);
		     */
}

struct thread_args {
	struct usrp_dma_ctx *ctx;
	long tid;
};

void *rx_thread(void *args)
{
	struct thread_args *targs = args;
	struct usrp_dma_ctx *rx_ctx = targs->ctx;
	int err, tx_done;
	uint64_t count = 0;
	uint64_t start, end;

	err = usrp_dma_request_buffers(rx_ctx, nbufs);
	if (err < 0) {
		fprintf(stderr, "failed to request rx buffers\n");
		goto out_free;
	}

	/* queue up receive buffers ... */
	for (size_t i = 0; i < rx_ctx->nbufs; i++) {
		err = usrp_dma_buf_enqueue(rx_ctx, rx_ctx->bufs + i);
		if (err)
			goto out_free;
	}

	log_info(__func__, "Starting streaming");
	err = usrp_dma_ctx_start_streaming(rx_ctx);
	if (err) {
		fprintf(stderr, "failed to start streaming\n");
		goto out_free;
	}

	start = get_time();

	while (!is_tx_done())
	{
		struct usrp_dma_buf *buf;

		buf = usrp_dma_buf_dequeue(rx_ctx);
		if (!buf) {
			fprintf(stderr, "failed to get RX buffer\n");
			goto out_stop_streaming;
		}

		//check_buf(buf);
		count += buf->len;

		err = usrp_dma_buf_enqueue(rx_ctx, buf);
		if (err) {
			fprintf(stderr, "failed to put RX buffer\n");
			goto out_stop_streaming;
		}
	}

out_stop_streaming:
	end = get_time();

	log_info(__func__, "Stopping streaming");

	usrp_dma_ctx_stop_streaming(rx_ctx);

	log_info(__func__, "Transmitted %llu bytes in %llu ns -> %f MB/s",
	       count, (end - start), ((double) count / (double) (end-start) * 1e9) / 1024.0 / 1024.0);


out_free:
	usrp_dma_ctx_put(targs->ctx);
	pthread_exit((void*) targs->tid);
}

void *tx_thread(void *args)
{
	struct thread_args *targs = args;
	struct usrp_dma_ctx *tx_ctx = targs->ctx;
	int err;

	err = usrp_dma_request_buffers(tx_ctx, nbufs);
	if (err < 0) {
		fprintf(stderr, "failed to request tx buffers\n");
		goto out_free;
	}

	log_info(__func__, "Starting streaming");
	err = usrp_dma_ctx_start_streaming(tx_ctx);
	if (err) {
		fprintf(stderr, "failed to start streaming\n");
		goto out_free;
	}

	for (size_t i = 0; i < niter; ++i) {
		struct usrp_dma_buf *buf;

		/* all buffers start out in dequeued state */
		if (i >= tx_ctx->nbufs) {
			buf = usrp_dma_buf_dequeue(tx_ctx);
			if (!buf)
				goto out_stop_streaming;
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

	tx_set_done();

out_stop_streaming:
	log_info(__func__, "Stopping streaming");
	usrp_dma_ctx_stop_streaming(tx_ctx);

out_free:
	usrp_dma_ctx_put(targs->ctx);
	pthread_exit((void*) targs->tid);
}

int main(int argc, char *argv[])
{
	pthread_t threads[2];
	void *status;
	int opt;
	struct thread_args args[2];

	usrp_dma_init();

	while ((opt = getopt(argc, argv, "n:b:")) != -1) {
		switch (opt) {
		case 'b':
			nbufs = atoi(optarg);
			break;
		case 'n':
			niter = atoi(optarg);
			break;
		default:
			print_usage();
			return EXIT_FAILURE;
		}
	}

	log_info(__func__, "nbufs = %u, iterations = %u", nbufs, niter);

	pthread_spin_init(&lock, PTHREAD_PROCESS_PRIVATE);
	done = 0;

	args[0].ctx = usrp_dma_ctx_alloc("/dev/tx-dma", TX);
	if (!args[0].ctx)
		return EXIT_FAILURE;

	args[1].ctx = usrp_dma_ctx_alloc("/dev/rx-dma", RX);
	if (!args[1].ctx)
		goto out_err_rx;

	/* grab ref before handing off ... */
	usrp_dma_ctx_get(args[0].ctx);
	pthread_create(&threads[0], NULL, tx_thread, (void*)&args[0]);

	/* grab ref before handing off ... */
	usrp_dma_ctx_get(args[1].ctx);
	pthread_create(&threads[1], NULL, rx_thread, (void*)&args[1]);

	for (size_t i = 0; i < 2; i++)
		pthread_join(threads[i], &status);

	pthread_exit(NULL);

	usrp_dma_ctx_put(args[1].ctx);
	usrp_dma_ctx_put(args[0].ctx);

out_err_rx:
	usrp_dma_ctx_put(args[0].ctx);

	return EXIT_SUCCESS;
}
