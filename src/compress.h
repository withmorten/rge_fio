#pragma once

#include "main.h"

#define INFLATE_OK 0
#define INFLATE_EOF -1
#define INFLATE_ERROR -2

size_t Inf32BufSize();
int32 Inf32Decode(byte *in_buf, size_t in_buf_ofs, size_t *in_buf_size, byte *out_buf, size_t out_buf_offset, size_t *out_buf_size, void *_wd, bool32 b);

#define DEFLATE_STRATEGY_STATIC 0
#define DEFLATE_STRATEGY_DYNAMIC 1
#define DEFLATE_STRATEGY_DEFAULT 2

#define DEFLATE_MAX_COMPARES_DEFAULT 75

#define DEFLATE_INIT 0
#define DEFLATE_OK 1
#define DEFLATE_ERROR 2

size_t deflate_buf_size();
int32 deflate_init(void *_wd, int32 max_compares, int32 strategy, bool32 greedy_flag, byte *out_buf_ofs, int32 out_buf_size, int32 (*out_buf_flush)(byte *, int32));
int32 deflate_data(void *_wd, byte *in_buf_ofs, int32 in_buf_size, bool32 eof_flag);
void deflate_deinit(void *_wd);
