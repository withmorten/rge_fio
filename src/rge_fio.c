#ifdef _WIN32
#include <io.h>
#else
#include <sys/io.h>
#endif
#include <fcntl.h>
#include <sys/stat.h>

#include "rge_fio.h"
#include "compress.h"

#define MODE_INVALID -1
#define MODE_READ 0
#define MODE_WRITE 1

bool32 rge_write_error = FALSE;

#define FLAG_INVALID -1
#define FLAG_INFLATE 0
#define FLAG_DEFLATE 1
#define FLAG_FIRST_INFLATE 2
#define FLAG_FIRST_DEFLATE 3

static byte flags = FLAG_INVALID; // current state of inflate/deflate
static size_t file_size = 0; // complete compressed file size
static byte *file_buffers = NULL; // complete compressed file
static size_t compression_point = 0; // offset in compressed file
static size_t point = 0; // offset in decompressed buffer
static byte *compression_buffers = NULL; // work data of inflate or deflate algo
static byte *current = NULL; // pointer to current position in decompress buffer
static byte buffers[0x10000] = ZEROMEM; // decompression/compression buffer
static handle current_handle = INVALID_HANDLE; // handle to current file
static char *current_filename = NULL;

handle rge_fake_open_read(handle file_handle, int32 fake_size)
{
	if (file_handle != INVALID_HANDLE)
	{
		flags = FLAG_FIRST_INFLATE;
		point = 0;
		memzero(buffers, sizeof(buffers));
		current = buffers;
		file_buffers = NULL;
		compression_buffers = NULL;
		current_handle = file_handle;
		file_size = fake_size;
		current_filename = EMPTYSTR;
	}
	else
	{
		printf("invalid handle passed to rge_fake_open_read\n");
	}

	return file_handle;
}

handle rge_fake_close(handle handle)
{
	if (handle != INVALID_HANDLE && handle == current_handle)
	{
		rge_free(compression_buffers);
		rge_free(file_buffers);

		current_handle = INVALID_HANDLE;
		flags = FLAG_INVALID;
		current_filename = NULL;
	}

	return handle;
}

handle rge_open_read(char *filename, int32 flag)
{
	handle handle = _open(filename, flag);

	if (handle != INVALID_HANDLE)
	{
		flags = FLAG_FIRST_INFLATE;
		point = 0;
		memzero(buffers, sizeof(buffers));
		current = buffers;
		file_buffers = NULL;
		compression_buffers = NULL;
		current_handle = handle;
		_lseek(handle, 0, SEEK_END);
		file_size = _tell(handle);
		_lseek(handle, 0, SEEK_SET);
		current_filename = filename;
	}
	else
	{
		printf("couldn't open %s\n", filename);
	}

	return handle;
}

handle rge_open_write(char *filename, int32 flag, int32 pmode)
{
	handle handle = _open(filename, flag, pmode);

	if (handle != INVALID_HANDLE)
	{
		flags = FLAG_FIRST_DEFLATE;
		point = 0;
		memzero(buffers, sizeof(buffers));
		current = buffers;
		file_buffers = NULL;
		compression_buffers = NULL;
		current_handle = handle;
		_lseek(handle, 0, SEEK_END);
		file_size = _tell(handle);
		_lseek(handle, 0, SEEK_SET);
		current_filename = filename;
	}
	else
	{
		printf("couldn't open %s\n", filename);
	}

	return handle;
}

int32 rge_close(handle handle)
{
	if (handle != INVALID_HANDLE && handle == current_handle)
	{
		if (flags == FLAG_DEFLATE)
		{
			if (deflate_data(compression_buffers, NULL, 0, TRUE) == DEFLATE_ERROR) rge_write_error = TRUE;

			deflate_deinit(compression_buffers);
		}

		current_handle = INVALID_HANDLE;
		flags = FLAG_INVALID;
		current_filename = NULL;

		rge_free(compression_buffers);
		rge_free(file_buffers);

		return _close(handle);
	}

	return -1;
}

void rge_fast_forward(handle handle, int32 size)
{
	if (handle != INVALID_HANDLE && handle == current_handle)
	{
		if (file_size) file_size -= size;

		_lseek(handle, size, SEEK_CUR);
	}
}

void rge_read_uncompressed(handle handle, void *data, int32 size)
{
	if (handle != INVALID_HANDLE && handle == current_handle)
	{
		_read(handle, data, size);
		file_size -= size;
	}
}

void rge_write_uncompressed(handle handle, void *data, int32 size)
{
	if (handle != INVALID_HANDLE && handle == current_handle)
	{
		if (_write(handle, data, size) == -1) rge_write_error = TRUE;
	}
}

void rge_read_full(handle handle, void **data, int32 *size)
{
	if (handle != INVALID_HANDLE && handle == current_handle)
	{
		size_t temp_size;
		size_t temp_max = sizeof(buffers);

		if (flags == FLAG_FIRST_INFLATE)
		{
			flags = FLAG_INFLATE;

			file_buffers = rge_malloc(file_size);
			_read(handle, file_buffers, file_size);

			compression_buffers = rge_calloc(Inf32BufSize(), 1);
			compression_point = 0;
		}

		int32 code;
		int32 data_size = 0;
		size_t data_alloc = sizeof(buffers) * 16;
		byte *data_ptr = rge_malloc(data_alloc);

		do
		{
			temp_size = file_size;
			temp_max = sizeof(buffers);
			code = Inf32Decode(file_buffers, compression_point, &temp_size, buffers, 0, &temp_max, compression_buffers, TRUE);
			compression_point += temp_size;

			if (data_size + temp_max > data_alloc)
			{
				size_t new_alloc = data_alloc * 2;
				byte *new_data_ptr = rge_malloc(new_alloc);

				memcpy(new_data_ptr, data_ptr, data_alloc);
				rge_free(data_ptr);

				data_alloc = new_alloc;
				data_ptr = new_data_ptr;
			}

			memcpy(data_ptr + data_size, buffers, temp_max);

			data_size += temp_max;
		}
		while (code != INFLATE_EOF);

		*data = data_ptr;
		*size = data_size;
	}
}

void rge_read(handle handle, void *data, int32 size)
{
	if (handle != INVALID_HANDLE && handle == current_handle)
	{
		byte *temp = (byte *)data;

		size_t temp_size;
		size_t temp_max = sizeof(buffers);

		if (flags == FLAG_FIRST_INFLATE)
		{
			flags = FLAG_INFLATE;

			file_buffers = rge_malloc(file_size);
			_read(handle, file_buffers, file_size);

			compression_buffers = rge_calloc(Inf32BufSize(), 1);
			compression_point = 0;

			temp_size = file_size;
			Inf32Decode(file_buffers, compression_point, &temp_size, buffers, 0, &temp_max, compression_buffers, TRUE);
			compression_point += temp_size;
		}

		if (size + point >= sizeof(buffers))
		{
			do
			{
				memcpy(temp, current, sizeof(buffers) - point);
				size -= sizeof(buffers) - point;
				temp += sizeof(buffers) - point;
				point = 0;
				current = buffers;

				temp_size = file_size;
				temp_max = sizeof(buffers);
				Inf32Decode(file_buffers, compression_point, &temp_size, buffers, 0, &temp_max, compression_buffers, TRUE);
				compression_point += temp_size;
			}
			while (size >= sizeof(buffers));
		}

		if (size > 0)
		{
			memcpy(temp, current, size);
			point += size;
			current += size;
		}
	}
}

static int32 rge_buffer_full(byte *out_buf_ofs, int32 out_buf_size)
{
	rge_write_uncompressed(current_handle, out_buf_ofs, out_buf_size);

	return 0;
}

void rge_write(handle handle, void *data, int32 size)
{
	if (handle != INVALID_HANDLE && handle == current_handle)
	{
		if (flags == FLAG_FIRST_DEFLATE)
		{
			flags = FLAG_DEFLATE;

			compression_buffers = rge_calloc(deflate_buf_size(), 1);
			deflate_init(compression_buffers, DEFLATE_MAX_COMPARES_DEFAULT, DEFLATE_STRATEGY_DEFAULT, TRUE, buffers, sizeof(buffers), &rge_buffer_full);
		}

		if (deflate_data(compression_buffers, (byte *)data, size, FALSE) == DEFLATE_ERROR) rge_write_error = TRUE;
	}
}
