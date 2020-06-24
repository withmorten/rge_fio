#ifdef _WIN32
#include <io.h>
#else
#include <sys/io.h>
#endif
#include <fcntl.h>
#include <sys/stat.h>

#include "main.h"
#include "rge_fio.h"

int32 main(int32 argc, char **argv)
{
	if (argc < 4)
	{
		printf("usage: rge_fio r/w <in> <out> [num uncompressed header bytes] [r: num bytes to read - i.e. filesize of decompressed file]\n");

		return 1;
	}

	if (*argv[1] == 'r')
	{
		handle h = rge_open_read_(argv[2]);

		FILE *out = rge_fopen(argv[3], "wb");

		if (argc == 5)
		{
			int32 num_uncompressed_bytes = atoi(argv[4]);
			void *uncompressed = rge_malloc(num_uncompressed_bytes);

			rge_read_uncompressed(h, uncompressed, num_uncompressed_bytes);

			fwrite(uncompressed, num_uncompressed_bytes, 1, out);

			rge_free(uncompressed);
		}

		int32 num_decompressed_bytes = 0;
		void *decompressed = NULL;

		if (argc == 6)
		{
			num_decompressed_bytes = atoi(argv[5]);
			decompressed = rge_malloc(num_decompressed_bytes);

			rge_read(h, decompressed, num_decompressed_bytes);
		}
		else
		{
			rge_read_full(h, &decompressed, &num_decompressed_bytes);
		}

		fwrite(decompressed, num_decompressed_bytes, 1, out);

		rge_free(decompressed);

		rge_fclose(out);

		rge_close(h);
	}
	else if (*argv[1] == 'w')
	{
		handle h = rge_open_write_(argv[3]);

		FILE *in = rge_fopen(argv[2], "rb");

		if (argc == 5)
		{
			int32 num_uncompressed_bytes = atoi(argv[4]);
			void *uncompressed = malloc(num_uncompressed_bytes);

			fread(uncompressed, num_uncompressed_bytes, 1, in);

			rge_write_uncompressed(h, uncompressed, num_uncompressed_bytes);
		}

		size_t pos = ftell(in);
		fseek(in, 0, SEEK_END);
		size_t size = ftell(in) - pos;
		fseek(in, pos, SEEK_SET);

		void *data = rge_malloc(size);

		fread(data, size, 1, in);

		rge_write(h, data, size);

		rge_free(data);

		rge_fclose(in);

		rge_close(h);
	}
	else
	{
		printf("usage: rge_fio r/w <in> <out>\n");

		return 1;
	}

	return 0;
}
