#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <time.h>

#include <liberio/liberio.h>

#include "../src/log.h"

#define NBUFS 128
#define NITER 250
//#define NITER 3

static uint64_t get_time(void)
{
	struct timespec ts;
	int err;

	err = clock_gettime(CLOCK_MONOTONIC, &ts);
	if (err) {
		log_crit(__func__, "failed to get time");
	}

	return ((uint64_t)ts.tv_sec) * 1000 * 1000 * 1000 + ts.tv_nsec;
}

static void print_buf(struct liberio_buf *buf)
{
	uint64_t *vals = liberio_buf_get_mem(buf, 0);

	log_debug(__func__, "-- Printing buffer %u --", liberio_buf_get_index(buf));

	for (size_t i = 0; (i < 10) && (8*i < liberio_buf_get_len(buf, 0)); i++)
		log_debug(__func__, "%08llx", vals[i]);

	log_debug(__func__, "[...]");

	for (size_t i = 500; (i < 512) && (8*i < liberio_buf_get_len(buf, 0)); i++)
		log_debug(__func__, "%08llx", vals[i]);

	//buf->valid_bytes = buf->len;
}

int main(int argc, char *argv[])
{
	struct liberio_chan *chan;
	struct liberio_ctx *ctx;
	int fd;
	int err;
	uint64_t received;
	uint64_t start, end;
	uint64_t last;
	uint64_t *vals;

	ctx = liberio_ctx_new();
	if (!ctx)
		return -1;

	liberio_ctx_set_loglevel(ctx, 3);

	chan = liberio_ctx_alloc_chan(ctx, "/dev/rx-dma0", RX,
				      USRP_MEMORY_MMAP);
	liberio_ctx_put(ctx);
	if (!chan)
		return EXIT_FAILURE;

	liberio_chan_stop_streaming(chan);
	liberio_chan_set_fixed_size(chan, 0, 8192);

	err = liberio_chan_request_buffers(chan, NBUFS);
	if (err < 0) {
		log_crit(__func__, "failed to request buffers");
		goto out_free;
	}

	/* queue up all the buffers, as they start out owned
	 * by the application ... */

	for (size_t i = 0; i < liberio_chan_get_num_bufs(chan); i++) {
		struct liberio_buf *buf =
			liberio_chan_get_buf_at_index(chan, i);
		/*
		//printf("-- Queing up buffer %u\n", i);
		err = liberio_buf_enqueue(chan, chan->bufs + i);
		if (err) {
			log_warn(__func__, "failed to get buffer");
			goto out_free;
		}
		*/
		liberio_buf_set_payload(buf, 0, 16);
	}

	log_info(__func__, "Starting streaming (%s)",
		 liberio_chan_get_type(chan));

	err = liberio_chan_start_streaming(chan);
	if (err) {
		log_crit(__func__, "failed to start streaming\n");
		goto out_free;
	}

	received = 0;
	start = get_time();

	for (size_t i = 0; i < NITER; ++i) {
		struct liberio_buf *buf;

		if (i < liberio_chan_get_num_bufs(chan))
			liberio_chan_buf_enqueue(chan,
						 liberio_chan_get_buf_at_index(chan, i));

		buf = liberio_chan_buf_dequeue(chan, -1);
		if (!buf) {
			log_warn(__func__, "failed to get buffer");
			goto out_free;
		}

		print_buf(buf);
		received += liberio_buf_get_len(buf, 0);


		if (i < NITER - 1) {
			err = liberio_chan_buf_enqueue(chan, buf);
			if (err) {
				log_warn(__func__, "failed to get buffer");
				goto out_free;
			}
		}
	}

	end = get_time();

	log_info(__func__, "Stopping streaming");
	err = liberio_chan_stop_streaming(chan);
	if (err) {
		log_crit(__func__, "failed to start streaming");
		goto out_free;
	}

	liberio_chan_put(chan);

	log_info(__func__, "Received %llu bytes in %llu ns -> %f MB/s",
	       received, (end - start),
	       ((double) received / (double) (end-start) * 1e9) / 1024.0 / 1024.0);

	return 0;

out_free:
out_close:
	end = get_time();

	liberio_chan_stop_streaming(chan);
	liberio_chan_put(chan);

	log_info(__func__, "Received %llu bytes in %llu ns -> %f MB/s",
	       received, (end - start),
	       ((double) received / (double) (end-start) * 1e9) / 1024.0 / 1024.0);


	return err;
}
