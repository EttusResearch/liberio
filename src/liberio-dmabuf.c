static int __liberio_buf_init_dmabuf(struct liberio_chan *chan,
				       struct liberio_buf *buf, size_t index)
{
	log_crit(__func__, "Not implemented");

	return -ENOTTY;
}

static void __liberio_buf_release_dmabuf(struct liberio_buf *buf)
{
	log_crit(__func__, "Not implemented");
}

const struct liberio_buf_ops liberio_buf_dmabuf_ops = {
	.init		=	__liberio_buf_init_dmabuf,
	.release	=	__liberio_buf_release_dmabuf,
};
