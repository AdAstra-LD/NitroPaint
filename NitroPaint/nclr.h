#pragma once
#include <Windows.h>
#include "color.h"
#include "filecommon.h"

#define NCLR_TYPE_INVALID 0
#define NCLR_TYPE_NCLR 1
#define NCLR_TYPE_HUDSON 2
#define NCLR_TYPE_BIN 3
#define NCLR_TYPE_NTFP 4

extern LPCWSTR paletteFormatNames[];

typedef struct NCLR_ {
	OBJECT_HEADER header;
	int nBits;
	int nColors;
	int nPalettes; //for compressed palettes
	int totalSize; //for extended palettes
	int extPalette;
	short *idxTable;
	COLOR *colors;
} NCLR;

int nclrIsValidHudson(LPBYTE lpFile, int size);

int nclrIsValidBin(LPBYTE lpFile, int size);

int nclrIsValidNtfp(LPBYTE lpFile, int size);

int nclrRead(NCLR * nclr, char * buffer, int size);

void nclrWrite(NCLR * nclr, LPWSTR name);

void nclrCreate(DWORD * palette, int nColors, int nBits, int extended, LPWSTR name, int fmt);

int nclrReadFile(NCLR *nclr, LPWSTR path);