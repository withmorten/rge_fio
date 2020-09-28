#pragma once

// this file must be included AFTER any standard headers

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/stat.h>

#ifndef _WIN32
#include <stddef.h>
#include <unistd.h>

#define _O_BINARY 0

#define _O_WRONLY O_WRONLY
#define _O_APPEND O_APPEND
#define _O_CREAT O_CREAT
#define _O_TRUNC O_TRUNC
#define _S_IREAD S_IREAD
#define _S_IWRITE S_IWRITE

#define _open open
#define _close close
#define _lseek lseek
#define _read read
#define _write write
#define _lseek lseek
#define _tell(fd) lseek(fd, 0, SEEK_CUR)
#endif

typedef int8_t int8;
typedef uint8_t uint8;
typedef int16_t int16;
typedef uint16_t uint16;
typedef int32_t int32;
typedef uint32_t uint32;
typedef int64_t int64;
typedef uint64_t uint64;

typedef uint8 byte;

typedef uint8 bool8;
typedef int32 bool32;

typedef uintptr_t uintptr;
typedef ptrdiff_t ptrdiff;

typedef int32 handle;
typedef int32 res_id;

#ifndef FALSE
#define FALSE 0
#endif

#ifndef TRUE
#define TRUE 1
#endif

#define INVALID_HANDLE -1

#define local static

#define rge_fopen fopen
#define rge_fclose(stream) { if (stream) { fclose(stream); } stream = NULL; }

#define rge_free(pblock) { free(pblock); pblock = NULL; }

#define ever ;;

#define ZEROSTR { '\0' }
#define ZEROMEM { 0x00 }
#define EMPTYSTR ""

#define MAX_CHAR 255

#ifndef MAX_PATH
#define MAX_PATH 260
#endif

#define strzero(str) memset(str, '\0', sizeof(str)) // only works on char arrays
#define memzero(mem, size) memset(mem, 0x00, size)
