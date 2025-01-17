#include "palette.h"
#include "analysis.h"
#include <math.h>

int lightnessCompare(const void *d1, const void *d2) {
	COLOR32 c1 = *(COLOR32 *) d1;
	COLOR32 c2 = *(COLOR32 *) d2;
	if (c1 == c2) return 0;
	
	//by properties of linear transformations, this is valid
	int dr = (c1 & 0xFF) - (c2 & 0xFF);
	int dg = ((c1 >> 8) & 0xFF) - ((c2 >> 8) & 0xFF);
	int db = ((c1 >> 16) & 0xFF) - ((c2 >> 16) & 0xFF);
	int dy = dr * 299 + dg * 587 + db * 114;

	return dy;
}

void createPaletteExact(COLOR32 *img, int width, int height, COLOR32 *pal, unsigned int nColors) {
	createPaletteSlow(img, width, height, pal, nColors);
}

void createPalette_(COLOR32 *img, int width, int height, COLOR32 *pal, int nColors) {
	createPaletteExact(img, width, height, pal + 1, nColors - 1);
	*pal = 0xFF00FF;
}

int closestPalette(COLOR32 rgb, COLOR32 *palette, int paletteSize) {
	int smallestDistance = 1 << 24;
	int index = 0, i = 0;
	int ey, eu, ev;

	//test exact matches
	for (i = 0; i < paletteSize; i++) {
		if ((rgb & 0xFFFFFF) == (palette[i] & 0xFFFFFF)) {
			return i;
		}
	}

	//else
	for (i = 0; i < paletteSize; i++) {
		COLOR32 entry = palette[i];
		int dr = ((entry >>  0) & 0xFF) - ((rgb >>  0) & 0xFF);
		int dg = ((entry >>  8) & 0xFF) - ((rgb >>  8) & 0xFF);
		int db = ((entry >> 16) & 0xFF) - ((rgb >> 16) & 0xFF);

		convertRGBToYUV(dr, dg, db, &ey, &eu, &ev);
		int dst = 4 * ey * ey + eu * eu + ev * ev;

		if (dst < smallestDistance) {
			index = i;
			smallestDistance = dst;
		}
	}

	return index;
}

int m(int a) {
	return a < 0? 0: (a > 255? 255: a);
}

#define diffuse(a,r,g,b,ap) a=m((int)((a&0xFF)+(r)))|(m((int)(((a>>8)&0xFF)+(g)))<<8)|(m((int)(((a>>16)&0xFF)+(b)))<<16)|(m((int)(((a>>24)&0xFF)+(ap)))<<24)

void doDiffuse(int i, int width, int height, unsigned int * pixels, int errorRed, int errorGreen, int errorBlue, int errorAlpha, float amt) {
	//if ((pixels[i] >> 24) < 127) return;
	if (i % width < width - 1) {
		unsigned int right = pixels[i + 1];
		diffuse(right, errorRed * 7 * amt / 16, errorGreen * 7 * amt / 16, errorBlue * 7 * amt / 16, errorAlpha * 7 * amt / 16);
		pixels[i + 1] = right;
	}
	if (i / width < height - 1) {
		if (i % width > 0) {//downleft
			unsigned int right = pixels[i + width - 1];
			diffuse(right, errorRed * 3 * amt / 16, errorGreen * 3 * amt / 16, errorBlue * 3 * amt / 16, errorAlpha * 3 * amt / 16);
			pixels[i + width - 1] = right;
		}
		if (1) {//down
			unsigned int right = pixels[i + width];
			diffuse(right, errorRed * 5 * amt / 16, errorGreen * 5 * amt / 16, errorBlue * 5 * amt / 16, errorAlpha * 5 * amt / 16);
			pixels[i + width] = right;
		}
		if (i % width < width - 1) {
			unsigned int right = pixels[i + width + 1];
			diffuse(right, errorRed * 1 * amt / 16, errorGreen * 1 * amt / 16, errorBlue * 1 * amt / 16, errorAlpha * 1 * amt / 16);
			pixels[i + width + 1] = right;
		}
	}
}

COLOR32 averageColor(COLOR32 *cols, int nColors) {
	int tr = 0, tg = 0, tb = 0, ta = 0;

	for (int i = 0; i < nColors; i++) {
		COLOR32 c = cols[i];
		int a = c >> 24;
		if (a == 0) continue;
		ta += a;

		tr += (c & 0xFF) * a;
		tg += ((c >> 8) & 0xFF) * a;
		tb += ((c >> 16) & 0xFF) * a;
	}

	if (ta == 0) return 0;
	tr = tr / ta;
	tg = tg / ta;
	tb = tb / ta;
	//maybe unnecessary
	if (tr > 255) tr = 255;
	if (tg > 255) tg = 255;
	if (tb > 255) tb = 255;

	return tr | (tg << 8) | (tb << 16) | 0xFF000000;
}

unsigned int getPaletteError(COLOR32 *px, int nPx, COLOR32 *pal, int paletteSize) {
	unsigned int error = 0;
	int ey, eu, ev;

	for (int i = 0; i < nPx; i++) {
		COLOR32 thisColor = px[i];
		if ((thisColor >> 24) == 0) continue;

		COLOR32 closest = pal[closestPalette(px[i], pal + 1, paletteSize - 1) + 1];
		int er = (closest & 0xFF) - (thisColor & 0xFF);
		int eg = ((closest >> 8) & 0xFF) - ((thisColor >> 8) & 0xFF);
		int eb = ((closest >> 16) & 0xFF) - ((thisColor >> 16) & 0xFF);

		convertRGBToYUV(er, eg, eb, &ey, &eu, &ev);
		error += 4 * ey * ey + eu * eu + ev * ev;
	}
	return error;
}

void convertRGBToYUV(int r, int g, int b, int *y, int *u, int *v) {
	*y = (int) ( 0.2990 * r + 0.5870 * g + 0.1140 * b);
	*u = (int) (-0.1684 * r - 0.3316 * g + 0.5000 * b);
	*v = (int) ( 0.5000 * r - 0.4187 * g - 0.0813 * b);
}

void convertYUVToRGB(int y, int u, int v, int *r, int *g, int *b) {
	*r = (int) (y - 0.00004f * u + 1.40199f * v);
	*g = (int) (y - 0.34408f * u - 0.71389f * v);
	*b = (int) (y + 1.77180f * u - 0.00126f * v);
}

int pixelCompare(const void *p1, const void *p2) {
	return *(COLOR32 *) p1 - (*(COLOR32 *) p2);
}

int countColors(COLOR32 *px, int nPx) {
	//sort the colors by raw RGB value. This way, same colors are grouped.
	COLOR32 *copy = (COLOR32 *) malloc(nPx * 4);
	memcpy(copy, px, nPx * 4);
	qsort(copy, nPx, 4, pixelCompare);
	int nColors = 0;
	int hasTransparent = 0;
	for (int i = 0; i < nPx; i++) {
		int a = copy[i] >> 24;
		if (!a) hasTransparent = 1;
		else {
			COLOR32 color = copy[i] & 0xFFFFFF;
			//has this color come before?
			int repeat = 0;
			if(i){
				COLOR32 comp = copy[i - 1] & 0xFFFFFF;
				if (comp == color) {
					repeat = 1;
				}
			}
			if (!repeat) {
				nColors++;
			}
		}
	}
	free(copy);
	return nColors + hasTransparent;
}

unsigned long long computePaletteError(COLOR32 *px, int nPx, COLOR32 *pal, int nColors, int alphaThreshold, unsigned long long nMaxError) {
	if (nMaxError == 0) nMaxError = 0xFFFFFFFFFFFFFFFFull;
	unsigned long long error = 0;

	for (int i = 0; i < nPx; i++) {
		COLOR32 p = px[i];
		int a = (p >> 24) & 0xFF;
		if (a < alphaThreshold) continue;
		int best = closestPalette(px[i], pal, nColors);
		COLOR32 chosen = pal[best];
		int dr = (chosen & 0xFF) - (p & 0xFF);
		int dg = ((chosen >> 8) & 0xFF) - ((p >> 8) & 0xFF);
		int db = ((chosen >> 16) & 0xFF) - ((p >> 16) & 0xFF);
		int dy, du, dv;
		convertRGBToYUV(dr, dg, db, &dy, &du, &dv);
		error += 4 * dy * dy;
		if (error >= nMaxError) return nMaxError;
		error += du * du + dv * dv;
		if (error >= nMaxError) return nMaxError;
	}
	return error;
}
