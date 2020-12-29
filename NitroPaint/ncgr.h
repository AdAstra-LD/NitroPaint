#pragma once

#include "nclr.h"

#define NCGR_TYPE_INVALID	0
#define NCGR_TYPE_NCGR		1
#define NCGR_TYPE_HUDSON	2
#define NCGR_TYPE_HUDSON2	3

typedef struct NCGR_{
	int nTiles;
	int tilesX;
	int tilesY;
	int mapping;
	int nBits;
	int tileWidth;
	BYTE **tiles;
	int compress;
	int type;
} NCGR;

int ncgrIsValidHudson(LPBYTE buffer, int size);

int ncgrGetTile(NCGR * ncgr, NCLR * nclr, int x, int y, DWORD * out, int previewPalette, BOOL drawChecker);

int ncgrReadFile(NCGR *ncgr, LPWSTR path);

DWORD getColor(WORD col);

void ncgrWrite(NCGR * ncgr, LPWSTR name);

void ncgrCreate(DWORD * blocks, int nBlocks, int nBits, LPWSTR name, int bin);