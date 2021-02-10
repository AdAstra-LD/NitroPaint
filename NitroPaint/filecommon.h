#pragma once

#define FILE_TYPE_PALETTE    1
#define FILE_TYPE_CHARACTER  2
#define FILE_TYPE_SCREEN     3
#define FILE_TYPE_CELL       4
#define FILE_TYPE_NSBTX      5

#define COMPRESSION_NONE     0
#define COMPRESSION_LZ77     1

typedef struct OBJECT_HEADER_ {
	int size;
	int type;
	int format;
	int compression;
} OBJECT_HEADER;