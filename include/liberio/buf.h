/*
 * Copyright (c) 2017, National Instruments Corp.
 *
 * "That dude over there just punched Merica into that guy,
 *  he must be a Liberio!"
 * 	- Urban Dictionary
 *
 * SPDX-License-Identifier: GPL-2.0
 *
 */

#ifndef LIBERIO_BUF_H
#define LIBERIO_BUF_H

struct liberio_buf;

/* Buffer API */
void *liberio_buf_get_mem(const struct liberio_buf *buf, size_t plane);

void liberio_buf_set_payload(struct liberio_buf *buf, size_t plane, size_t len);
size_t liberio_buf_get_payload(const struct liberio_buf *buf, size_t plane);

size_t liberio_buf_get_len(const struct liberio_buf *buf, size_t plane);

size_t liberio_buf_get_index(const struct liberio_buf *buf);

#endif /* LIBERIO_BUF_H */
