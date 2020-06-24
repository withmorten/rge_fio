// use zlib deflate instead of game deflate, produces slightly smaller output files and is a lot faster,
// but uses more than infile size memory vs a fixed UINT16_MAX buffer size
// #define USE_ZLIB_DEFLATE

#ifdef USE_ZLIB_DEFLATE
#include <zlib.h>
#endif

#include "compress.h"

#ifdef USE_ZLIB_DEFLATE
static void *zalloc(void *opaque, uint32 items, uint32 size)
{
	return rge_calloc(size, items);
};

static void zfree(void *opaque, void *address)
{
	rge_free(address);
}

static z_stream deflate_stream = ZEROMEM;
static int32 deflate_code;

static byte *buffer = NULL;
static size_t buffer_len = 0;

#define DEFAULT_ALLOC 0x10000

static byte *data = NULL;
static size_t data_alloc = 0;
static size_t data_pos = 0;

static int32 (*flush_out_buf)(byte *out_buf_ofs, int32 out_buf_size) = NULL;

size_t deflate_buf_size()
{
	return 0;
}

int32 deflate_init(void *_wd, int32 max_compares, int32 strategy, bool32 greedy_flag, byte *out_buf_ofs, int32 out_buf_size, int32 (*out_buf_flush)(byte *, int32))
{
	buffer = out_buf_ofs;
	buffer_len = out_buf_size;
	flush_out_buf = out_buf_flush;

	data_alloc = DEFAULT_ALLOC;
	data = (byte *)rge_malloc(DEFAULT_ALLOC);
	data_pos = 0;

	return DEFLATE_INIT;
}

int32 deflate_data(void *_wd, byte *in_buf_ofs, int32 in_buf_size, bool32 eof_flag)
{
	if (!eof_flag)
	{
		if (data_pos + in_buf_size > data_alloc)
		{
			size_t new_alloc = (data_alloc * 2) + in_buf_size;
			byte *new_data = (byte *)rge_malloc(new_alloc);

			memcpy(new_data, data, data_alloc);
			rge_free(data);

			data_alloc = new_alloc;
			data = new_data;
		}

		memcpy(data + data_pos, in_buf_ofs, in_buf_size);

		data_pos += in_buf_size;
	}
	else
	{
		memzero(&deflate_stream, sizeof(deflate_stream));

		deflate_stream.zalloc = zalloc;
		deflate_stream.zfree = zfree;

		deflate_stream.next_in = data;
		deflate_stream.avail_in = data_pos + 1;

		deflate_code = deflateInit2(&deflate_stream, -1, Z_DEFLATED, -15, 9, Z_RLE);

		if (deflate_code != Z_OK)
		{
			printf("deflate error %d: %s\n", deflate_code, deflate_stream.msg);

			return DEFLATE_ERROR;
		}

		while (deflate_code == Z_OK)
		{
			memzero(buffer, buffer_len);

			deflate_stream.next_out = buffer;
			deflate_stream.avail_out = buffer_len;

			deflate_code = deflate(&deflate_stream, Z_FINISH);

			if (deflate_code == Z_OK || deflate_code == Z_STREAM_END)
			{
				flush_out_buf(buffer, buffer_len - deflate_stream.avail_out);
			}
			else
			{
				printf("deflate error %d: %s\n", deflate_code, deflate_stream.msg);

				deflateEnd(&deflate_stream);

				return DEFLATE_ERROR;
			}
		}

		deflateEnd(&deflate_stream);
	}

	return DEFLATE_OK;
}

void deflate_deinit(void *_wd)
{
	buffer = NULL;
	buffer_len = 0;

	rge_free(data);
	data_alloc = 0;
	data_pos = 0;
}
#else
#include "deflate.h"

static work_data *wd = NULL;

static byte *dict = NULL;
static uint16 *hash = NULL;
static uint16 *next = NULL;
static uint16 *last = NULL;

static int32 max_compares = 0;
static int32 match_len = 0;
static uint32 match_pos = 0;

static uint32 bit_buf = 0;
static int32 bit_buf_len = 0;
static bool32 bit_buf_total_flag = 0;

static byte *out_buf_cur_ofs = NULL;
static int32 out_buf_left = 0;

static int32 code_list_len = 0;
static int32 num_codes[33] = ZEROMEM;
static int32 next_code[33] = ZEROMEM;
static int32 new_code_sizes[288] = ZEROMEM;
static int32 code_list[288] = ZEROMEM;
static int32 others[288] = ZEROMEM;
static int32 heap[289] = ZEROMEM;

static uint16 len_code[256] =
{
	0x101, 0x102, 0x103, 0x104, 0x105, 0x106, 0x107, 0x108, 0x109,
	0x109, 0x10A, 0x10A, 0x10B, 0x10B, 0x10C, 0x10C, 0x10D, 0x10D,
	0x10D, 0x10D, 0x10E, 0x10E, 0x10E, 0x10E, 0x10F, 0x10F, 0x10F,
	0x10F, 0x110, 0x110, 0x110, 0x110, 0x111, 0x111, 0x111, 0x111,
	0x111, 0x111, 0x111, 0x111, 0x112, 0x112, 0x112, 0x112, 0x112,
	0x112, 0x112, 0x112, 0x113, 0x113, 0x113, 0x113, 0x113, 0x113,
	0x113, 0x113, 0x114, 0x114, 0x114, 0x114, 0x114, 0x114, 0x114,
	0x114, 0x115, 0x115, 0x115, 0x115, 0x115, 0x115, 0x115, 0x115,
	0x115, 0x115, 0x115, 0x115, 0x115, 0x115, 0x115, 0x115, 0x116,
	0x116, 0x116, 0x116, 0x116, 0x116, 0x116, 0x116, 0x116, 0x116,
	0x116, 0x116, 0x116, 0x116, 0x116, 0x116, 0x117, 0x117, 0x117,
	0x117, 0x117, 0x117, 0x117, 0x117, 0x117, 0x117, 0x117, 0x117,
	0x117, 0x117, 0x117, 0x117, 0x118, 0x118, 0x118, 0x118, 0x118,
	0x118, 0x118, 0x118, 0x118, 0x118, 0x118, 0x118, 0x118, 0x118,
	0x118, 0x118, 0x119, 0x119, 0x119, 0x119, 0x119, 0x119, 0x119,
	0x119, 0x119, 0x119, 0x119, 0x119, 0x119, 0x119, 0x119, 0x119,
	0x119, 0x119, 0x119, 0x119, 0x119, 0x119, 0x119, 0x119, 0x119,
	0x119, 0x119, 0x119, 0x119, 0x119, 0x119, 0x119, 0x11A, 0x11A,
	0x11A, 0x11A, 0x11A, 0x11A, 0x11A, 0x11A, 0x11A, 0x11A, 0x11A,
	0x11A, 0x11A, 0x11A, 0x11A, 0x11A, 0x11A, 0x11A, 0x11A, 0x11A,
	0x11A, 0x11A, 0x11A, 0x11A, 0x11A, 0x11A, 0x11A, 0x11A, 0x11A,
	0x11A, 0x11A, 0x11A, 0x11B, 0x11B, 0x11B, 0x11B, 0x11B, 0x11B,
	0x11B, 0x11B, 0x11B, 0x11B, 0x11B, 0x11B, 0x11B, 0x11B, 0x11B,
	0x11B, 0x11B, 0x11B, 0x11B, 0x11B, 0x11B, 0x11B, 0x11B, 0x11B,
	0x11B, 0x11B, 0x11B, 0x11B, 0x11B, 0x11B, 0x11B, 0x11B, 0x11C,
	0x11C, 0x11C, 0x11C, 0x11C, 0x11C, 0x11C, 0x11C, 0x11C, 0x11C,
	0x11C, 0x11C, 0x11C, 0x11C, 0x11C, 0x11C, 0x11C, 0x11C, 0x11C,
	0x11C, 0x11C, 0x11C, 0x11C, 0x11C, 0x11C, 0x11C, 0x11C, 0x11C,
	0x11C, 0x11C, 0x11C, 0x11D,
};

static byte len_extra[256] =
{
	0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2,
	2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3,
	3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
	3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4,
	4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
	4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
	4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
	4, 4, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
	5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
	5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
	5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
	5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
	5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
	5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
	5, 5, 5, 0,
};

static byte len_mask[256] =
{
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01,
	0x01, 0x01, 0x01, 0x01, 0x01, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
	0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x07,
	0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
	0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
	0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x0F, 0x0F,
	0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F,
	0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F,
	0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F,
	0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F,
	0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F,
	0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x1F, 0x1F, 0x1F, 0x1F,
	0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F,
	0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F,
	0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F,
	0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F,
	0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F,
	0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F,
	0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F,
	0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F,
	0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F,
	0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F,
	0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F,
	0x1F, 0x1F, 0x00,
};

static byte dist_hi_code[128] =
{
	0x00, 0x00, 0x12, 0x13, 0x14, 0x14, 0x15, 0x15, 0x16, 0x16, 0x16,
	0x16, 0x17, 0x17, 0x17, 0x17, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18,
	0x18, 0x18, 0x19, 0x19, 0x19, 0x19, 0x19, 0x19, 0x19, 0x19, 0x1A,
	0x1A, 0x1A, 0x1A, 0x1A, 0x1A, 0x1A, 0x1A, 0x1A, 0x1A, 0x1A, 0x1A,
	0x1A, 0x1A, 0x1A, 0x1A, 0x1B, 0x1B, 0x1B, 0x1B, 0x1B, 0x1B, 0x1B,
	0x1B, 0x1B, 0x1B, 0x1B, 0x1B, 0x1B, 0x1B, 0x1B, 0x1B, 0x1C, 0x1C,
	0x1C, 0x1C, 0x1C, 0x1C, 0x1C, 0x1C, 0x1C, 0x1C, 0x1C, 0x1C, 0x1C,
	0x1C, 0x1C, 0x1C, 0x1C, 0x1C, 0x1C, 0x1C, 0x1C, 0x1C, 0x1C, 0x1C,
	0x1C, 0x1C, 0x1C, 0x1C, 0x1C, 0x1C, 0x1C, 0x1C, 0x1D, 0x1D, 0x1D,
	0x1D, 0x1D, 0x1D, 0x1D, 0x1D, 0x1D, 0x1D, 0x1D, 0x1D, 0x1D, 0x1D,
	0x1D, 0x1D, 0x1D, 0x1D, 0x1D, 0x1D, 0x1D, 0x1D, 0x1D, 0x1D, 0x1D,
	0x1D, 0x1D, 0x1D, 0x1D, 0x1D, 0x1D, 0x1D,
};

static byte dist_hi_extra[128] =
{
	0x00, 0x00, 0x08, 0x08, 0x09, 0x09, 0x09, 0x09, 0x0A, 0x0A, 0x0A,
	0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B,
	0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0C,
	0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C,
	0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C,
	0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0D, 0x0D,
	0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D,
	0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D,
	0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D,
	0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D,
	0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D,
	0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D,
};

static uint16 dist_hi_mask[128] =
{
	0x0000, 0x0000, 0x00FF, 0x00FF, 0x01FF, 0x01FF, 0x01FF, 0x01FF,
	0x03FF, 0x03FF, 0x03FF, 0x03FF, 0x03FF, 0x03FF, 0x03FF, 0x03FF,
	0x07FF, 0x07FF, 0x07FF, 0x07FF, 0x07FF, 0x07FF, 0x07FF, 0x07FF,
	0x07FF, 0x07FF, 0x07FF, 0x07FF, 0x07FF, 0x07FF, 0x07FF, 0x07FF,
	0x0FFF, 0x0FFF, 0x0FFF, 0x0FFF, 0x0FFF, 0x0FFF, 0x0FFF, 0x0FFF,
	0x0FFF, 0x0FFF, 0x0FFF, 0x0FFF, 0x0FFF, 0x0FFF, 0x0FFF, 0x0FFF,
	0x0FFF, 0x0FFF, 0x0FFF, 0x0FFF, 0x0FFF, 0x0FFF, 0x0FFF, 0x0FFF,
	0x0FFF, 0x0FFF, 0x0FFF, 0x0FFF, 0x0FFF, 0x0FFF, 0x0FFF, 0x0FFF,
	0x1FFF, 0x1FFF, 0x1FFF, 0x1FFF, 0x1FFF, 0x1FFF, 0x1FFF, 0x1FFF,
	0x1FFF, 0x1FFF, 0x1FFF, 0x1FFF, 0x1FFF, 0x1FFF, 0x1FFF, 0x1FFF,
	0x1FFF, 0x1FFF, 0x1FFF, 0x1FFF, 0x1FFF, 0x1FFF, 0x1FFF, 0x1FFF,
	0x1FFF, 0x1FFF, 0x1FFF, 0x1FFF, 0x1FFF, 0x1FFF, 0x1FFF, 0x1FFF,
	0x1FFF, 0x1FFF, 0x1FFF, 0x1FFF, 0x1FFF, 0x1FFF, 0x1FFF, 0x1FFF,
	0x1FFF, 0x1FFF, 0x1FFF, 0x1FFF, 0x1FFF, 0x1FFF, 0x1FFF, 0x1FFF,
	0x1FFF, 0x1FFF, 0x1FFF, 0x1FFF, 0x1FFF, 0x1FFF, 0x1FFF, 0x1FFF,
	0x1FFF, 0x1FFF, 0x1FFF, 0x1FFF, 0x1FFF, 0x1FFF, 0x1FFF, 0x1FFF,
};

static byte dist_lo_code[512] =
{
	0x00, 0x01, 0x02, 0x03, 0x04, 0x04, 0x05, 0x05, 0x06, 0x06, 0x06,
	0x06, 0x07, 0x07, 0x07, 0x07, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,
	0x08, 0x08, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x0A,
	0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A,
	0x0A, 0x0A, 0x0A, 0x0A, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B,
	0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0C, 0x0C,
	0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C,
	0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C,
	0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0D, 0x0D, 0x0D,
	0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D,
	0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D,
	0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0E, 0x0E, 0x0E, 0x0E,
	0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E,
	0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E,
	0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E,
	0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E,
	0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E,
	0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F,
	0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F,
	0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F,
	0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F,
	0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F,
	0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F,
	0x0F, 0x0F, 0x0F, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10,
	0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10,
	0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10,
	0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10,
	0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10,
	0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10,
	0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10,
	0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10,
	0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10,
	0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10,
	0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10,
	0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x11,
	0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
	0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
	0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
	0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
	0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
	0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
	0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
	0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
	0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
	0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
	0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
	0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
};

static byte dist_lo_extra[512] =
{
	0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3,
	3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 4, 4, 4, 4,
	4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
	4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5, 5, 5, 5, 5,
	5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
	5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
	5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
	5, 5, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
	6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
	6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
	6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
	6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
	6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
	6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
	6, 6, 6, 6, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
	7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
	7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
	7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
	7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
	7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
	7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
	7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
	7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
	7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
	7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
	7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
	7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
	7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
	7, 7, 7, 7, 7, 7, 7, 7,
};

static byte dist_lo_mask[512] =
{
	0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x03, 0x03, 0x03,
	0x03, 0x03, 0x03, 0x03, 0x03, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
	0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x0F,
	0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F,
	0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F,
	0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x1F, 0x1F,
	0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F,
	0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F,
	0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F,
	0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F,
	0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F,
	0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x3F, 0x3F, 0x3F, 0x3F,
	0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F,
	0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F,
	0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F,
	0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F,
	0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F,
	0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F,
	0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F,
	0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F,
	0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F,
	0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F,
	0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F,
	0x3F, 0x3F, 0x3F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F,
	0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F,
	0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F,
	0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F,
	0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F,
	0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F,
	0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F,
	0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F,
	0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F,
	0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F,
	0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F,
	0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F,
	0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F,
	0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F,
	0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F,
	0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F,
	0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F,
	0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F,
	0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F,
	0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F,
	0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F,
	0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F,
	0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F,
	0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F,
};

static byte bit_length_order[19] =
{
	0x10, 0x11, 0x12, 0x00, 0x08, 0x07, 0x09, 0x06, 0x0A, 0x05, 0x0B,
	0x04, 0x0C, 0x03, 0x0D, 0x02, 0x0E, 0x01, 0x0F
};

static void int_set(int32 *dst, int32 dat, size_t len);
static void uint_set(uint32 *dst, uint32 dat, size_t len);
static void ushort_set(uint16 *dst, uint16 dat, size_t len);
static void int_move(int32 *dst, int32 *src, size_t len);
static int32 *repeat_last(int32 *dst, int32 size, int32 run_len);
static int32 *repeat_zero(int32 *dst, int32 run_len);

static void init_compress_code_sizes();
static bool32 compress_code_sizes();

static void huff_down_heap(int32 *heap, int32 *sym_freq, int32 heap_len, int32 i);
static void huff_code_sizes(int32 num_symbols, int32 *sym_freq, int32 *code_sizes);
static void huff_sort_code_sizes(int32 num_symbols, int32 *code_sizes);
static void huff_fix_code_sizes(int32 max_code_size);
static void huff_make_codes(int32 num_symbols, int32 *code_sizes, int32 max_code_size, uint32 *codes);

static bool32 send_static_block();
static bool32 send_dynamic_block();
static bool32 send_raw_block();

static void init_static_block();
static void init_dynamic_block();

static bool32 code_block();
static bool32 code_token_buf(bool32 last_block_flag);

static void delete_data(int32 dict_pos);
static void hash_data(int32 dict_pos, int32 bytes_to_do);
static void find_match(int32 dict_pos);

static bool32 empty_flag_buf();
static bool32 flush_flag_buf();
static bool32 flush_out_buffer();
static bool32 put_bits(int32 bits, int32 len);
static bool32 flush_bits();

static bool32 dict_search_lazy();
static bool32 dict_search_flash();
static bool32 dict_search_greedy();
static bool32 dict_search();
static bool32 dict_search_main(int32 dict_ofs);
static bool32 dict_search_eof();
static bool32 dict_fill();

static void deflate_main_init();
static int32 deflate_main();

static void int_set(int32 *dst, int32 dat, size_t len)
{
	while (len--) dst[len] = dat;
}

static void uint_set(uint32 *dst, uint32 dat, size_t len)
{
	while (len--) dst[len] = dat;
}

static void ushort_set(uint16 *dst, uint16 dat, size_t len)
{
	while (len--) dst[len] = dat;
}

static void int_move(int32 *dst, int32 *src, size_t len)
{
	while (len--) *dst++ = *src++;
}

static int32 *repeat_last(int32 *dst, int32 size, int32 run_len)
{
	if (run_len < 3)
	{
		wd->freq_3[size] += run_len;

		while (run_len--) *dst++ = size;

		return dst;
	}
	else
	{
		wd->freq_3[16]++;
		*dst = 16;
		dst[1] = run_len - 3;

		return dst + 2;
	}
}

static int32 *repeat_zero(int32 *dst, int32 run_len)
{
	if (run_len < 3)
	{
		wd->freq_3[0] += run_len;

		while (run_len--) *dst++ = 0;

		return dst;
	}
	else
	{
		int32 val;

		if (run_len > 10)
		{
			wd->freq_3[18]++;
			*dst++ = 18;
			val = run_len - 11;
		}
		else
		{
			wd->freq_3[17]++;
			*dst++ = 17;
			val = run_len - 3;
		}

		*dst++ = val;

		return dst;
	}
}

static void init_compress_code_sizes()
{
	int_set(wd->freq_3, 0x00, 19);

	wd->used_lit_codes = 285;

	do
	{
		if (wd->size_1[wd->used_lit_codes]) break;

		wd->used_lit_codes--;
	}
	while (wd->used_lit_codes >= 0);

	wd->used_lit_codes++;

	if (wd->used_lit_codes <= 257)
	{
		wd->used_lit_codes = 257;
	}

	wd->used_dist_codes = 29;

	do
	{
		if (wd->size_2[wd->used_dist_codes]) break;

		wd->used_dist_codes--;
	}
	while (wd->used_dist_codes >= 0);

	wd->used_dist_codes++;

	if (wd->used_dist_codes <= 1) wd->used_dist_codes = 1;

	int_move(wd->bundled_sizes, wd->size_1, wd->used_lit_codes);
	int_move(&wd->bundled_sizes[wd->used_lit_codes], wd->size_2, wd->used_dist_codes);

	int32 last_size = 255;
	int32 run_len_z = 0;
	int32 run_len = 0;
	int32 *bundled_sizes = wd->bundled_sizes;
	int32 *coded_sizes = wd->coded_sizes;

	for (int32 i = wd->used_dist_codes + wd->used_lit_codes; i > 0; i--)
	{
		int32 size = *bundled_sizes++;

		if (size)
		{
			if (run_len_z)
			{
				coded_sizes = repeat_zero(coded_sizes, run_len_z);

				run_len_z = 0;
			}

			if (last_size == size)
			{
				if (++run_len == 6)
				{
					coded_sizes = repeat_last(coded_sizes, last_size, run_len);

					run_len = 0;
				}
			}
			else
			{
				if (run_len)
				{
					coded_sizes = repeat_last(coded_sizes, last_size, run_len);

					run_len = 0;
				}

				*coded_sizes++ = size;
				wd->freq_3[size]++;
			}
		}
		else
		{
			if (run_len)
			{
				coded_sizes = repeat_last(coded_sizes, last_size, run_len);

				run_len = 0;
			}

			if (++run_len_z == 138)
			{
				coded_sizes = repeat_zero(coded_sizes, run_len_z);

				run_len_z = 0;
			}
		}

		last_size = size;
	}

	if (run_len)
	{
		coded_sizes = repeat_last(coded_sizes, last_size, run_len);
	}
	else if (run_len_z)
	{
		coded_sizes = repeat_zero(coded_sizes, run_len_z);
	}

	wd->coded_sizes_end = coded_sizes;

	huff_code_sizes(19, wd->freq_3, wd->size_3);
	huff_sort_code_sizes(19, wd->size_3);
	huff_fix_code_sizes(7);
	huff_make_codes(19, wd->size_3, 7, wd->code_3);
}

static bool32 compress_code_sizes()
{
	if (put_bits(wd->used_lit_codes - 257, 5)) return TRUE;

	if (put_bits(wd->used_dist_codes - 1, 5)) return TRUE;

	int32 i = 18;

	do
	{
		if (wd->size_3[bit_length_order[i]]) break;

		i--;
	}
	while (i >= 0);

	int32 blc = i + 1;

	if (blc <= 4) blc = 4;

	if (put_bits(blc - 4, 4)) return TRUE;

	if (blc <= 0)
	{
		int32 *code_sizes = wd->coded_sizes;

		while (wd->coded_sizes_end > code_sizes)
		{
			int32 code_size = *code_sizes++;

			if (put_bits(wd->code_3[code_size], wd->size_3[code_size])) return TRUE;

			switch (code_size)
			{
			case 16:
				code_size = *code_sizes++;

				if (put_bits(code_size, 2)) return TRUE;

				break;

			case 17:
				code_size = *code_sizes++;

				if (put_bits(code_size, 3)) return TRUE;

				break;
			case 18:

				code_size = *code_sizes++;

				if (put_bits(code_size, 7)) return TRUE;

				break;
			}
		}

		return FALSE;
	}
	else
	{
		int32 j = 0;

		while (!put_bits(wd->size_3[bit_length_order[j++]], 3))
		{
			if (j >= blc)
			{
				int32 *code_sizes = wd->coded_sizes;

				while (wd->coded_sizes_end > code_sizes)
				{
					int32 code_size = *code_sizes++;

					if (put_bits(wd->code_3[code_size], wd->size_3[code_size])) return TRUE;

					switch (code_size)
					{
					case 16:
						code_size = *code_sizes++;

						if (put_bits(code_size, 2)) return TRUE;

						break;

					case 17:
						code_size = *code_sizes++;

						if (put_bits(code_size, 3)) return TRUE;

						break;
					case 18:

						code_size = *code_sizes++;

						if (put_bits(code_size, 7)) return TRUE;

						break;
					}
				}

				return FALSE;
			}
		}

		return TRUE;
	}
}

static void huff_down_heap(int32 *heap, int32 *sym_freq, int32 heap_len, int32 i)
{
	int32 v = heap[i];
	int32 k = i << 1;

	while (k <= heap_len)
	{
		if (k < heap_len)
		{
			int32 n = heap[k + 1];
			int32 m = heap[k];

			if (sym_freq[n] < sym_freq[m] || sym_freq[n] == sym_freq[m] && n > m) k++;
		}

		int32 m = heap[k];

		if (sym_freq[m] > sym_freq[v] || sym_freq[m] == sym_freq[v] && v > m) break;

		heap[i] = m;
		i = k;
		k <<= 1;
	}

	heap[i] = v;
}

static void huff_code_sizes(int32 num_symbols, int32 *sym_freq, int32 *code_sizes)
{
	if (num_symbols > 0)
	{
		int_set(others, -1, num_symbols);
		int_set(code_sizes, 0, num_symbols);
	}

	int32 new_heap_len = 1;

	for (int32 i = 0; i < num_symbols; i++)
	{
		if (sym_freq[i]) heap[new_heap_len++] = i;
	}

	int32 heap_len = new_heap_len - 1;

	if (heap_len > 1)
	{
		for (int32 j = heap_len >> 1; j; j--)
		{
			huff_down_heap(heap, sym_freq, heap_len, j);
		}

		do
		{
			int32 heap_last = heap[heap_len--];
			int32 heap_first = heap[1];
			heap[1] = heap_last;

			huff_down_heap(heap, sym_freq, heap_len, 1);

			int32 heap_first_two = heap[1];
			sym_freq[heap_first_two] += sym_freq[heap_first];

			huff_down_heap(heap, sym_freq, heap_len, 1);

			int32 others_off;

			do
			{
				++code_sizes[heap_first_two];
				others_off = heap_first_two;
				heap_first_two = others[heap_first_two];

			}
			while (heap_first_two != -1);

			others[others_off] = heap_first;

			do
			{
				++code_sizes[heap_first];
				heap_first = others[heap_first];
			}
			while (heap_first != -1);
		}
		while (heap_len != 1);
	}
	else if (heap_len)
	{
		code_sizes[heap[1]] = 1;
	}
}

static void huff_sort_code_sizes(int32 num_symbols, int32 *code_sizes)
{
	code_list_len = 0;
	int_set(num_codes, 0, 33);

	int32 num_symbols_1 = num_symbols;
	int32 *code_sizes_1 = code_sizes;

	while (num_symbols_1 > 0)
	{
		num_codes[*code_sizes_1]++;
		code_sizes_1++;
		num_symbols_1--;
	}

	int32 j = 0;

	for (int32 k = 0; k <= 31; k++)
	{
		int32 code_num = num_codes[k + 1];
		next_code[k + 1] = j;
		j += code_num;
	}

	int32 *code_sizes_2 = code_sizes;

	for (int32 i = 0; i < num_symbols; i++)
	{
		int32 next_code_off = *code_sizes_2;

		if (next_code_off)
		{
			int32 code_next = next_code[next_code_off];
			code_list[code_next] = i;
			next_code[next_code_off] = code_next + 1;
			code_list_len++;
		}

		code_sizes_2++;
	}
}

static void huff_fix_code_sizes(int32 max_code_size)
{
	if (code_list_len > 1)
	{
		for (int32 i = max_code_size + 1; i <= 32; i++)
		{
			num_codes[max_code_size] += num_codes[i];
		}

		int32 used_codes = 0;

		for (int32 i = max_code_size; i > 0; i--)
		{
			used_codes += num_codes[i] << (max_code_size - i);
		}

		for (int32 k = used_codes - (1 << max_code_size); k; k--)
		{
			int32 l = max_code_size - 1;

			if (l > 0)
			{
				while (!num_codes[l])
				{
					if (--l <= 0) goto continue_2;
				}

				--num_codes[l];
				num_codes[l + 1] += 2;
			}

continue_2:;
		}
	}
}

static void huff_make_codes(int32 num_symbols, int32 *code_sizes, int32 max_code_size, uint32 *codes)
{
	if (code_list_len)
	{
		if (num_symbols > 0) uint_set(codes, 0x00, num_symbols);

		int32 i = 0;

		for (int32 new_code_size = 1; max_code_size >= new_code_size; new_code_size++)
		{
			int32 num_code = num_codes[new_code_size];

			if (num_code > 0)
			{
				int_set(&new_code_sizes[i], new_code_size, num_code);
				i += num_code;
			}
		}

		int32 num_codes_off = 0;
		int32 next_code_val = 0;

		next_code[1] = 0;

		if (max_code_size >= 2)
		{
			for (int32 j = max_code_size - 1; j; j--)
			{
				next_code_val = 2 * (next_code_val + num_codes[num_codes_off + 1]);
				next_code[num_codes_off + 2] = next_code_val;
				num_codes_off++;
			}
		}

		for (int32 code_off = 0; code_off < code_list_len; code_off++)
		{
			code_sizes[code_list[code_off]] = new_code_sizes[code_off];
		}

		while (num_symbols > 0)
		{
			if (*code_sizes)
			{
				int32 prev_code = next_code[*code_sizes]++;

				int32 new_code = 0;

				for (int32 code_size = *code_sizes; code_size > 0; code_size--)
				{
					int32 save_prev_code = prev_code;
					prev_code >>= 1;
					new_code = save_prev_code & 1 | 2 * new_code;
				}

				*codes = new_code;
			}

			codes++;
			code_sizes++;
			num_symbols--;
		}
	}
}

static bool32 send_static_block()
{
	return put_bits(1, 2) != 0;
}

static bool32 send_dynamic_block()
{
	if (put_bits(2, 2)) return TRUE;

	return compress_code_sizes() >= 1;
}

static bool32 send_raw_block()
{
	if (put_bits(0, 2)) return TRUE;

	if (flush_bits()) return TRUE;

	while (--out_buf_left < 0)
	{
		out_buf_left++;

		if (flush_out_buffer()) return TRUE;
	}

	*out_buf_cur_ofs++ = (byte)wd->token_buf_bytes;

	while (--out_buf_left < 0)
	{
		out_buf_left++;

		if (flush_out_buffer()) return TRUE;
	}

	*out_buf_cur_ofs++ = *((byte *)&(wd->token_buf_bytes) + 1);

	while (--out_buf_left < 0)
	{
		out_buf_left++;

		if (flush_out_buffer()) return TRUE;
	}

	*out_buf_cur_ofs++ = ~(byte)wd->token_buf_bytes;

	while (--out_buf_left < 0)
	{
		out_buf_left++;

		if (flush_out_buffer()) return TRUE;
	}

	*out_buf_cur_ofs++ = ~*((byte *)&(wd->token_buf_bytes) + 1);

	int32 token_buf_bytes = wd->token_buf_bytes;
	uint32 token_buf_start = wd->token_buf_start;

	while (token_buf_bytes--)
	{
		while (--out_buf_left < 0)
		{
			out_buf_left++;

			if (flush_out_buffer()) return TRUE;
		}

		*out_buf_cur_ofs++ = dict[token_buf_start];
		token_buf_start = ((uint16)token_buf_start + 1) & INT16_MAX;
	}

	return FALSE;
}

static void init_dynamic_block()
{
	int_set(wd->freq_1, 0, 288);
	int_set(wd->freq_2, 0, 32);

	int32 flag = 0;
	byte *token_buf = wd->token_buf;
	int32 token_buf_len = wd->token_buf_len;
	int32 i = 0;
	uint32 *flag_buf = wd->flag_buf;

	while (token_buf_len)
	{
		if (!i)
		{
			flag = *flag_buf++;
			i = 32;
		}

		if (flag < 0)
		{
			wd->freq_1[len_code[*token_buf]]++;

			uint32 code_off = *(uint16 *)(token_buf + 1) - 1;
			uint32 code;

			if (code_off < 512)
			{
				code = dist_lo_code[code_off];
			}
			else
			{
				code = dist_hi_code[code_off >> 8];
			}

			token_buf += 3;

			wd->freq_2[code]++;
		}
		else
		{
			wd->freq_1[*token_buf++]++;;
		}

		flag *= 2;
		i--;
		token_buf_len--;
	}

	wd->freq_1[256]++;

	huff_code_sizes(288, wd->freq_1, wd->size_1);
	huff_sort_code_sizes(288, wd->size_1);
	huff_fix_code_sizes(15);
	huff_make_codes(288, wd->size_1, 15, wd->code_1);

	huff_code_sizes(32, wd->freq_2, wd->size_2);
	huff_sort_code_sizes(32, wd->size_2);
	huff_fix_code_sizes(15);
	huff_make_codes(32, wd->size_2, 15, wd->code_2);
	init_compress_code_sizes();
}

static void init_static_block()
{
	int_set(&wd->size_1[0], 8, 144);
	int_set(&wd->size_1[144], 9, 112);
	int_set(&wd->size_1[256], 7, 24);
	int_set(&wd->size_1[280], 8, 8);

	huff_sort_code_sizes(288, wd->size_1);
	huff_make_codes(288, wd->size_1, 15, wd->code_1);

	int_set(wd->size_2, 5, 32);

	huff_sort_code_sizes(32, wd->size_2);
	huff_make_codes(32, wd->size_2, 15, wd->code_2);
}

static bool32 code_block()
{
	if (wd->token_buf_len <= 0) return put_bits(wd->code_1[256], wd->size_1[256]) >= 1;

	uint32 i = 0;
	uint32 *flag_buf = wd->flag_buf;
	byte *token_buf = wd->token_buf;
	int32 flag_buf_content = 0;
	int32 token_buf_len = wd->token_buf_len;

	do
	{
		if (!i)
		{
			i = 32;
			flag_buf_content = *flag_buf++;
		}

		if (flag_buf_content < 0)
		{
			uint32 token_buf_content1 = *token_buf;
			uint32 token_buf_content2 = *(int16 *)(token_buf + 1) - 1;

			if (put_bits(wd->code_1[len_code[token_buf_content1]], wd->size_1[len_code[token_buf_content1]])) return TRUE;

			if (put_bits((byte)(token_buf_content1 & len_mask[token_buf_content1]), len_extra[token_buf_content1])) return TRUE;

			if (token_buf_content2 < 512)
			{
				if (put_bits(wd->code_2[dist_lo_code[token_buf_content2]], wd->size_2[dist_lo_code[token_buf_content2]])
				|| put_bits(token_buf_content2 & dist_lo_mask[token_buf_content2], dist_lo_extra[token_buf_content2]))
				{
					return TRUE;
				}
			}
			else
			{
				if (put_bits(wd->code_2[dist_hi_code[token_buf_content2 >> 8]], wd->size_2[dist_hi_code[token_buf_content2 >> 8]])
				|| put_bits(token_buf_content2 & dist_hi_mask[token_buf_content2 >> 8], dist_hi_extra[token_buf_content2 >> 8]))
				{
					return TRUE;
				}
			}

			token_buf += 3;
		}
		else
		{
			byte token_buf_content = *token_buf++;

			if (put_bits(wd->code_1[token_buf_content], wd->size_1[token_buf_content])) return TRUE;
		}

		flag_buf_content *= 2;
		token_buf_len--;
		i--;
	}
	while(token_buf_len > 0);

	return put_bits(wd->code_1[256], wd->size_1[256]) != 0;
}

static bool32 code_token_buf(bool32 last_block_flag)
{
	wd->token_buf_end = wd->search_offset;

	if (wd->token_buf_len)
	{
		if (put_bits(0, 1)) return TRUE;

		if (wd->strategy == DEFLATE_STRATEGY_STATIC)
		{
			init_static_block();

			if (send_static_block()) return TRUE;
			if (code_block()) return TRUE;
		}
		else if (wd->strategy == DEFLATE_STRATEGY_DYNAMIC)
		{
			init_dynamic_block();

			if (send_dynamic_block()) return TRUE;
			if (code_block()) return TRUE;
		}
		else if (wd->token_buf_len < 128)
		{
			bit_buf_total_flag = TRUE;
			wd->bit_buf_total = 0;

			init_static_block();

			if (send_static_block()) return TRUE;
			if (code_block()) return TRUE;

			uint32 save_bit_buf_total1 = wd->bit_buf_total;
			wd->bit_buf_total = 0;

			init_dynamic_block();

			if (send_dynamic_block()) return TRUE;
			if (code_block()) return TRUE;

			uint32 save_bit_buf_total2 = wd->bit_buf_total;
			bit_buf_total_flag = FALSE;

			uint32 block_size = 8 * wd->token_buf_bytes + 34;

			if (((byte)bit_buf_len + 2) & 7)
			{
				block_size = block_size - (((byte)bit_buf_len + 2) & 7) + 8;
			}

			if (block_size >= save_bit_buf_total1 || block_size >= save_bit_buf_total2)
			{
				if (save_bit_buf_total2 > save_bit_buf_total1)
				{
					init_static_block();

					if (send_static_block()) return TRUE;
					if (code_block()) return TRUE;
				}
				else
				{
					if (send_dynamic_block()) return TRUE;
					if (code_block()) return TRUE;
				}
			}
			else if (send_raw_block())
			{
				return TRUE;
			}
		}
		else
		{
			if (wd->token_buf_bytes < 14745)
			{
				bit_buf_total_flag = TRUE;
				wd->bit_buf_total = 0;

				init_dynamic_block();

				if (send_dynamic_block()) return TRUE;
				if (code_block()) return TRUE;

				uint32 save_bit_buf_total = wd->bit_buf_total;
				bit_buf_total_flag = FALSE;

				uint32 block_size = 8 * wd->token_buf_bytes + 34;

				if (((byte)bit_buf_len + 2) & 7)
				{
					block_size = block_size - (((byte)bit_buf_len + 2) & 7) + 8;
				}

				if (block_size >= save_bit_buf_total)
				{
					if (send_dynamic_block()) return TRUE;
					if (code_block()) return TRUE;
				}
				else if (send_raw_block())
				{
					return TRUE;
				}
			}
			else
			{
				init_dynamic_block();

				if (send_dynamic_block()) return TRUE;
				if (code_block()) return TRUE;
			}
		}
	}

	wd->flag_buf_ofs = wd->flag_buf;
	wd->flag_buf_left = 32;
	wd->token_buf_ofs = wd->token_buf;
	wd->token_buf_len = 0;
	wd->token_buf_bytes = 0;
	wd->token_buf_start = wd->token_buf_end;

	if (!last_block_flag) return FALSE;

	if (put_bits(1, 1)) return TRUE;

	init_static_block();

	if (send_static_block()) return TRUE;
	if (code_block()) return TRUE;

	return FALSE;
}

static void delete_data(int32 dict_pos)
{
	int32 save_dict_pos = dict_pos;

	if (dict_pos < dict_pos + 0x1000)
	{
		do
		{
			uint16 hash_off = last[dict_pos];

			if (hash_off & 0x8000)
			{
				if (hash_off != UINT16_MAX) hash[hash_off & INT16_MAX] = UINT16_MAX;
			}
			else
			{
				next[hash_off] = UINT16_MAX;
			}

			dict_pos++;
		}
		while (dict_pos < save_dict_pos + 0x1000);
	}
}

static void hash_data(int32 dict_pos, int32 bytes_to_do)
{
	int32 range = (bytes_to_do - 2) < 0 ? 0 : bytes_to_do - 2;

	if (range < bytes_to_do)
	{
		ushort_set(&last[range + dict_pos], UINT16_MAX, bytes_to_do - range);
		ushort_set(&next[range + dict_pos], UINT16_MAX, bytes_to_do - range);
	}

	if (bytes_to_do > 2)
	{
		int32 dict_range = dict_pos + bytes_to_do - 2;
		int32 hash_off = 32 * dict[dict_pos] ^ dict[dict_pos + 1];

		if (dict_pos < dict_range)
		{
			do
			{
				hash_off = dict[dict_pos + 2] ^ 32 * (byte)hash_off;

				last[dict_pos] = hash_off | 0x8000;
				next[dict_pos] = hash[hash_off];

				if (next[dict_pos] != UINT16_MAX) last[next[dict_pos]] = dict_pos;

				hash[hash_off] = dict_pos++;
			}
			while (dict_range > dict_pos);
		}
	}
}

static void find_match(int32 dict_pos)
{
	uint16 next_dict_pos = dict_pos & INT16_MAX;
	int32 max_match = max_compares;

	uint16 l = *(uint16 *)&dict[match_len - 1 + dict_pos];
	uint16 m = *(uint16 *)&dict[dict_pos];
	byte *s = &dict[match_len - 1];
	byte *dict_at_pos = &dict[dict_pos];

	while (max_match > 0)
	{
		next_dict_pos = next[next_dict_pos];
		max_match--;

		if (next_dict_pos == UINT16_MAX) break;

		if (*(uint16 *)&s[next_dict_pos] == l) goto find_match_len;

		next_dict_pos = next[next_dict_pos];
		max_match--;

		if (next_dict_pos == UINT16_MAX) return;

		if (*(uint16 *)&s[next_dict_pos] == l) goto find_match_len;

		next_dict_pos = next[next_dict_pos];
		max_match--;

		if (next_dict_pos == UINT16_MAX) return;

		if (*(uint16 *)&s[next_dict_pos] == l) goto find_match_len;

		next_dict_pos = next[next_dict_pos];
		max_match--;

		if (next_dict_pos == UINT16_MAX) return;

		if (*(uint16 *)&s[next_dict_pos] == l)
		{
find_match_len:;
			byte *dict_at_next = &dict[next_dict_pos];

			if (*(uint16 *)&dict[next_dict_pos] == m)
			{
				uint16 *dict_at_pos_short = (uint16 *)dict_at_pos;
				uint16 dict_at_pos_short_plus;

				int32 i = 32;

				do
				{
					dict_at_pos_short_plus = dict_at_pos_short[1];
					dict_at_pos_short++;
					dict_at_next += 2;

					if (dict_at_pos_short_plus != *(uint16 *)dict_at_next) break;

					dict_at_pos_short_plus = dict_at_pos_short[1];
					dict_at_pos_short++;
					dict_at_next += 2;

					if (dict_at_pos_short_plus != *(uint16 *)dict_at_next) break;

					dict_at_pos_short_plus = dict_at_pos_short[1];
					dict_at_pos_short++;
					dict_at_next += 2;

					if (dict_at_pos_short_plus != *(uint16 *)dict_at_next) break;

					dict_at_pos_short_plus = dict_at_pos_short[1];
					dict_at_pos_short++;
					dict_at_next += 2;

					if (dict_at_pos_short_plus != *(uint16 *)dict_at_next) break;

					i--;
				}
				while (i > 0);

				if (!i)
				{
					match_pos = next_dict_pos;
					match_len = 258;

					return;
				}

				int32 new_match_len = (*(byte *)dict_at_pos_short == *dict_at_next) + 2 * (((byte *)dict_at_pos_short - (byte *)dict_at_pos) >> 1);

				if (new_match_len > match_len)
				{
					l = *(uint16 *)&dict_at_pos[new_match_len - 1];
					match_len = new_match_len;
					s = &dict[new_match_len - 1];
					match_pos = next_dict_pos;
				}
			}
		}
	}
}

static bool32 empty_flag_buf()
{
	wd->flag_buf_ofs++;
	wd->flag_buf_left = 32;
	wd->token_buf_len += 32;

	if (wd->token_buf_len == 12288)
	{
		return code_token_buf(FALSE);
	}

	return FALSE;
}

static bool32 flush_flag_buf()
{
	if (wd->flag_buf_left != 32)
	{
		wd->token_buf_len += 32 - wd->flag_buf_left;

		while (wd->flag_buf_left)
		{
			*wd->flag_buf_ofs *= 2;
			wd->flag_buf_left--;
		}

		wd->flag_buf_ofs++;
		wd->flag_buf_left = 32;
	}

	return code_token_buf(TRUE);
}

static bool32 flush_out_buffer()
{
	if (wd->flush_out_buf(wd->out_buf_ofs, wd->out_buf_size - out_buf_left)) return TRUE;

	out_buf_cur_ofs = wd->out_buf_ofs;
	out_buf_left = wd->out_buf_size;

	return FALSE;
}

static bool32 put_bits(int32 bits, int32 len)
{
	if (bit_buf_total_flag)
	{
		wd->bit_buf_total += len;

		return FALSE;
	}

	bit_buf |= bits << bit_buf_len;
	bit_buf_len += len;

	if (bit_buf_len < 8)
	{
		return FALSE;
	}

	if (bit_buf_len < 16)
	{
		if (--out_buf_left < 0)
		{
			out_buf_left++;

			while (--out_buf_left < 0)
			{
				out_buf_left++;

				if (flush_out_buffer())	return TRUE;
			}

			*out_buf_cur_ofs++ = bit_buf;
			bit_buf >>= 8;
			bit_buf_len -= 8;
		}
		else
		{
			*out_buf_cur_ofs++ = bit_buf;
			bit_buf >>= 8;
			bit_buf_len -= 8;
		}
	}
	else
	{
		out_buf_left -= 2;

		if (out_buf_left < 0)
		{
			out_buf_left += 2;

			while (--out_buf_left < 0)
			{
				out_buf_left++;

				if (flush_out_buffer())	return TRUE;
			}

			*out_buf_cur_ofs++ = bit_buf;

			while (--out_buf_left < 0)
			{
				out_buf_left++;

				if (flush_out_buffer())	return TRUE;
			}

			*out_buf_cur_ofs++ = *((byte *)&(bit_buf) + 1);
			bit_buf >>= 16;
			bit_buf_len -= 16;
		}
		else
		{
			*(uint16 *)out_buf_cur_ofs = bit_buf;
			out_buf_cur_ofs += 2;
			bit_buf >>= 16;
			bit_buf_len -= 16;
		}
	}

	return FALSE;
}

static bool32 flush_bits()
{
	if (put_bits(0, 7)) return TRUE;

	bit_buf_len = 0;

	return FALSE;
}

static bool32 dict_search_lazy()
{
	while (wd->search_bytes_left && wd->search_offset < wd->search_threshold)
	{
		if (next[wd->search_offset & INT16_MAX] == UINT16_MAX)
		{
			*wd->token_buf_ofs++ = dict[wd->search_offset++];
			wd->search_bytes_left--;
			wd->token_buf_bytes++;
			*wd->flag_buf_ofs *= 2;
			wd->flag_buf_left--;

			if (!wd->flag_buf_left && empty_flag_buf()) return TRUE;

			continue;
		}

		match_len = 2;

		find_match(wd->search_offset);

		if (match_len == 2)
		{
			*wd->token_buf_ofs++ = dict[wd->search_offset++];
			wd->search_bytes_left--;
			wd->token_buf_bytes++;
			*wd->flag_buf_ofs *= 2;
			wd->flag_buf_left--;

			if (!wd->flag_buf_left && empty_flag_buf()) return TRUE;

			continue;
		}

		int32 save_match_len = match_len;

		while (save_match_len < 128 && next[((uint16)wd->search_offset + 1) & INT16_MAX] != UINT16_MAX)
		{
			find_match(wd->search_offset + 1);

			if (match_len > wd->search_bytes_left - 1) match_len = wd->search_bytes_left - 1;

			if (save_match_len >= match_len) break; // is actually never greater

			save_match_len = match_len;

			*wd->token_buf_ofs++ = dict[wd->search_offset++];
			wd->search_bytes_left--;
			wd->token_buf_bytes++;
			*wd->flag_buf_ofs *= 2;
			wd->flag_buf_left--;

			if (!wd->flag_buf_left && empty_flag_buf()) return TRUE;
		}

		if (match_len > wd->search_bytes_left)
		{
			match_len = wd->search_bytes_left;

			if (wd->search_bytes_left <= 2)
			{
				*wd->token_buf_ofs++ = dict[wd->search_offset++];
				wd->search_bytes_left--;
				wd->token_buf_bytes++;
				*wd->flag_buf_ofs *= 2;
				wd->flag_buf_left--;

				if (!wd->flag_buf_left && empty_flag_buf()) return TRUE;

				continue;
			}
		}

		uint32 offset = ((uint16)wd->search_offset - (uint16)match_pos) & INT16_MAX;

		if (match_len == 3 && offset >= 0x4000)
		{
			*wd->token_buf_ofs++ = dict[wd->search_offset++];
			wd->search_bytes_left--;
			wd->token_buf_bytes++;
			*wd->flag_buf_ofs *= 2;
			wd->flag_buf_left--;

			if (!wd->flag_buf_left && empty_flag_buf()) return TRUE;

			continue;
		}

		*wd->token_buf_ofs++ = (byte)(match_len - 3);
		*(uint16 *)wd->token_buf_ofs = (uint16)offset;
		wd->token_buf_ofs += 2;
		wd->search_offset += match_len;
		wd->search_bytes_left -= match_len;
		wd->token_buf_bytes += match_len;
		*wd->flag_buf_ofs = (2 * *wd->flag_buf_ofs) | 1;
		wd->flag_buf_left--;

		if (!wd->flag_buf_left && empty_flag_buf()) return TRUE;
	}

	wd->search_offset &= INT16_MAX;

	return FALSE;
}

static bool32 dict_search_flash()
{
	while (wd->search_bytes_left && wd->search_offset < wd->search_threshold)
	{
		match_pos = next[wd->search_offset & INT16_MAX];

		if (match_pos == UINT16_MAX)
		{
			*wd->token_buf_ofs++ = dict[wd->search_offset++];
			wd->search_bytes_left--;
			wd->token_buf_bytes++;
			*wd->flag_buf_ofs *= 2;
			wd->flag_buf_left--;

			if (!wd->flag_buf_left && empty_flag_buf()) return TRUE;

			continue;
		}

		uint16 *dict_match_pos = (uint16 *)&dict[match_pos];
		uint16 *dict_search_offset = (uint16 *)&dict[wd->search_offset];

		if (*dict_match_pos == *dict_search_offset)
		{
			match_len = 32;

			do
			{
				dict_match_pos++;
				dict_search_offset++;

				if (*dict_match_pos != *dict_search_offset) break;

				dict_match_pos++;
				dict_search_offset++;

				if (*dict_match_pos != *dict_search_offset) break;

				dict_match_pos++;
				dict_search_offset++;

				if (*dict_match_pos != *dict_search_offset) break;

				dict_match_pos++;
				dict_search_offset++;

				if (*dict_match_pos != *dict_search_offset) break;

				match_len--;
			}
			while (match_len > 0);

			if (match_len)
			{
				match_len = ((byte)(*(byte *)dict_search_offset - *(byte *)dict_match_pos) < 1) + (((byte *)dict_match_pos - match_pos - dict) & 0xFFFFFFFE);
			}
			else
			{
				match_len = 258;
			}

			if (match_len > wd->search_bytes_left)
			{
				match_len = wd->search_bytes_left;

				if (wd->search_bytes_left <= 2)
				{
					*wd->token_buf_ofs++ = dict[wd->search_offset++];
					wd->search_bytes_left--;
					wd->token_buf_bytes++;
					*wd->flag_buf_ofs *= 2;
					wd->flag_buf_left--;

					if (!wd->flag_buf_left && empty_flag_buf()) return TRUE;

					continue;
				}
			}

			uint32 offset = ((uint16)wd->search_offset - (uint16)match_pos) & INT16_MAX;

			if (match_len == 3 && offset >= 0x4000)
			{
				*wd->token_buf_ofs++ = dict[wd->search_offset++];
				wd->search_bytes_left--;
				wd->token_buf_bytes++;
				*wd->flag_buf_ofs *= 2;
				wd->flag_buf_left--;

				if (!wd->flag_buf_left && empty_flag_buf()) return TRUE;

				continue;
			}

			*wd->token_buf_ofs++ = (byte)(match_len - 3);
			*(uint16 *)wd->token_buf_ofs = (uint16)offset;
			wd->token_buf_ofs += 2;
			wd->search_offset += match_len;
			wd->search_bytes_left -= match_len;
			wd->token_buf_bytes += match_len;
			*wd->flag_buf_ofs = (2 * *wd->flag_buf_ofs) | 1;
			wd->flag_buf_left--;

			if (!wd->flag_buf_left && empty_flag_buf()) return TRUE;

			continue;
		}

		*wd->token_buf_ofs++ = dict[wd->search_offset++];
		wd->search_bytes_left--;
		wd->token_buf_bytes++;
		*wd->flag_buf_ofs *= 2;
		wd->flag_buf_left--;

		if (!wd->flag_buf_left && empty_flag_buf()) return TRUE;
	}

	wd->search_offset &= INT16_MAX;

	return FALSE;
}

static bool32 dict_search_greedy()
{
	while (wd->search_bytes_left && wd->search_offset < wd->search_threshold)
	{
		if (next[wd->search_offset & INT16_MAX] == UINT16_MAX)
		{
			*wd->token_buf_ofs++ = dict[wd->search_offset++];
			wd->search_bytes_left--;
			wd->token_buf_bytes++;
			*wd->flag_buf_ofs *= 2;
			wd->flag_buf_left--;

			if (!wd->flag_buf_left && empty_flag_buf()) return TRUE;

			continue;
		}

		match_len = 2;

		find_match(wd->search_offset);

		if (match_len == 2)
		{
			*wd->token_buf_ofs++ = dict[wd->search_offset++];
			wd->search_bytes_left--;
			wd->token_buf_bytes++;
			*wd->flag_buf_ofs *= 2;
			wd->flag_buf_left--;

			if (!wd->flag_buf_left && empty_flag_buf()) return TRUE;

			continue;
		}

		if (match_len > wd->search_bytes_left)
		{
			match_len = wd->search_bytes_left;

			if (wd->search_bytes_left <= 2)
			{
				*wd->token_buf_ofs++ = dict[wd->search_offset++];
				wd->search_bytes_left--;
				wd->token_buf_bytes++;
				*wd->flag_buf_ofs *= 2;
				wd->flag_buf_left--;

				if (!wd->flag_buf_left && empty_flag_buf()) return TRUE;

				continue;
			}
		}

		uint32 offset = ((uint16)wd->search_offset - (uint16)match_pos) & INT16_MAX;

		if (match_len == 3 && offset >= 0x4000)
		{
			*wd->token_buf_ofs++ = dict[wd->search_offset++];
			wd->search_bytes_left--;
			wd->token_buf_bytes++;
			*wd->flag_buf_ofs *= 2;
			wd->flag_buf_left--;

			if (!wd->flag_buf_left && empty_flag_buf()) return TRUE;

			continue;
		}

		*wd->token_buf_ofs++ = (byte)(match_len - 3);
		*(uint16 *)wd->token_buf_ofs = (uint16)offset;
		wd->token_buf_ofs += 2;
		wd->search_offset += match_len;
		wd->search_bytes_left -= match_len;
		wd->token_buf_bytes += match_len;
		*wd->flag_buf_ofs = (2 * *wd->flag_buf_ofs) | 1;
		wd->flag_buf_left--;

		if (!wd->flag_buf_left && empty_flag_buf()) return TRUE;
	}

	wd->search_offset &= INT16_MAX;

	return FALSE;
}

static bool32 dict_search()
{
	if (wd->greedy_flag)
	{
		if (max_compares < DEFLATE_GREEDY_COMPARE_THRESHOLD)
		{
			return dict_search_flash();
		}
		else
		{
			return dict_search_greedy();
		}
	}

	return dict_search_lazy();
}

static bool32 dict_search_main(int32 dict_ofs)
{
	int32 add_search_bytes = (dict_ofs - wd->search_offset) & INT16_MAX;

	wd->search_bytes_left += add_search_bytes;
	wd->search_threshold = add_search_bytes + wd->search_offset + 0xEFD;

	return dict_search();
}

static bool32 dict_search_eof()
{
	wd->search_threshold = UINT16_MAX;

	return dict_search();
}

static bool32 dict_fill()
{
	int32 read_left = wd->main_read_left;

	if (wd->in_buf_left < read_left)
	{
		read_left = wd->in_buf_left;
	}

	memcpy(dict + wd->main_read_pos, wd->in_buf_cur_ofs, read_left);

	wd->in_buf_cur_ofs += read_left;
	wd->in_buf_left -= read_left;
	wd->main_read_pos = read_left + wd->main_read_pos & INT16_MAX;
	wd->main_read_left -= read_left;
	wd->search_bytes_left += read_left;

	if (wd->main_read_left)
	{
		if (wd->eof_flag) memzero(dict + wd->main_read_pos, wd->main_read_left);

		return TRUE;
	}
	else
	{
		wd->main_read_left = 0x1000;

		return FALSE;
	}
}

static void deflate_main_init()
{
	ushort_set(wd->next, UINT16_MAX, 32768);
	ushort_set(wd->hash, UINT16_MAX, 8192);

	wd->flag_buf_ofs = wd->flag_buf;
	wd->flag_buf_left = 32;
	wd->token_buf_ofs = wd->token_buf;
}

static int32 deflate_main()
{
	for (ever)
	{
		if (dict_fill() && !wd->eof_flag) return DEFLATE_OK;

		if (wd->main_del_flag) delete_data(wd->main_dict_pos);

		hash_data(wd->main_dict_pos, wd->search_bytes_left);

		if (!wd->main_dict_pos) memcpy(wd->dict + 0x8000, wd->dict, 0x1102);

		if (dict_search_main(wd->main_dict_pos)) break;

		wd->main_dict_pos += 0x1000;

		if (wd->main_dict_pos == 0x8000)
		{
			wd->main_dict_pos = 0;
			wd->main_del_flag = TRUE;
		}

		if (wd->eof_flag && !wd->in_buf_left)
		{
			if (!dict_search_eof() && !flush_flag_buf() && !flush_bits() && !flush_out_buffer())
			{
				wd->sig = DEFLATE_SIG_DONE;
			}

			return DEFLATE_OK;
		}

		wd->search_bytes_left = 0;
	}

	return DEFLATE_OK;
}

size_t deflate_buf_size()
{
	return sizeof(work_data);
}

int32 deflate_init(void *_wd, int32 max_compares, int32 strategy, bool32 greedy_flag, byte *out_buf_ofs, int32 out_buf_size, int32 (*out_buf_flush)(byte *, int32))
{
	wd = (work_data *)_wd;

	if (max_compares < DEFLATE_MIN_COMPARE)
	{
		max_compares = DEFLATE_MIN_COMPARE;
	}
	else if (max_compares > DEFLATE_MAX_COMPARE)
	{
		max_compares = DEFLATE_MAX_COMPARE;
	}

	wd->max_compares = max_compares;
	wd->strategy = strategy;
	wd->greedy_flag = greedy_flag;
	wd->out_buf_ofs = out_buf_ofs;
	wd->out_buf_size = out_buf_size;
	wd->saved_out_buf_cur_ofs = wd->out_buf_ofs;
	wd->saved_out_buf_left = wd->out_buf_size;
	wd->flush_out_buf = out_buf_flush;
	wd->main_read_left = 0x1000;

	deflate_main_init();

	wd->sig = DEFLATE_SIG_INIT;

	return DEFLATE_INIT;
}

int32 deflate_data(void *_wd, byte *in_buf_ofs, int32 in_buf_size, bool32 eof_flag)
{
	wd = (work_data *)_wd;

	if (!wd || wd->sig != DEFLATE_SIG_INIT) return DEFLATE_ERROR;

	wd->in_buf_ofs = in_buf_ofs;
	wd->in_buf_size = in_buf_size;
	wd->in_buf_cur_ofs = wd->in_buf_ofs;
	wd->in_buf_left = wd->in_buf_size;
	wd->eof_flag = eof_flag;

	dict = wd->dict;
	hash = wd->hash;
	next = wd->next;
	last = wd->last;
	max_compares = wd->max_compares;

	match_len = wd->saved_match_len;
	match_pos = wd->saved_match_pos;
	bit_buf = wd->saved_bit_buf;
	bit_buf_len = wd->saved_bit_buf_len;

	out_buf_cur_ofs = wd->saved_out_buf_cur_ofs;

	out_buf_left = wd->saved_out_buf_left;

	int32 code = deflate_main();

	wd->saved_match_len = match_len;
	wd->saved_match_pos = match_pos;
	wd->saved_bit_buf = bit_buf;
	wd->saved_bit_buf_len = bit_buf_len;
	wd->saved_bit_buf_total_flag = bit_buf_total_flag;

	wd->saved_out_buf_cur_ofs = out_buf_cur_ofs;
	wd->saved_out_buf_left = out_buf_left;

	return code;
}

void deflate_deinit(void *_wd)
{
	wd = (work_data *)_wd;

	wd->sig = DEFLATE_SIG_DONE;
}
#endif
