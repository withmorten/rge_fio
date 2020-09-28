#ifdef _WIN32
#include <io.h>
#else
#include <sys/io.h>
#endif
#include <fcntl.h>
#include <sys/stat.h>

#include "main.h"
#include "rge_fio.h"

#define USAGE \
"usage: rge_fio r/w <in> <out> [num uncompressed bytes at start] [offset from which to read/to write to]\n\n"

int32 main(int32 argc, char **argv)
{
	if (argc < 4)
	{
		printf(USAGE);

		return 1;
	}

	if (*argv[1] == 'r')
	{
		handle h = rge_open_read_(argv[2]);

		if (h == INVALID_HANDLE)
		{
			printf("error: couldn't open %s for reading\n", argv[2]);

			return 1;
		}

		FILE *out = rge_fopen(argv[3], "wb");

		if (!out)
		{
			printf("error: couldn't fopen %s for writing\n", argv[3]);

			return 1;
		}

		if (argc == 6)
		{
			int32 num_skip_bytes = atoi(argv[5]);

			rge_fast_forward(h, num_skip_bytes);
		}

		int32 num_uncompressed_bytes;

		if (argc == 5)
		{
			num_uncompressed_bytes = atoi(argv[4]);
			void *uncompressed = malloc(num_uncompressed_bytes);

			rge_read_uncompressed(h, uncompressed, num_uncompressed_bytes);

			fwrite(uncompressed, num_uncompressed_bytes, 1, out);

			printf("wrote %d uncompressed bytes\n", num_uncompressed_bytes);

			rge_free(uncompressed);
		}

		int32 num_decompressed_bytes = 0;
		void *decompressed = NULL;

		rge_read_full(h, &decompressed, &num_decompressed_bytes);

		fwrite(decompressed, num_decompressed_bytes, 1, out);

		printf("wrote %d decompressed bytes\n", num_decompressed_bytes);

		rge_free(decompressed);

		rge_fclose(out);

		rge_close(h);
	}
	else if (*argv[1] == 'w')
	{
		handle h;

		if (argc == 6)
		{
			h = rge_open_write(argv[3], _O_WRONLY | _O_CREAT | _O_BINARY, _S_IREAD | _S_IWRITE);

			int32 num_skip_bytes = atoi(argv[5]);

			rge_fast_forward(h, num_skip_bytes);
		}
		else
		{
			h = rge_open_write_(argv[3]);
		}

		if (h == INVALID_HANDLE)
		{
			printf("error: couldn't open %s for writing\n", argv[3]);

			return 1;
		}

		FILE *in = rge_fopen(argv[2], "rb");

		if (!in)
		{
			printf("error: couldn't fopen %s for reading\n", argv[2]);

			return 1;
		}

		int32 num_uncompressed_bytes = 0;

		if (argc == 5)
		{
			num_uncompressed_bytes = atoi(argv[4]);
			void *uncompressed = malloc(num_uncompressed_bytes);

			fread(uncompressed, num_uncompressed_bytes, 1, in);

			rge_write_uncompressed(h, uncompressed, num_uncompressed_bytes);

			printf("wrote %d uncompressed bytes\n", num_uncompressed_bytes);
		}

		size_t pos = ftell(in);
		fseek(in, 0, SEEK_END);
		size_t size = ftell(in) - pos;
		fseek(in, pos, SEEK_SET);

		void *data = malloc(size);

		fread(data, size, 1, in);

		rge_write(h, data, size);

		printf("wrote %d compressed bytes\n", _tell(h) - num_uncompressed_bytes);

		rge_free(data);

		rge_fclose(in);

		rge_close(h);
	}
	else
	{
		printf(USAGE);

		return 1;
	}

	return 0;
}
