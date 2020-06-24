#include <zlib.h>

#include "compress.h"

static void *zalloc(void *opaque, uint32 items, uint32 size)
{
	return rge_calloc(size, items);
};

static void zfree(void *opaque, void *address)
{
	rge_free(address);
}

static z_stream inflate_stream = ZEROMEM;
static int32 inflate_code;

size_t Inf32BufSize()
{
	return 0;
}

int32 Inf32Decode(byte *in_buf, size_t in_buf_ofs, size_t *in_buf_size, byte *out_buf, size_t out_buf_offset, size_t *out_buf_size, void *_wd, bool32 b)
{
	if (!in_buf_ofs)
	{
		memzero(&inflate_stream, sizeof(inflate_stream));

		inflate_stream.zalloc = zalloc;
		inflate_stream.zfree = zfree;

		inflate_stream.next_in = in_buf;
		inflate_stream.avail_in = *in_buf_size;

		inflate_code = inflateInit2(&inflate_stream, -15);

		if (inflate_code != Z_OK)
		{
			printf("inflate error %d: %s\n", inflate_code, inflate_stream.msg);

			return INFLATE_ERROR;
		}
	}

	if (inflate_code == Z_OK)
	{
		memzero(out_buf, *out_buf_size);

		inflate_stream.next_out = out_buf;
		inflate_stream.avail_out = *out_buf_size;

		inflate_code = inflate(&inflate_stream, Z_SYNC_FLUSH);

		// this is always 1 off after in_buf_ofs is nonzero ... no idea how it works in the game
		*in_buf_size = (*in_buf_size - inflate_stream.avail_in) - in_buf_ofs;

		*out_buf_size -= inflate_stream.avail_out;

		if (inflate_code == Z_OK) return INFLATE_OK;
	}

	if (inflate_code == Z_STREAM_END)
	{
		inflateEnd(&inflate_stream);

		return INFLATE_EOF;
	}

	printf("inflate error %d: %s\n", inflate_code, inflate_stream.msg);

	return INFLATE_ERROR;
}
