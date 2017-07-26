/*
 * Copyright (c) 2017 National Instruments Corp.
 *
 * "That dude over there just punched Merica into that guy,
 *  he must be a Liberio!"
 * 	- Urban Dictionary
 *
 * SPDX-License-Identifier: GPL-2.0
 *
 */

#ifndef LIBERIO_H
#define LIBERIO_H
#include <unistd.h>

#ifdef __cplusplus
extern "C"
{
#endif

#include <liberio/chan.h>
#include <liberio/buf.h>

enum liberio_direction {
	TX = 0,
	RX
};

struct liberio_ctx;

struct liberio_ctx *liberio_ctx_new(void);

void liberio_ctx_put(struct liberio_ctx *ctx);

void liberio_ctx_get(struct liberio_ctx *ctx);

struct liberio_chan *
liberio_ctx_alloc_chan(struct liberio_ctx *ctx,
		       const char *file,
		       const enum liberio_direction dir,
		       enum usrp_memory mem_type);

void liberio_ctx_set_loglevel(struct liberio_ctx *ctx, int loglevel);
void liberio_ctx_register_logger(struct liberio_ctx *ctx, void (*cb)(int, const char *, void*),
				 void *priv);

#ifdef __cplusplus
}
#endif

#endif /* LIBERIO_H */
