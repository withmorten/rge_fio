#pragma once

#include "main.h"

extern bool32 rge_write_error;

handle rge_fake_open_read(handle file_handle, int32 fake_size);

#define rge_open_read_(filename) rge_open_read(filename, _O_BINARY) // easy open_read
handle rge_open_read(char *filename, int32 flag);

#define rge_open_write_(filename) rge_open_write(filename, _O_WRONLY | _O_APPEND | _O_CREAT | _O_TRUNC | _O_BINARY, _S_IREAD | _S_IWRITE) // easy open_write
handle rge_open_write(char *filename, int32 flag, int32 pmode);

handle rge_fake_close(handle handle);
int32 rge_close(handle handle);

void rge_fast_forward(handle handle, int32 size);

void rge_read_uncompressed(handle handle, void *data, int32 size);
void rge_write_uncompressed(handle handle, void *data, int32 size);

void rge_read_full(handle handle, void **data, int32 *size);

void rge_read(handle handle, void *data, int32 size);
void rge_write(handle handle, void *data, int32 size);
