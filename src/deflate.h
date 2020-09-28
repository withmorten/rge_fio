#pragma once

#include "main.h"

#include "defs.h"

#define DEFLATE_MIN_MATCH 3
#define DEFLATE_THRESHOLD (DEFLATE_MIN_MATCH - 1)
#define DEFLATE_MAX_MATCH 258

#define DEFLATE_DICT_BITS 15
#define DEFLATE_HASH_BITS 13
#define DEFLATE_SHIFT_BITS ((DEFLATE_HASH_BITS + (DEFLATE_MIN_MATCH - 1)) / DEFLATE_MIN_MATCH)
#define DEFLATE_SECTOR_BITS 12

#define DEFLATE_DICT_SIZE (1 << DEFLATE_DICT_BITS)
#define DEFLATE_HASH_SIZE (1 << DEFLATE_HASH_BITS)
#define DEFLATE_SECTOR_SIZE (1 << DEFLATE_SECTOR_BITS)

#define DEFLATE_HASH_FLAG_1 0x8000
#define DEFLATE_HASH_FLAG_2 0x7FFF

#define DEFLATE_NEXT_MASK 0x7FFF

#define DEFLATE_MAX_TOKENS 12288

#define DEFLATE_NUM_SYMBOLS_1 288
#define DEFLATE_NUM_SYMBOLS_2 32
#define DEFLATE_NUM_SYMBOLS_3 19
#define DEFLATE_MAX_SYMBOLS 288

#define DEFLATE_NIL 0xFFFFui16

typedef struct work_data work_data;

struct work_data
{
	byte dict[DEFLATE_DICT_SIZE + DEFLATE_SECTOR_SIZE + DEFLATE_MAX_MATCH];
	uint16 hash[DEFLATE_HASH_SIZE];
	uint16 next[DEFLATE_DICT_SIZE];
	uint16 last[DEFLATE_DICT_SIZE];
	byte token_buf[DEFLATE_MAX_TOKENS * 3];
	byte *token_buf_ofs;
	int32 token_buf_len;
	uint32 flag_buf[DEFLATE_MAX_TOKENS >> 5];
	uint32 *flag_buf_ofs;
	int32 flag_buf_left;
	uint32 token_buf_start;
	uint32 token_buf_end;
	int32 token_buf_bytes;
	int32 freq_1[DEFLATE_NUM_SYMBOLS_1];
	int32 freq_2[DEFLATE_NUM_SYMBOLS_2];
	int32 freq_3[DEFLATE_NUM_SYMBOLS_3];
	int32 size_1[DEFLATE_NUM_SYMBOLS_1];
	int32 size_2[DEFLATE_NUM_SYMBOLS_2];
	int32 size_3[DEFLATE_NUM_SYMBOLS_3];
	uint32 code_1[DEFLATE_NUM_SYMBOLS_1];
	uint32 code_2[DEFLATE_NUM_SYMBOLS_2];
	uint32 code_3[DEFLATE_NUM_SYMBOLS_3];
	int32 bundled_sizes[DEFLATE_NUM_SYMBOLS_1 + DEFLATE_NUM_SYMBOLS_2];
	int32 coded_sizes[DEFLATE_NUM_SYMBOLS_1 + DEFLATE_NUM_SYMBOLS_2];
	int32 *coded_sizes_end;
	int32 used_lit_codes;
	int32 used_dist_codes;
	uint32 saved_bit_buf;
	int32 saved_bit_buf_len;
	int32 saved_bit_buf_total_flag;
	uint32 bit_buf_total;
	uint32 search_offset;
	int32 search_bytes_left;
	uint32 search_threshold;
	int32 saved_match_len;
	int32 saved_match_pos;
	int32 max_compares;
	int32 strategy;
	bool32 greedy_flag;
	bool32 eof_flag;
	bool32 main_del_flag;
	int32 main_dict_pos;
	int32 main_read_pos;
	int32 main_read_left;
	byte *in_buf_cur_ofs;
	int32 in_buf_left;
	byte *in_buf_ofs;
	int32 in_buf_size;
	byte *out_buf_ofs;
	int32 out_buf_size;
	int32 (*flush_out_buf)(byte *, int32);
	byte *saved_out_buf_cur_ofs;
	int32 saved_out_buf_left;
	uint32 sig;
};
