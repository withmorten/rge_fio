#pragma once

#include "main.h"

#include "defs.h"

typedef struct work_data work_data;

struct work_data
{
	byte dict[37122];
	uint16 hash[8192];
	uint16 next[32768];
	uint16 last[32768];
	byte token_buf[36864];
	byte *token_buf_ofs;
	int32 token_buf_len;
	uint32 flag_buf[384];
	uint32 *flag_buf_ofs;
	int32 flag_buf_left;
	uint32 token_buf_start;
	uint32 token_buf_end;
	int32 token_buf_bytes;
	int32 freq_1[288];
	int32 freq_2[32];
	int32 freq_3[19];
	int32 size_1[288];
	int32 size_2[32];
	int32 size_3[19];
	uint32 code_1[288];
	uint32 code_2[32];
	uint32 code_3[19];
	int32 bundled_sizes[320];
	int32 coded_sizes[320];
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
