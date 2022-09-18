#include "color.h"

static uint8_t colorConversionLookupReverse[] = {
	0,  0,  0,  0,  0,  1,  1,  1,  1,  1,  1,  1,  1,  2,  2,  2,
	2,  2,  2,  2,  2,  3,  3,  3,  3,  3,  3,  3,  3,  4,  4,  4,
	4,  4,  4,  4,  4,  4,  5,  5,  5,  5,  5,  5,  5,  5,  6,  6,
	6,  6,  6,  6,  6,  6,  7,  7,  7,  7,  7,  7,  7,  7,  8,  8,
	8,  8,  8,  8,  8,  8,  9,  9,  9,  9,  9,  9,  9,  9,  9,  10,
	10, 10, 10, 10, 10, 10, 10, 11, 11, 11, 11, 11, 11, 11, 11, 12,
	12, 12, 12, 12, 12, 12, 12, 13, 13, 13, 13, 13, 13, 13, 13, 13,
	14, 14, 14, 14, 14, 14, 14, 14, 15, 15, 15, 15, 15, 15, 15, 15,
	16, 16, 16, 16, 16, 16, 16, 16, 17, 17, 17, 17, 17, 17, 17, 17,
	18, 18, 18, 18, 18, 18, 18, 18, 18, 19, 19, 19, 19, 19, 19, 19,
	19, 20, 20, 20, 20, 20, 20, 20, 20, 21, 21, 21, 21, 21, 21, 21,
	21, 22, 22, 22, 22, 22, 22, 22, 22, 22, 23, 23, 23, 23, 23, 23,
	23, 23, 24, 24, 24, 24, 24, 24, 24, 24, 25, 25, 25, 25, 25, 25,
	25, 25, 26, 26, 26, 26, 26, 26, 26, 26, 27, 27, 27, 27, 27, 27,
	27, 27, 27, 28, 28, 28, 28, 28, 28, 28, 28, 29, 29, 29, 29, 29,
	29, 29, 29, 30, 30, 30, 30, 30, 30, 30, 30, 31, 31, 31, 31, 31
};

COLOR ColorConvertToDS(COLOR32 c) {
	int r = colorConversionLookupReverse[c & 0xFF];
	int g = colorConversionLookupReverse[(c >> 8) & 0xFF];
	int b = colorConversionLookupReverse[(c >> 16) & 0xFF];
	return r | (g << 5) | (b << 10);
}

static uint8_t colorConversionLookup[] = {
	0,   8,   16,  25,  33,  41,  49,  58,
	66,  74,  82,  90,  99,  107, 115, 123,
	132, 140, 148, 156, 165, 173, 181, 189,
	197, 206, 214, 222, 230, 239, 247, 255
};

static uint8_t colorRound6Lookup[] = {
	0,   0,   0,   4,   4,   4,   4,   8,   8,   8,   8,   12,  12,  12,  12,  16,
	16,  16,  16,  20,  20,  20,  20,  24,  24,  24,  24,  28,  28,  28,  28,  32,
	32,  32,  32,  36,  36,  36,  36,  40,  40,  40,  40,  45,  45,  45,  45,  49,
	49,  49,  49,  53,  53,  53,  53,  57,  57,  57,  57,  61,  61,  61,  61,  65,
	65,  65,  65,  69,  69,  69,  69,  73,  73,  73,  73,  77,  77,  77,  77,  81,
	81,  81,  81,  85,  85,  85,  85,  85,  89,  89,  89,  89,  93,  93,  93,  93,
	97,  97,  97,  97,  101, 101, 101, 101, 105, 105, 105, 105, 109, 109, 109, 109,
	113, 113, 113, 113, 117, 117, 117, 117, 121, 121, 121, 121, 125, 125, 125, 125,
	130, 130, 130, 130, 134, 134, 134, 134, 138, 138, 138, 138, 142, 142, 142, 142,
	146, 146, 146, 146, 150, 150, 150, 150, 154, 154, 154, 154, 158, 158, 158, 158,
	162, 162, 162, 162, 166, 166, 166, 166, 170, 170, 170, 170, 170, 174, 174, 174,
	174, 178, 178, 178, 178, 182, 182, 182, 182, 186, 186, 186, 186, 190, 190, 190,
	190, 194, 194, 194, 194, 198, 198, 198, 198, 202, 202, 202, 202, 206, 206, 206,
	206, 210, 210, 210, 210, 215, 215, 215, 215, 219, 219, 219, 219, 223, 223, 223,
	223, 227, 227, 227, 227, 231, 231, 231, 231, 235, 235, 235, 235, 239, 239, 239,
	239, 243, 243, 243, 243, 247, 247, 247, 247, 251, 251, 251, 251, 255, 255, 255
};

COLOR32 ColorConvertFromDS(COLOR c) {
	int r = colorConversionLookup[GetR(c)];
	int g = colorConversionLookup[GetG(c)];
	int b = colorConversionLookup[GetB(c)];
	return r | (g << 8) | (b << 16);
}

COLOR32 ColorRoundToDS15(COLOR32 c) {
	int r = (c >> 0) & 0xFF;
	int g = (c >> 8) & 0xFF;
	int b = (c >> 16) & 0xFF;
	r = colorConversionLookup[colorConversionLookupReverse[r]];
	g = colorConversionLookup[colorConversionLookupReverse[g]];
	b = colorConversionLookup[colorConversionLookupReverse[b]];
	return r | (g << 8) | (b << 16);
}

COLOR32 ColorRoundToDS18(COLOR32 c) {
	int r = (c >> 0) & 0xFF;
	int g = (c >> 8) & 0xFF;
	int b = (c >> 16) & 0xFF;
	r = colorRound6Lookup[r];
	g = colorRound6Lookup[g];
	b = colorRound6Lookup[b];
	return r | (g << 8) | (b << 16);
}

COLOR ColorInterpolate(COLOR c1, COLOR c2, float amt) {
	int r = (int) (GetR(c1) * (1.0f - amt) + GetR(c2) * amt);
	int g = (int) (GetG(c1) * (1.0f - amt) + GetG(c2) * amt);
	int b = (int) (GetB(c1) * (1.0f - amt) + GetB(c2) * amt);
	return r | (g << 5) | (b << 10);
}
