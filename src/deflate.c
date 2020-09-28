// use zlib deflate instead of game deflate, produces slightly smaller output files and is a lot faster,
// but uses more than infile size memory vs a fixed UINT16_MAX buffer size
// #define USE_ZLIB_DEFLATE

#ifdef USE_ZLIB_DEFLATE
#include <zlib.h>
#endif

#include "compress.h"

#ifdef USE_ZLIB_DEFLATE
local void *zalloc(void *opaque, uint32 items, uint32 size)
{
	return calloc(size, items);
};

local void zfree(void *opaque, void *address)
{
	free(address);
}

local z_stream deflate_stream = ZEROMEM;
local int32 deflate_code;

local byte *buffer = NULL;
local size_t buffer_len = 0;

#define DEFAULT_ALLOC 0x10000

local byte *data = NULL;
local size_t data_alloc = 0;
local size_t data_pos = 0;

local int32 (*flush_out_buf)(byte *out_buf_ofs, int32 out_buf_size) = NULL;

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
	data = malloc(DEFAULT_ALLOC);
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
			byte *new_data = malloc(new_alloc);

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

		deflate_code = deflateInit2(&deflate_stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -15, 9, Z_DEFAULT_STRATEGY);

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

#define DEFLATE_MIN_COMPARE 1
#define DEFLATE_MAX_COMPARE 1500

#define DEFLATE_GREEDY_COMPARE_THRESHOLD 4

#define DEFLATE_SIG_INIT 0x12345678
#define DEFLATE_SIG_DONE 0xABCD1234

#define read_word(src) *(uint16 *)(src)
#define write_word(dst, w) *(uint16 *)(dst) = (uint16)(w)

#define PUT_BYTE(c) do { while (--out_buf_left < 0) { out_buf_left++; if (flush_out_buffer()) return TRUE; } *out_buf_cur_ofs++ = c; } while(0)

#define FLAG(i) do { \
	*wd->flag_buf_ofs = (*wd->flag_buf_ofs << 1) | (i); \
	if (!--wd->flag_buf_left) { if (empty_flag_buf()) return TRUE; } \
} while(0)

#define CHAR do { \
	*wd->token_buf_ofs++ = dict[wd->search_offset++]; \
	wd->search_bytes_left--; \
	wd->token_buf_bytes++; \
	FLAG(0); \
} while(0)

#define MATCH(len, dist) do { \
	*wd->token_buf_ofs++ = (byte)((len) - DEFLATE_MIN_MATCH); \
	write_word(wd->token_buf_ofs, dist); \
	wd->token_buf_ofs += 2; \
	wd->search_offset += (len); \
	wd->search_bytes_left -= (len); \
	wd->token_buf_bytes += (len); \
	FLAG(1); \
} while(0)

local work_data *wd = NULL;

local byte *dict = NULL;
local uint16 *hash = NULL;
local uint16 *next = NULL;
local uint16 *last = NULL;

local int32 max_compares = 0;
local int32 match_len = 0;
local uint32 match_pos = 0;

local uint32 bit_buf = 0;
local int32 bit_buf_len = 0;
local bool32 bit_buf_total_flag = 0;

local byte *out_buf_cur_ofs = NULL;
local int32 out_buf_left = 0;

local int32 code_list_len = 0;
local int32 num_codes[33] = ZEROMEM;
local int32 next_code[33] = ZEROMEM;
local int32 new_code_sizes[DEFLATE_MAX_SYMBOLS] = ZEROMEM;
local int32 code_list[DEFLATE_MAX_SYMBOLS] = ZEROMEM;
local int32 others[DEFLATE_MAX_SYMBOLS] = ZEROMEM;
local int32 heap[DEFLATE_MAX_SYMBOLS + 1] = ZEROMEM;

local void int_set(int32 *dst, int32 dat, size_t len);
local void uint_set(uint32 *dst, uint32 dat, size_t len);
local void ushort_set(uint16 *dst, uint16 dat, size_t len);
local void int_move(int32 *dst, int32 *src, size_t len);
local int32 *repeat_last(int32 *dst, int32 size, int32 run_len);
local int32 *repeat_zero(int32 *dst, int32 run_len);

local void init_compress_code_sizes();
local bool32 compress_code_sizes();

local void huff_down_heap(int32 *heap, int32 *sym_freq, int32 heap_len, int32 i);
local void huff_code_sizes(int32 num_symbols, int32 *sym_freq, int32 *code_sizes);
local void huff_sort_code_sizes(int32 num_symbols, int32 *code_sizes);
local void huff_fix_code_sizes(int32 max_code_size);
local void huff_make_codes(int32 num_symbols, int32 *code_sizes, int32 max_code_size, uint32 *codes);

local bool32 send_static_block();
local bool32 send_dynamic_block();
local bool32 send_raw_block();

local void init_static_block();
local void init_dynamic_block();

local bool32 code_block();
local bool32 code_token_buf(bool32 last_block_flag);

local void delete_data(int32 dict_pos);
local void hash_data(int32 dict_pos, int32 bytes_to_do);
local void find_match(int32 dict_pos);

local bool32 empty_flag_buf();
local bool32 flush_flag_buf();
local bool32 flush_out_buffer();
local bool32 put_bits(int32 bits, int32 len);
local bool32 flush_bits();

local bool32 dict_search_lazy();
local bool32 dict_search_flash();
local bool32 dict_search_greedy();
local bool32 dict_search();
local bool32 dict_search_main(int32 dict_ofs);
local bool32 dict_search_eof();
local bool32 dict_fill();

local void deflate_main_init();
local int32 deflate_main();

local void int_set(int32 *dst, int32 dat, size_t len)
{
	while (len--) dst[len] = dat;
}

local void uint_set(uint32 *dst, uint32 dat, size_t len)
{
	while (len--) dst[len] = dat;
}

local void ushort_set(uint16 *dst, uint16 dat, size_t len)
{
	while (len--) dst[len] = dat;
}

local void int_move(int32 *dst, int32 *src, size_t len)
{
	while (len--) *dst++ = *src++;
}

local void mem_copy(byte *dst, byte *src, size_t len)
{
	memcpy(dst, src, len);
}

local void mem_set(byte *dst, byte c, size_t len)
{
	memset(dst, c, len);
}

local int32 *repeat_last(int32 *dst, int32 size, int32 run_len)
{
	if (run_len < 3)
	{
		wd->freq_3[size] += run_len;

		while (run_len--) *dst++ = size;
	}
	else
	{
		wd->freq_3[16]++;

		*dst++ = 16;
		*dst++ = run_len - 3;
	}

	return dst;
}

local int32 *repeat_zero(int32 *dst, int32 run_len)
{
	if (run_len < 3)
	{
		wd->freq_3[0] += run_len;

		while (run_len--) *dst++ = 0;
	}
	else if (run_len <= 10)
	{
		wd->freq_3[17]++;

		*dst++ = 17;
		*dst++ = run_len - 3;
	}
	else
	{
		wd->freq_3[18]++;

		*dst++ = 18;
		*dst++ = run_len - 11;
	}

	return dst;
}

local void init_compress_code_sizes()
{
	int_set(wd->freq_3, 0x00, DEFLATE_NUM_SYMBOLS_3);

	for (wd->used_lit_codes = 285; wd->used_lit_codes >= 0; wd->used_lit_codes--)
	{
		if (wd->size_1[wd->used_lit_codes]) break;
	}

	wd->used_lit_codes = max(257, wd->used_lit_codes + 1);

	for (wd->used_dist_codes = 29; wd->used_dist_codes >= 0; wd->used_dist_codes--)
	{
		if (wd->size_2[wd->used_dist_codes]) break;
	}

	wd->used_dist_codes = max(1, wd->used_dist_codes + 1);

	int_move(wd->bundled_sizes, wd->size_1, wd->used_lit_codes);
	int_move(&wd->bundled_sizes[wd->used_lit_codes], wd->size_2, wd->used_dist_codes);

	int32 last_size = 0xFF;
	int32 run_len_z = 0;
	int32 run_len_nz = 0;
	int32 *src = wd->bundled_sizes;
	int32 *dst = wd->coded_sizes;

	for (int32 codes_left = wd->used_dist_codes + wd->used_lit_codes; codes_left > 0; codes_left--)
	{
		int32 size = *src++;

		if (size)
		{
			if (run_len_z)
			{
				dst = repeat_zero(dst, run_len_z);

				run_len_z = 0;
			}

			if (last_size == size)
			{
				if (++run_len_nz == 6)
				{
					dst = repeat_last(dst, last_size, run_len_nz);

					run_len_nz = 0;
				}
			}
			else
			{
				if (run_len_nz)
				{
					dst = repeat_last(dst, last_size, run_len_nz);

					run_len_nz = 0;
				}

				*dst++ = size;
				wd->freq_3[size]++;
			}
		}
		else
		{
			if (run_len_nz)
			{
				dst = repeat_last(dst, last_size, run_len_nz);

				run_len_nz = 0;
			}

			if (++run_len_z == 138)
			{
				dst = repeat_zero(dst, run_len_z);

				run_len_z = 0;
			}
		}

		last_size = size;
	}

	if (run_len_nz)
	{
		dst = repeat_last(dst, last_size, run_len_nz);
	}
	else if (run_len_z)
	{
		dst = repeat_zero(dst, run_len_z);
	}

	wd->coded_sizes_end = dst;

	huff_code_sizes(DEFLATE_NUM_SYMBOLS_3, wd->freq_3, wd->size_3);
	huff_sort_code_sizes(DEFLATE_NUM_SYMBOLS_3, wd->size_3);
	huff_fix_code_sizes(7);
	huff_make_codes(DEFLATE_NUM_SYMBOLS_3, wd->size_3, 7, wd->code_3);
}

local bool32 compress_code_sizes()
{
	if (put_bits(wd->used_lit_codes - 257, 5)) return TRUE;

	if (put_bits(wd->used_dist_codes - 1, 5)) return TRUE;

	int32 bit_lengths;

	for (bit_lengths = 18; bit_lengths >= 0; bit_lengths--)
	{
		if (wd->size_3[bit_length_order[bit_lengths]]) break;
	}

	bit_lengths = max(4, (bit_lengths + 1));

	if (put_bits(bit_lengths - 4, 4)) return TRUE;

	if (bit_lengths <= 0)
	{
		int32 *src = wd->coded_sizes;

		while (wd->coded_sizes_end > src)
		{
			int32 i = *src++;

			if (put_bits(wd->code_3[i], wd->size_3[i])) return TRUE;

			if (i == 16)
			{
				if (put_bits(*src++, 2)) return TRUE;
			}
			else if (i == 17)
			{
				if (put_bits(*src++, 3)) return TRUE;
			}
			else if (i == 18)
			{
				if (put_bits(*src++, 7)) return TRUE;
			}
		}

		return FALSE;
	}
	else
	{
		int32 j = 0;

		while (!put_bits(wd->size_3[bit_length_order[j++]], 3))
		{
			if (j >= bit_lengths)
			{
				int32 *src = wd->coded_sizes;

				while (wd->coded_sizes_end > src)
				{
					int32 i = *src++;

					if (put_bits(wd->code_3[i], wd->size_3[i])) return TRUE;

					if (i == 16)
					{
						if (put_bits(*src++, 2)) return TRUE;
					}
					else if (i == 17)
					{
						if (put_bits(*src++, 3)) return TRUE;
					}
					else if (i == 18)
					{
						if (put_bits(*src++, 7)) return TRUE;
					}
				}

				return FALSE;
			}
		}

		return TRUE;
	}
}

local void huff_down_heap(int32 *heap, int32 *sym_freq, int32 heap_len, int32 i)
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

local void huff_code_sizes(int32 num_symbols, int32 *sym_freq, int32 *code_sizes)
{
	if (num_symbols > 0)
	{
		int_set(others, -1, num_symbols);
		int_set(code_sizes, 0, num_symbols);
	}

	int32 heap_len = 1;

	for (int32 i = 0; i < num_symbols; i++)
	{
		if (sym_freq[i]) heap[heap_len++] = i;
	}

	heap_len--;

	if (heap_len <= 1)
	{
		if (!heap_len) return;

		code_sizes[heap[1]] = 1;

		return;
	}

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

local void huff_sort_code_sizes(int32 num_symbols, int32 *code_sizes)
{
	code_list_len = 0;
	int_set(num_codes, 0, 33);

	for (int32 i = 0; i < num_symbols; i++)
	{
		num_codes[code_sizes[i]]++;
	}

	for (int32 i = 1, j = 0; i <= 32; i++)
	{
		next_code[i] = j;
		j += num_codes[i];
	}

	for (int32 i = 0; i < num_symbols; i++)
	{
		int32 j = code_sizes[i];

		if (j)
		{
			code_list[next_code[j]++] = i;
			code_list_len++;
		}
	}
}

local void huff_fix_code_sizes(int32 max_code_size)
{
	if (code_list_len > 1)
	{
		for (int32 i = max_code_size + 1; i <= 32; i++)
		{
			num_codes[max_code_size] += num_codes[i];
		}

		int32 total = 0;

		for (int32 i = max_code_size; i > 0; i--)
		{
			total += (uint32)num_codes[i] << (max_code_size - i);
		}

		while (total != (1u << max_code_size))
		{
			num_codes[max_code_size]--;

			for (int32 i = max_code_size - 1; i > 0; i--)
			{
				if (num_codes[i])
				{
					num_codes[i]--;
					num_codes[i + 1] += 2;

					break;
				}
			}

			total--;
		}
	}
}

local void huff_make_codes(int32 num_symbols, int32 *code_sizes, int32 max_code_size, uint32 *codes)
{
	if (!code_list_len) return;

	uint_set(codes, 0x00, num_symbols);

	for (int32 i = 1, k = 0; i <= max_code_size; i++)
	{
		int_set(&new_code_sizes[k], i, num_codes[i]);

		k += num_codes[i];
	}

	next_code[1] = 0;

	for (int32 i = 0, j = 0; i <= max_code_size; i++)
	{
		next_code[i] = j = ((j + num_codes[i - 1]) << 1);
	}

	for (int32 i = 0; i < code_list_len; i++)
	{
		code_sizes[code_list[i]] = new_code_sizes[i];
	}

	for (int32 i = 0; i < num_symbols; i++)
	{
		if (code_sizes[i])
		{
			int32 j = next_code[code_sizes[i]]++;

			int32 k = 0;

			for (int32 l = code_sizes[i]; l > 0; l--)
			{
				k = (k << 1) | (j & 1);
				j >>= 1;
			}

			codes[i] = k;
		}
	}
}

local bool32 send_static_block()
{
	return put_bits(1, 2) != FALSE;
}

local bool32 send_dynamic_block()
{
	if (put_bits(2, 2)) return TRUE;

	return compress_code_sizes() != FALSE;
}

local bool32 send_raw_block()
{
	if (put_bits(0, 2)) return TRUE;

	if (flush_bits()) return TRUE;

	PUT_BYTE((byte)(wd->token_buf_bytes & 0xFF));
	PUT_BYTE((byte)(wd->token_buf_bytes >> 8));

	PUT_BYTE((byte)(~wd->token_buf_bytes & 0xFF));
	PUT_BYTE((byte)(~wd->token_buf_bytes >> 8));

	uint32 src = wd->token_buf_start;

	for (int32 len = wd->token_buf_bytes; len > 0; len--)
	{
		PUT_BYTE(dict[src++]);

		src &= (DEFLATE_DICT_SIZE - 1);
	}

	return FALSE;
}

local void init_dynamic_block()
{
	int_set(wd->freq_1, 0, DEFLATE_NUM_SYMBOLS_1);
	int_set(wd->freq_2, 0, DEFLATE_NUM_SYMBOLS_2);

	int32 flag_left = 0;
	uint32 flag = 0;
	uint32 *flag_buf_ptr = wd->flag_buf;
	byte *token_ptr = wd->token_buf;

	for (int32 tokens_left = wd->token_buf_len; tokens_left > 0; tokens_left--)
	{
		if (!flag_left)
		{
			flag = *flag_buf_ptr++;
			flag_left = 32;
		}

		if (flag & 0x80000000)
		{
			wd->freq_1[len_code[*token_ptr]]++;

			uint32 match_dist = read_word(token_ptr + 1) - 1;

			if (match_dist < 512)
			{
				wd->freq_2[dist_lo_code[match_dist]]++;
			}
			else
			{
				wd->freq_2[dist_hi_code[match_dist >> 8]]++;
			}

			token_ptr += 3;
		}
		else
		{
			wd->freq_1[*token_ptr++]++;;
		}

		flag <<= 1;
		flag_left--;
	}

	wd->freq_1[256]++;

	huff_code_sizes(DEFLATE_NUM_SYMBOLS_1, wd->freq_1, wd->size_1);
	huff_sort_code_sizes(DEFLATE_NUM_SYMBOLS_1, wd->size_1);
	huff_fix_code_sizes(15);
	huff_make_codes(DEFLATE_NUM_SYMBOLS_1, wd->size_1, 15, wd->code_1);

	huff_code_sizes(DEFLATE_NUM_SYMBOLS_2, wd->freq_2, wd->size_2);
	huff_sort_code_sizes(DEFLATE_NUM_SYMBOLS_2, wd->size_2);
	huff_fix_code_sizes(15);
	huff_make_codes(DEFLATE_NUM_SYMBOLS_2, wd->size_2, 15, wd->code_2);

	init_compress_code_sizes();
}

local void init_static_block()
{
	int_set(wd->size_1 + 0x00, 8, 0x90);
	int_set(wd->size_1 + 0x90, 9, 0x70);
	int_set(wd->size_1 + 0x100, 7, 0x18);
	int_set(wd->size_1 + 0x118, 8, 0x08);

	huff_sort_code_sizes(DEFLATE_NUM_SYMBOLS_1, wd->size_1);
	huff_make_codes(DEFLATE_NUM_SYMBOLS_1, wd->size_1, 15, wd->code_1);

	int_set(wd->size_2, 5, DEFLATE_NUM_SYMBOLS_2);

	huff_sort_code_sizes(DEFLATE_NUM_SYMBOLS_2, wd->size_2);
	huff_make_codes(DEFLATE_NUM_SYMBOLS_2, wd->size_2, 15, wd->code_2);
}

local bool32 code_block()
{
	byte *token_ptr = wd->token_buf;
	uint32 flag_left = 0;
	int32 flag = 0;
	uint32 *flag_buf_ptr = wd->flag_buf;

	for (int32 token_buf_len = wd->token_buf_len; token_buf_len > 0; token_buf_len--)
	{
		if (!flag_left)
		{
			flag_left = 32;
			flag = *flag_buf_ptr++;
		}

		if (flag < 0)
		{
			uint32 match_len = *token_ptr;
			uint32 match_dist = read_word(token_ptr + 1) - 1;

			if (put_bits(wd->code_1[len_code[match_len]], wd->size_1[len_code[match_len]])) return TRUE;

			if (put_bits((byte)(match_len & len_mask[match_len]), len_extra[match_len])) return TRUE;

			if (match_dist < 512)
			{
				if (put_bits(wd->code_2[dist_lo_code[match_dist]], wd->size_2[dist_lo_code[match_dist]])) return TRUE;

				if (put_bits(match_dist & dist_lo_mask[match_dist], dist_lo_extra[match_dist])) return TRUE;
			}
			else
			{
				uint32 match_dist_hi = match_dist >> 8;

				if (put_bits(wd->code_2[dist_hi_code[match_dist_hi]], wd->size_2[dist_hi_code[match_dist_hi]])) return TRUE;

				if (put_bits(match_dist & dist_hi_mask[match_dist_hi], dist_hi_extra[match_dist_hi])) return TRUE;
			}

			token_ptr += 3;
		}
		else
		{
			byte token_buf_content = *token_ptr++;

			if (put_bits(wd->code_1[token_buf_content], wd->size_1[token_buf_content])) return TRUE;
		}

		flag <<= 1;
		flag_left--;
	}

	return put_bits(wd->code_1[256], wd->size_1[256]) != FALSE;
}

local bool32 code_token_buf(bool32 last_block_flag)
{
	wd->token_buf_end = wd->search_offset;

	if (wd->token_buf_len)
	{
		if (put_bits(0, 1)) return TRUE;

		if (wd->strategy == DEFLATE_STATIC_BLOCKS)
		{
			init_static_block();

			if (send_static_block()) return TRUE;
			if (code_block()) return TRUE;
		}
		else if (wd->strategy == DEFLATE_DYNAMIC_BLOCKS)
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

			uint32 static_bits = wd->bit_buf_total;
			wd->bit_buf_total = 0;

			init_dynamic_block();

			if (send_dynamic_block()) return TRUE;
			if (code_block()) return TRUE;

			uint32 dynamic_bits = wd->bit_buf_total;
			bit_buf_total_flag = FALSE;

			uint32 raw_bits = 2 + 32 + (wd->token_buf_bytes << 3);

			if (((byte)bit_buf_len + 2) & 7)
			{
				raw_bits += (8 - ((bit_buf_len + 2) & 7));
			}

			if (raw_bits < static_bits && raw_bits < dynamic_bits)
			{
				if (send_raw_block()) return TRUE;
			}
			else
			{
				if (static_bits < dynamic_bits)
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
		}
		else
		{
			if (wd->token_buf_bytes >= (DEFLATE_MAX_TOKENS + (DEFLATE_MAX_TOKENS / 5)))
			{
				init_dynamic_block();

				if (send_dynamic_block()) return TRUE;
				if (code_block()) return TRUE;
			}
			else
			{
				bit_buf_total_flag = TRUE;
				wd->bit_buf_total = 0;

				init_dynamic_block();

				if (send_dynamic_block()) return TRUE;
				if (code_block()) return TRUE;

				uint32 dynamic_bits = wd->bit_buf_total;
				bit_buf_total_flag = FALSE;

				uint32 raw_bits = 2 + 32 + (wd->token_buf_bytes << 3);

				if (((byte)bit_buf_len + 2) & 7)
				{
					raw_bits += (8 - ((bit_buf_len + 2) & 7));
				}

				if (raw_bits < dynamic_bits)
				{
					if (send_raw_block()) return TRUE;
				}
				else
				{
					if (send_dynamic_block()) return TRUE;
					if (code_block()) return TRUE;
				}
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

local void delete_data(int32 dict_pos)
{
	uint32 k = dict_pos + DEFLATE_SECTOR_SIZE;

	for (uint32 i = dict_pos; i < k; i++)
	{
		uint32 j = last[i];

		if (j & DEFLATE_HASH_FLAG_1)
		{
			if (j != DEFLATE_NIL) hash[j & DEFLATE_HASH_FLAG_2] = DEFLATE_NIL;
		}
		else
		{
			next[j] = DEFLATE_NIL;
		}
	}
}

local void hash_data(int32 dict_pos, int32 bytes_to_do)
{
	uint32 i = max(0, bytes_to_do - DEFLATE_THRESHOLD);

	if (i < (uint32)bytes_to_do)
	{
		ushort_set(&last[i + dict_pos], DEFLATE_NIL, bytes_to_do - i);
		ushort_set(&next[i + dict_pos], DEFLATE_NIL, bytes_to_do - i);
	}

	if (bytes_to_do > DEFLATE_THRESHOLD)
	{
		uint32 k = dict_pos + bytes_to_do - DEFLATE_THRESHOLD;
		uint32 j = ((uint32)dict[dict_pos] << DEFLATE_SHIFT_BITS) ^ dict[dict_pos + 1];

		for (uint32 i = dict_pos; i < k; i++)
		{
			j = ((j << DEFLATE_SHIFT_BITS) & (DEFLATE_HASH_SIZE - 1)) ^ dict[i + DEFLATE_THRESHOLD];

			last[i] = j | DEFLATE_HASH_FLAG_1;

			next[i] = hash[j];

			if (next[i] != DEFLATE_NIL) last[next[i]] = i;

			hash[j] = i;
		}
	}
}

local void find_match(int32 dict_pos)
{
	uint16 *r = (uint16 *)&dict[dict_pos];

	uint16 l = read_word(&dict[dict_pos + match_len - 1]);
	uint16 m = read_word(r);

	byte *s = &dict[match_len - 1];

	int32 compares_left = max_compares;
	uint16 probe_pos = dict_pos & (DEFLATE_DICT_SIZE - 1);

	for (ever)
	{
		for (ever)
		{
			if (compares_left <= 0) return;

			compares_left--;
			probe_pos = next[probe_pos];
			if (probe_pos == DEFLATE_NIL) return;

			if (read_word(&s[probe_pos]) == l) break;

			compares_left--;
			probe_pos = next[probe_pos];
			if (probe_pos == DEFLATE_NIL) return;

			if (read_word(&s[probe_pos]) == l) break;

			compares_left--;
			probe_pos = next[probe_pos];
			if (probe_pos == DEFLATE_NIL) return;

			if (read_word(&s[probe_pos]) == l) break;

			compares_left--;
			probe_pos = next[probe_pos];
			if (probe_pos == DEFLATE_NIL) return;

			if (read_word(&s[probe_pos]) == l) break;
		}

		if (read_word(&dict[probe_pos]) != m) continue;

		uint16 *p = r;
		uint16 *q = (uint16 *)&dict[probe_pos];

		int32 probe_len = 32;

		do
		{
		}
		while (read_word(++p) == read_word(++q)
			&& read_word(++p) == read_word(++q)
			&& read_word(++p) == read_word(++q)
			&& read_word(++p) == read_word(++q)
			&& --probe_len > 0);

		if (!probe_len) goto max_match;

		probe_len = ((p - r) * 2) + (*(byte *)p == *(byte *)q); // read_word doesn't work???

		if (probe_len > match_len)
		{
			match_pos = probe_pos;
			match_len = probe_len;

			l = read_word(&dict[dict_pos + match_len - 1]);
			s = &dict[match_len - 1];
		}
	}

	return;

max_match:;
	match_pos = probe_pos;
	match_len = DEFLATE_MAX_MATCH;
}

local bool32 empty_flag_buf()
{
	wd->flag_buf_ofs++;
	wd->flag_buf_left = 32;
	wd->token_buf_len += 32;

	if (wd->token_buf_len == DEFLATE_MAX_TOKENS)
	{
		return code_token_buf(FALSE);
	}

	return FALSE;
}

local bool32 flush_flag_buf()
{
	if (wd->flag_buf_left != 32)
	{
		wd->token_buf_len += 32 - wd->flag_buf_left;

		while (wd->flag_buf_left)
		{
			*wd->flag_buf_ofs <<= 1;
			wd->flag_buf_left--;
		}

		wd->flag_buf_ofs++;
		wd->flag_buf_left = 32;
	}

	return code_token_buf(TRUE);
}

local bool32 flush_out_buffer()
{
	if (wd->flush_out_buf(wd->out_buf_ofs, wd->out_buf_size - out_buf_left)) return TRUE;

	out_buf_cur_ofs = wd->out_buf_ofs;
	out_buf_left = wd->out_buf_size;

	return FALSE;
}

local bool32 put_bits(int32 bits, int32 len)
{
	if (bit_buf_total_flag) goto bit_buf_total;

	bit_buf |= bits << bit_buf_len;
	bit_buf_len += len;

	if (bit_buf_len < 8)
	{
		return FALSE;
	}

	if (bit_buf_len >= 16) goto flush_word;

	if (--out_buf_left < 0) goto flush_byte;

	*out_buf_cur_ofs++ = (byte)(bit_buf & 0xFF);

	bit_buf >>= 8;
	bit_buf_len -= 8;

	return FALSE;

flush_byte:;

	out_buf_left++;

	PUT_BYTE((byte)(bit_buf & 0xFF));

	bit_buf >>= 8;
	bit_buf_len -= 8;

	return FALSE;

flush_word:;

	PUT_BYTE((byte)(bit_buf & 0xFF));
	PUT_BYTE((byte)((bit_buf >> 8) & 0xFF));

	bit_buf >>= 16;
	bit_buf_len -= 16;

	return FALSE;

bit_buf_total:;
	wd->bit_buf_total += len;

	return FALSE;
}

local bool32 flush_bits()
{
	if (put_bits(0, 7)) return TRUE;

	bit_buf_len = 0;

	return FALSE;
}

local bool32 dict_search_lazy()
{
	while (wd->search_bytes_left && wd->search_offset < wd->search_threshold)
	{
		if (next[wd->search_offset & (DEFLATE_DICT_SIZE - 1)] == DEFLATE_NIL)
		{
			CHAR;

			continue;
		}

		match_len = DEFLATE_THRESHOLD;

		find_match(wd->search_offset);

		if (match_len == DEFLATE_THRESHOLD)
		{
			CHAR;

			continue;
		}

		int32 match_len_cur = match_len;
		int32 match_pos_cur = match_pos;

		while (match_len_cur < 128)
		{
			if (next[((uint16)wd->search_offset + 1) & (DEFLATE_DICT_SIZE - 1)] != DEFLATE_NIL) find_match(wd->search_offset + 1);
			else break;

			if (match_len > wd->search_bytes_left - 1) match_len = wd->search_bytes_left - 1;

			if (match_len <= match_len_cur) break;

			match_len_cur = match_len;
			match_pos_cur = match_pos;

			CHAR;
		}

		if (match_len_cur > wd->search_bytes_left)
		{
			match_len_cur = wd->search_bytes_left;

			if (wd->search_bytes_left <= 2)
			{
				CHAR;

				continue;
			}
		}

		int32 match_dist = ((uint16)wd->search_offset - (uint16)match_pos_cur) & (DEFLATE_DICT_SIZE - 1);

		if (match_len == DEFLATE_MIN_MATCH && match_dist >= 16384)
		{
			CHAR;
		}
		else
		{
			MATCH(match_len_cur, match_dist);
		}
	}

	wd->search_offset &= (DEFLATE_DICT_SIZE - 1);

	return FALSE;
}

local bool32 dict_search_flash()
{
	while (wd->search_bytes_left && wd->search_offset < wd->search_threshold)
	{
		match_pos = next[wd->search_offset & (DEFLATE_DICT_SIZE - 1)];

		if (match_pos == DEFLATE_NIL)
		{
			CHAR;

			continue;
		}

		uint16 *p = (uint16 *)&dict[match_pos];
		uint16 *q = (uint16 *)&dict[wd->search_offset];

		if (read_word(p) == read_word(q))
		{
			match_len = 32;

			do
			{
			}
			while (read_word(++p) == read_word(++q)
				&& read_word(++p) == read_word(++q)
				&& read_word(++p) == read_word(++q)
				&& read_word(++p) == read_word(++q)
				&& --match_len > 0);

			if (match_len)
			{
				// match_len = ((byte)(*(byte *)dict_search_offset - *(byte *)dict_match_pos) < 1) + (((byte *)dict_match_pos - match_pos - dict) & 0xFFFFFFFE);
				match_len = ((byte)(*q - *p) < 1) + (byte)((byte *)p - match_pos - dict);
			}
			else
			{
				match_len = DEFLATE_MAX_MATCH;
			}

			if (match_len > wd->search_bytes_left)
			{
				match_len = wd->search_bytes_left;

				if (wd->search_bytes_left <= DEFLATE_THRESHOLD)
				{
					CHAR;

					continue;
				}
			}

			int32 match_dist = ((uint16)wd->search_offset - (uint16)match_pos) & (DEFLATE_DICT_SIZE - 1);

			if (match_len == DEFLATE_MIN_MATCH && match_dist >= 16384)
			{
				CHAR;
			}
			else
			{
				MATCH(match_len, match_dist);
			}
		}
		else
		{
			CHAR;
		}
	}

	wd->search_offset &= (DEFLATE_DICT_SIZE - 1);

	return FALSE;
}

local bool32 dict_search_greedy()
{
	while (wd->search_bytes_left && wd->search_offset < wd->search_threshold)
	{
		if (next[wd->search_offset & (DEFLATE_DICT_SIZE - 1)] == DEFLATE_NIL)
		{
			CHAR;

			continue;
		}

		match_len = DEFLATE_THRESHOLD;

		find_match(wd->search_offset);

		if (match_len == DEFLATE_THRESHOLD)
		{
			CHAR;

			continue;
		}

		if (match_len > wd->search_bytes_left)
		{
			match_len = wd->search_bytes_left;

			if (wd->search_bytes_left <= DEFLATE_THRESHOLD)
			{
				CHAR;

				continue;
			}
		}

		int32 match_dist = ((uint16)wd->search_offset - (uint16)match_pos) & (DEFLATE_DICT_SIZE - 1);

		if (match_len == DEFLATE_MIN_MATCH && match_dist >= 16384)
		{
			CHAR;
		}
		else
		{
			MATCH(match_len, match_dist);
		}
	}

	wd->search_offset &= (DEFLATE_DICT_SIZE - 1);

	return FALSE;
}

local bool32 dict_search()
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

local bool32 dict_search_main(int32 dict_ofs)
{
	uint32 search_gap_bytes = (dict_ofs - wd->search_offset) & (DEFLATE_DICT_SIZE - 1);

	wd->search_bytes_left += search_gap_bytes;
	wd->search_threshold = search_gap_bytes + wd->search_offset + (DEFLATE_SECTOR_SIZE - (DEFLATE_MAX_MATCH + 1));

	return dict_search();
}

local bool32 dict_search_eof()
{
	wd->search_threshold = UINT16_MAX;

	return dict_search();
}

local bool32 dict_fill()
{
	int32 bytes_to_read = min(wd->in_buf_left, wd->main_read_left);

	mem_copy(dict + wd->main_read_pos, wd->in_buf_cur_ofs, bytes_to_read);

	wd->in_buf_cur_ofs += bytes_to_read;
	wd->in_buf_left -= bytes_to_read;
	wd->main_read_pos = (bytes_to_read + wd->main_read_pos) & INT16_MAX;
	wd->main_read_left -= bytes_to_read;
	wd->search_bytes_left += bytes_to_read;

	if (wd->main_read_left)
	{
		if (wd->eof_flag) mem_set(dict + wd->main_read_pos, 0x00, wd->main_read_left);

		return TRUE;
	}
	else
	{
		wd->main_read_left = 4096;

		return FALSE;
	}
}

local void deflate_main_init()
{
	ushort_set(wd->last, DEFLATE_NIL, DEFLATE_DICT_SIZE);
	ushort_set(wd->next, DEFLATE_NIL, DEFLATE_DICT_SIZE);
	ushort_set(wd->hash, DEFLATE_NIL, DEFLATE_HASH_SIZE);

	wd->flag_buf_ofs = wd->flag_buf;
	wd->flag_buf_left = 32;
	wd->token_buf_ofs = wd->token_buf;
}

local int32 deflate_main()
{
	for (ever)
	{
		if (dict_fill() && !wd->eof_flag) return DEFLATE_OK;

		if (wd->main_del_flag) delete_data(wd->main_dict_pos);

		hash_data(wd->main_dict_pos, wd->search_bytes_left);

		if (!wd->main_dict_pos) mem_copy(wd->dict + DEFLATE_DICT_SIZE, wd->dict, DEFLATE_SECTOR_SIZE + DEFLATE_MAX_MATCH);

		if (dict_search_main(wd->main_dict_pos)) break;

		wd->main_dict_pos += 4096;

		if (wd->main_dict_pos == DEFLATE_DICT_SIZE)
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
	wd->main_read_left = 4096;

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

	int32 status = deflate_main();

	wd->saved_match_len = match_len;
	wd->saved_match_pos = match_pos;
	wd->saved_bit_buf = bit_buf;
	wd->saved_bit_buf_len = bit_buf_len;
	wd->saved_bit_buf_total_flag = bit_buf_total_flag;

	wd->saved_out_buf_cur_ofs = out_buf_cur_ofs;
	wd->saved_out_buf_left = out_buf_left;

	return status;
}

void deflate_deinit(void *_wd)
{
	wd = (work_data *)_wd;

	wd->sig = DEFLATE_SIG_DONE;
}
#endif
