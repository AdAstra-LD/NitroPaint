#include "textureeditor.h"
#include "nsbtxviewer.h"
#include "childwindow.h"
#include "nitropaint.h"
#include "nclrviewer.h"
#include "palette.h"
#include "colorchooser.h"
#include "resource.h"
#include "tiler.h"
#include "gdip.h"
#include "texconv.h"
#include "nclr.h"

#include <Shlwapi.h>
#include <ShlObj.h>
#include <commctrl.h>
#include <math.h>

extern HICON g_appIcon;

HWND CreateTexturePaletteEditor(int x, int y, int width, int height, HWND hWndParent, TEXTUREEDITORDATA *data);

void exportTextureImage(LPCWSTR path, TEXTURE *texture) {
	//export as PNG
	//if texture is palette4, palette16 or palette256, output indexed image with appropriate color 0
	//if texture is direct or 4x4, output non-indexed
	//if texture is a3i5 or a5i3, use a 256-color palette that repeats the palette at varying alphas
	int texImageParam = texture->texels.texImageParam;
	int format = FORMAT(texImageParam);
	int width = TEXW(texImageParam);
	int height = TEXH(texImageParam);

	//buffer to hold converted palette
	int paletteSize = 0;
	COLOR32 palette[256] = { 0 };
	if (format != CT_DIRECT) {
		//convert to 24-bit
		if (format != CT_4x4) {
			paletteSize = texture->palette.nColors;
			if (paletteSize > 256) paletteSize = 256;
			for (int i = 0; i < paletteSize; i++) {
				palette[i] = ColorConvertFromDS(texture->palette.pal[i]);
			}
		}

		//for a3i5 and a5i3, build up varying levels of alpha
		if (format == CT_A3I5 || format == CT_A5I3) {
			int nAlphaLevels = format == CT_A3I5 ? 8 : 32;
			int nColorsPerAlpha = 256 / nAlphaLevels;

			for (int i = 1; i < nAlphaLevels; i++) {
				int alpha = (i * 510 + nAlphaLevels - 1) / (2 * nAlphaLevels - 2); //rounding to nearest
				for (int j = 0; j < paletteSize; j++) {
					COLOR32 c = palette[j];
					c |= alpha << 24;
					palette[j + i * nColorsPerAlpha] = c;
				}
			}
			paletteSize = 256;
		} else if (format == CT_4COLOR || format == CT_16COLOR || format == CT_256COLOR) {
			//make palette opaque except first color if c0xp
			int c0xp = COL0TRANS(texImageParam);
			for (int i = 0; i < paletteSize; i++) {
				if (i || !c0xp) palette[i] |= 0xFF000000;
			}
		} else {
			//else we can't export an indexed image unfortunately (4x4 and direct)
			paletteSize = 0;
		}
	}

	if (format == CT_256COLOR || format == CT_A3I5 || format == CT_A5I3) {
		//prepare image output. For palette256, a3i5 and a5i3, we can export the data as it already is.
		imageWriteIndexed((unsigned char *) texture->texels.texel, width, height, palette, paletteSize, path);
	} else if (format == CT_4x4 || format == CT_DIRECT) {
		//else if 4x4 or direct, just export full-color image. Red/blue must be swapped here
		COLOR32 *px = (COLOR32 *) calloc(width * height, sizeof(COLOR32));
		textureRender(px, &texture->texels, &texture->palette, 0);
		for (int i = 0; i < width * height; i++) {
			COLOR32 c = px[i];
			px[i] = REVERSE(c);
		}
		imageWrite(px, width, height, path);
		free(px);
	} else {
		//palette16 or palette4, will need to convert the bit depth
		unsigned char *bits = (unsigned char *) calloc(width * height, 1);
		int depth = format == CT_4COLOR ? 2 : 4;
		int mask = depth == 2 ? 0x3 : 0xF;

		for (int y = 0; y < height; y++) {
			unsigned char *rowSrc = texture->texels.texel + y * width * depth / 8;
			unsigned char *rowDst = bits + y * width;
			for (int x = 0; x < width; x++) {
				rowDst[x] = (rowSrc[x * depth / 8] >> ((x * depth) % 8)) & mask;
			}
		}
		imageWriteIndexed(bits, width, height, palette, paletteSize, path);
		free(bits);
	}
}

void UpdatePaletteLabel(HWND hWnd) {
	TEXTUREEDITORDATA *data = (TEXTUREEDITORDATA *) GetWindowLongPtr(hWnd, 0);

	WCHAR bf[32];
	int len;
	if (data->textureData.palette.nColors) {
		len = wsprintfW(bf, L"Palette: %d colors", data->textureData.palette.nColors);
		data->hasPalette = TRUE;
	} else {
		len = wsprintfW(bf, L"No palette");
		data->hasPalette = FALSE;
	}
	SendMessage(data->hWndPaletteLabel, WM_SETTEXT, len, (LPARAM) bf);

	len = wsprintfW(bf, L"Format: %S", stringFromFormat(FORMAT(data->textureData.texels.texImageParam)));
	SendMessage(data->hWndFormatLabel, WM_SETTEXT, len, (LPARAM) bf);

	int nColors = countColors(data->px, data->width * data->height);
	len = wsprintfW(bf, L"Colors: %d", nColors);
	SendMessage(data->hWndUniqueColors, WM_SETTEXT, len, (LPARAM) bf);

	int texelVram = getTextureVramSize(&data->textureData.texels);
	int paletteVram = getPaletteVramSize(&data->textureData.palette);
	
	//this code is ugly due to being unable to just use %.2f
	len = wsprintfW(bf, L"Texel: %d.%d%dKB", texelVram / 1024, (texelVram * 10 / 1024) % 10,
		((texelVram * 100 + 512) / 1024) % 10);
	SendMessage(data->hWndTexelVram, WM_SETTEXT, len, (LPARAM) bf);
	len = wsprintfW(bf, L"Palette: %d.%d%dKB", paletteVram / 1024, (paletteVram * 10 / 1024) % 10,
		((paletteVram * 100 + 512) / 1024) % 10);
	SendMessage(data->hWndPaletteVram, WM_SETTEXT, len, (LPARAM) bf);
}

LRESULT CALLBACK TextureEditorWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	TEXTUREEDITORDATA *data = (TEXTUREEDITORDATA *) GetWindowLongPtr(hWnd, 0);
	if (data == NULL) {
		data = (TEXTUREEDITORDATA *) calloc(1, sizeof(TEXTUREEDITORDATA));
		SetWindowLongPtr(hWnd, 0, (LONG_PTR) data);
	}
	switch (msg) {
		case WM_CREATE:
		{
			/*
			+----------------------+  Format: none
			|                      |  [Convert to...]
			|                      |  
			| Texture Preview      |  Palette: <x> colors
			|                      |  [Edit Palette]
			|                      |
			+----------------------+
			*/
			data->scale = 1;
			data->hoverX = -1;
			data->hoverY = -1;
			data->hoverIndex = -1;
			data->hWnd = hWnd;
			data->hWndPreview = CreateWindow(L"TexturePreviewClass", L"Texture Preview", WS_VISIBLE | WS_CHILD | WS_HSCROLL | WS_VSCROLL, 0, 0, 300, 300, hWnd, NULL, NULL, NULL);
			data->hWndFormatLabel = CreateWindow(L"STATIC", L"Format: none", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, 310, 10, 100, 22, hWnd, NULL, NULL, NULL);
			data->hWndConvert = CreateWindow(L"BUTTON", L"Convert To...", WS_VISIBLE | WS_CHILD, 310, 37, 100, 22, hWnd, NULL, NULL, NULL);
			data->hWndPaletteLabel = CreateWindow(L"STATIC", L"No palette", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, 310, 69, 100, 22, hWnd, NULL, NULL, NULL);
			data->hWndEditPalette = CreateWindow(L"BUTTON", L"Edit Palette", WS_VISIBLE | WS_CHILD, 310, 123, 100, 22, hWnd, NULL, NULL, NULL);
			data->hWndExportNTF = CreateWindow(L"BUTTON", L"Export NTF", WS_VISIBLE | WS_CHILD, 310, 150, 100, 22, hWnd, NULL, NULL, NULL);
			data->hWndUniqueColors = CreateWindow(L"STATIC", L"Colors: 0", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, 310, 155, 100, 22, hWnd, NULL, NULL, NULL);

			data->hWndTexelVram = CreateWindow(L"STATIC", L"Texel: 0KB", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, 310, 182, 100, 22, hWnd, NULL, NULL, NULL);
			data->hWndPaletteVram = CreateWindow(L"STATIC", L"Palette: 0KB", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, 310, 209, 110, 22, hWnd, NULL, NULL, NULL);
			break;
		}
		case WM_PAINT:
		{
			InvalidateRect(data->hWndPreview, NULL, FALSE);
			break;
		}
		case WM_SIZE:
		{
			RECT rcClient;
			GetClientRect(hWnd, &rcClient);
			MoveWindow(data->hWndPreview, 0, 0, rcClient.right - 120, rcClient.bottom, TRUE);
			MoveWindow(data->hWndFormatLabel, rcClient.right - 110, 10, 100, 22, TRUE);
			MoveWindow(data->hWndConvert, rcClient.right - 110, 37, 100, 22, TRUE);
			MoveWindow(data->hWndPaletteLabel, rcClient.right - 110, 69, 100, 22, TRUE);
			MoveWindow(data->hWndEditPalette, rcClient.right - 110, 96, 100, 22, TRUE);
			MoveWindow(data->hWndExportNTF, rcClient.right - 110, 123, 100, 22, TRUE);
			MoveWindow(data->hWndUniqueColors, rcClient.right - 110, 155, 100, 22, TRUE);
			MoveWindow(data->hWndTexelVram, rcClient.right - 110, 182, 100, 22, TRUE);
			MoveWindow(data->hWndPaletteVram, rcClient.right - 110, 209, 110, 22, TRUE);
			return DefMDIChildProc(hWnd, msg, wParam, lParam);
		}
		case NV_INITIALIZE:
		{
			data->width = wParam & 0xFFFF;
			data->height = (wParam >> 16) & 0xFFFF;
			data->px = (DWORD *) lParam;
			data->format = 0;
			data->hasPalette = FALSE;
			data->frameData.contentWidth = data->width;
			data->frameData.contentHeight = data->height;

			//check: is it a Nitro TGA?
			if (!nitroTgaRead(data->szInitialFile, &data->textureData.texels, &data->textureData.palette)) {
				memcpy(data->szCurrentFile, data->szInitialFile, 2 + 2 * wcslen(data->szInitialFile));
				data->format = FORMAT(data->textureData.texels.texImageParam);
				data->hasPalette = (data->format != CT_DIRECT && data->format != 0);
				data->isNitro = 1;
				textureRender(data->px, &data->textureData.texels, &data->textureData.palette, 0);
				for (int i = 0; i < data->width * data->height; i++) {
					DWORD col = data->px[i];
					data->px[i] = REVERSE(col);
				}
				UpdatePaletteLabel(hWnd);
			}

			WCHAR buffer[16];
			int nColors = countColors(data->px, data->width * data->height);
			int len = wsprintfW(buffer, L"Colors: %d", nColors);
			SendMessage(data->hWndUniqueColors, WM_SETTEXT, len, (LPARAM) buffer);

			SendMessage(data->hWndPreview, NV_RECALCULATE, 0, 0);
			RedrawWindow(data->hWndPreview, NULL, NULL, RDW_FRAME | RDW_INVALIDATE);
			break;
		}
		case NV_INITIALIZE_IMMEDIATE:
		{
			//set texture data directly
			TEXTURE *texture = (TEXTURE *) lParam;
			memcpy(&data->textureData, texture, sizeof(TEXTURE));
			data->isNitro = 1;
			data->hasPalette = FORMAT(texture->texels.texImageParam) != CT_DIRECT;
			data->format = FORMAT(texture->texels.texImageParam);
			data->width = TEXW(texture->texels.texImageParam);
			data->height = TEXH(texture->texels.texImageParam);
			data->frameData.contentWidth = data->width;
			data->frameData.contentHeight = data->height;
			data->px = (COLOR32 *) calloc(data->width * data->height, sizeof(COLOR32));

			//decode texture data for preview
			int nPx = data->width * data->height;
			textureRender(data->px, &texture->texels, &texture->palette, 0);
			for (int i = 0; i < nPx; i++) {
				COLOR32 p = data->px[i];
				data->px[i] = REVERSE(p);
			}

			//update UI
			WCHAR buffer[16];
			int nColors = countColors(data->px, data->width * data->height);
			int len = wsprintfW(buffer, L"Colors: %d", nColors);
			SendMessage(data->hWndUniqueColors, WM_SETTEXT, len, (LPARAM) buffer);

			SendMessage(data->hWndPreview, NV_RECALCULATE, 0, 0);
			RedrawWindow(data->hWndPreview, NULL, NULL, RDW_FRAME | RDW_INVALIDATE);
			UpdatePaletteLabel(hWnd);
			break;
		}
		case NV_SETPATH:
		{
			memcpy(data->szInitialFile, (LPWSTR) lParam, 2 * wParam + 2);
			break;
		}
		case WM_MDIACTIVATE:
		{
			HWND hWndMain = getMainWindow(hWnd);
			if ((HWND) lParam == hWnd) {
				if (data->showBorders)
					CheckMenuItem(GetMenu(hWndMain), ID_VIEW_GRIDLINES, MF_CHECKED);
				else
					CheckMenuItem(GetMenu(hWndMain), ID_VIEW_GRIDLINES, MF_UNCHECKED);
				int checkBox = ID_ZOOM_100;
				if (data->scale == 2) {
					checkBox = ID_ZOOM_200;
				} else if (data->scale == 4) {
					checkBox = ID_ZOOM_400;
				} else if (data->scale == 8) {
					checkBox = ID_ZOOM_800;
				}
				int ids[] = {ID_ZOOM_100, ID_ZOOM_200, ID_ZOOM_400, ID_ZOOM_800};
				for (int i = 0; i < sizeof(ids) / sizeof(*ids); i++) {
					int id = ids[i];
					CheckMenuItem(GetMenu(hWndMain), id, (id == checkBox) ? MF_CHECKED : MF_UNCHECKED);
				}
			}
			break;
		}
		case WM_COMMAND:
		{
			if (lParam) {
				HWND hWndControl = (HWND) lParam;
				if (hWndControl == data->hWndEditPalette) {
					int format = FORMAT(data->textureData.texels.texImageParam);
					if (format == CT_DIRECT || format == 0) {
						MessageBox(hWnd, L"No palette for this texture.", L"No palette", MB_ICONERROR);
					} else {
						HWND hWndMdi = (HWND) GetWindowLong(hWnd, GWL_HWNDPARENT);
						if (data->hWndPaletteEditor == NULL) {
							data->hWndPaletteEditor = CreateTexturePaletteEditor(CW_USEDEFAULT, CW_USEDEFAULT, 300, 300, hWndMdi, data);
						} else {
							SendMessage(hWndMdi, WM_MDIACTIVATE, (WPARAM) data->hWndPaletteEditor, 0);
						}
					}
				} else if (hWndControl == data->hWndConvert) {
					HWND hWndMain = (HWND) GetWindowLong((HWND) GetWindowLong(hWnd, GWL_HWNDPARENT), GWL_HWNDPARENT);
					data->hWndConvertDialog = CreateWindow(L"ConvertDialogClass", L"Convert Texture",
														   WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX & ~WS_MINIMIZEBOX & ~WS_THICKFRAME, 
														   CW_USEDEFAULT, CW_USEDEFAULT, 500, 500, hWndMain, NULL, NULL, NULL);
					SetWindowLongPtr(data->hWndConvertDialog, 0, (LONG_PTR) data);
					ShowWindow(data->hWndConvertDialog, SW_SHOW);
					SetWindowLong(hWndMain, GWL_STYLE, GetWindowLong(hWndMain, GWL_STYLE) | WS_DISABLED);
					SendMessage(data->hWndConvertDialog, NV_INITIALIZE, 0, 0);
				} else if (hWndControl == data->hWndExportNTF) {
					HWND hWndMain = (HWND) GetWindowLong((HWND) GetWindowLong(hWnd, GWL_HWNDPARENT), GWL_HWNDPARENT);
					//if not in any format, it cannot be exported.
					if (!data->isNitro) {
						MessageBox(hWnd, L"Texture must be converted.", L"Not converted", MB_ICONERROR);
						break;
					}

					LPWSTR ntftPath = saveFileDialog(hWndMain, L"Save NTFT", L"NTFT Files (*.ntft)\0*.ntft\0All Files\0*.*\0\0", L"ntft");
					if (ntftPath == NULL) break;

					LPWSTR ntfiPath = NULL;
					if (FORMAT(data->textureData.texels.texImageParam) == CT_4x4) {
						ntfiPath = saveFileDialog(hWndMain, L"Save NTFI", L"NTFI Files (*.ntfi)\0*.ntfi\0All Files\0*.*\0\0", L"ntfi");
						if (ntfiPath == NULL) {
							free(ntftPath);
							break;
						}
					}

					DWORD dwWritten;
					int texImageParam = data->textureData.texels.texImageParam;
					int texelSize = getTexelSize(TEXW(texImageParam), TEXH(texImageParam), texImageParam);
					HANDLE hFile = CreateFile(ntftPath, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
					WriteFile(hFile, data->textureData.texels.texel, texelSize, &dwWritten, NULL);
					CloseHandle(hFile);
					free(ntftPath);

					if (ntfiPath != NULL) {
						hFile = CreateFile(ntfiPath, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
						WriteFile(hFile, data->textureData.texels.cmp, texelSize / 2, &dwWritten, NULL);
						CloseHandle(hFile);
						free(ntfiPath);
					}

					//palette export
					if (data->textureData.palette.pal != NULL) {
						COLOR *colors = data->textureData.palette.pal;
						int nColors = data->textureData.palette.nColors;

						HWND hWndMain = (HWND) GetWindowLong((HWND) GetWindowLong(hWnd, GWL_HWNDPARENT), GWL_HWNDPARENT);
						LPWSTR ntfpPath = saveFileDialog(hWndMain, L"Save NTFP", L"NTFP files (*.ntfp)\0*.ntfp\0All Files\0*.*\0\0", L"ntfp");
						if (ntfpPath == NULL) break;

						DWORD dwWritten;
						HANDLE hFile = CreateFile(ntfpPath, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
						WriteFile(hFile, colors, nColors * 2, &dwWritten, NULL);
						CloseHandle(hFile);

						free(ntfpPath);
					}
				}
			}
			if (lParam == 0 && HIWORD(wParam) == 0) {
				switch (LOWORD(wParam)) {
					case ID_VIEW_GRIDLINES:
					{
						HWND hWndMain = getMainWindow(hWnd);
						int state = GetMenuState(GetMenu(hWndMain), ID_VIEW_GRIDLINES, MF_BYCOMMAND);
						state = !state;
						if (state) {
							data->showBorders = 1;
							CheckMenuItem(GetMenu(hWndMain), ID_VIEW_GRIDLINES, MF_CHECKED);
						} else {
							data->showBorders = 0;
							CheckMenuItem(GetMenu(hWndMain), ID_VIEW_GRIDLINES, MF_UNCHECKED);
						}
						SendMessage(data->hWndPreview, NV_RECALCULATE, 0, 0);
						InvalidateRect(hWnd, NULL, FALSE);
						break;
					}
					case ID_ZOOM_100:
					case ID_ZOOM_200:
					case ID_ZOOM_400:
					case ID_ZOOM_800:
					{
						if (LOWORD(wParam) == ID_ZOOM_100) data->scale = 1;
						if (LOWORD(wParam) == ID_ZOOM_200) data->scale = 2;
						if (LOWORD(wParam) == ID_ZOOM_400) data->scale = 4;
						if (LOWORD(wParam) == ID_ZOOM_800) data->scale = 8;

						int checkBox = ID_ZOOM_100;
						if (data->scale == 2) {
							checkBox = ID_ZOOM_200;
						} else if (data->scale == 4) {
							checkBox = ID_ZOOM_400;
						} else if (data->scale == 8) {
							checkBox = ID_ZOOM_800;
						}
						int ids[] = {ID_ZOOM_100, ID_ZOOM_200, ID_ZOOM_400, ID_ZOOM_800};
						for (int i = 0; i < sizeof(ids) / sizeof(*ids); i++) {
							int id = ids[i];
							CheckMenuItem(GetMenu(getMainWindow(hWnd)), id, (id == checkBox) ? MF_CHECKED : MF_UNCHECKED);
						}

						SendMessage(data->hWndPreview, NV_RECALCULATE, 0, 0);
						InvalidateRect(hWnd, NULL, FALSE);
						RedrawWindow(data->hWndPreview, NULL, NULL, RDW_FRAME | RDW_INVALIDATE);
						break;
					}
					case ID_FILE_SAVE:
					{
						HWND hWndMain = (HWND) GetWindowLong((HWND) GetWindowLong(hWnd, GWL_HWNDPARENT), GWL_HWNDPARENT);
						//if not in any format, it cannot be saved.
						if (!data->isNitro) {
							MessageBox(hWnd, L"Texture must be converted.", L"Not converted", MB_ICONERROR);
							break;
						}
						if (*data->szCurrentFile == L'\0') {
							//browse for file
							LPWSTR path = saveFileDialog(hWndMain, L"Save Texture", L"Nitro TGA Files (*.tga)\0*.tga\0All Files\0*.*\0\0", L"tga");
							if (!path) break;
							memcpy(data->szCurrentFile, path, 2 * wcslen(path) + 2);
							free(path);
						}
						writeNitroTGA(data->szCurrentFile, &data->textureData.texels, &data->textureData.palette);
						break;
					}
					case ID_FILE_EXPORT:
					{
						//PNG export
						HWND hWndMain = getMainWindow(hWnd);
						LPWSTR path = saveFileDialog(hWndMain, L"Export Texture", L"PNG files (*.png)\0*.png\0All Files\0*.*\0", L".png");
						if (path == NULL) break;

						//if texture is in DS format, export from texture data
						if (data->isNitro) {
							exportTextureImage(path, &data->textureData);
						} else {
							imageWrite(data->px, data->width, data->height, path);
						}
						free(path);
						break;
					}
				}
			}
			break;
		}
		case WM_DESTROY:
		{
			if (data->hWndPaletteEditor) DestroyWindow(data->hWndPaletteEditor);
			if (data->hWndTileEditor) DestroyWindow(data->hWndTileEditor);
			SetWindowLongPtr(data->hWndPreview, 0, 0);
			free(data->px);
			if (data->textureData.palette.pal) free(data->textureData.palette.pal);
			if (data->textureData.texels.texel) free(data->textureData.texels.texel);
			if (data->textureData.texels.cmp) free(data->textureData.texels.cmp);
			free(data);
			SetWindowLongPtr(hWnd, 0, 0);
			break;
		}
		case NV_GETTYPE:
			return FILE_TYPE_TEXTURE;
	}
	return DefChildProc(hWnd, msg, wParam, lParam);
}

HWND CreateTextureTileEditor(HWND hWndParent, int tileX, int tileY) {
	HWND hWndMdi = (HWND) GetWindowLongPtr(hWndParent, GWL_HWNDPARENT);
	
	HWND hWnd = CreateWindowEx(WS_EX_CLIENTEDGE | WS_EX_MDICHILD, L"TextureTileEditorClass", L"Tile Editor", 
							   WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_CAPTION, CW_USEDEFAULT, CW_USEDEFAULT,
							   500, 300, hWndMdi, NULL, NULL, NULL);
	SetWindowLongPtr(hWnd, 0, (LONG_PTR) hWndParent);
	SetWindowLongPtr(hWnd, sizeof(LONG_PTR), (LONG_PTR) tileX);
	SetWindowLongPtr(hWnd, 2 * sizeof(LONG_PTR), (LONG_PTR) tileY);
	SendMessage(hWnd, NV_INITIALIZE, 0, 0);
	ShowWindow(hWnd, SW_SHOW);
	return hWnd;
}

int getTextureOffsetByTileCoordinates(TEXELS *texel, int x, int y) {
	int fmt = FORMAT(texel->texImageParam);
	int width = TEXW(texel->texImageParam);
	int height = TEXH(texel->texImageParam);

	int bits[] = { 0, 8, 2, 4, 8, 2, 8, 16 };

	if(fmt != CT_4x4){
		int pxX = x * 4;
		int pxY = y * 4;
		return (pxX + pxY * width) * bits[fmt] / 8;
	}

	int tileNumber = x + (width / 4) * y;
	int tileOffset = tileNumber * 4;
	return tileOffset;
}

int ilog2(int x);

void DrawColorEntryAlpha(HDC hDC, HPEN hOutline, COLOR color, float alpha, int x, int y) {
	HPEN hOldPen = (HPEN) SelectObject(hDC, hOutline);
	HPEN hNullPen = GetStockObject(NULL_PEN);
	HBRUSH hNullBrush = GetStockObject(NULL_BRUSH);
	HBRUSH hOldBrush = (HBRUSH) SelectObject(hDC, hNullBrush);

	if (alpha == 1.0f) {
		HBRUSH hBg = CreateSolidBrush((COLORREF) ColorConvertFromDS(color));
		SelectObject(hDC, hBg);
		Rectangle(hDC, x, y, x + 16, y + 16);
		DeleteObject(hBg);
	} else {
		COLOR32 c = ColorConvertFromDS(color);
		int r = c & 0xFF, g = (c >> 8) & 0xFF, b = (c >> 16) & 0xFF;

		int wr = (int) (r * alpha + 255 * (1.0f - alpha) + 0.5f);
		int wg = (int) (g * alpha + 255 * (1.0f - alpha) + 0.5f);
		int wb = (int) (b * alpha + 255 * (1.0f - alpha) + 0.5f);
		int gr = (int) (r * alpha + 192 * (1.0f - alpha) + 0.5f);
		int gg = (int) (g * alpha + 192 * (1.0f - alpha) + 0.5f);
		int gb = (int) (b * alpha + 192 * (1.0f - alpha) + 0.5f);
		HBRUSH hbrWhite = CreateSolidBrush(RGB(wr, wg, wb));
		HBRUSH hbrGray = CreateSolidBrush(RGB(gr, gg, gb));

		SelectObject(hDC, hbrWhite);
		Rectangle(hDC, x, y, x + 16, y + 16);
		SelectObject(hDC, hbrGray);
		SelectObject(hDC, hNullPen);
		Rectangle(hDC, x + 8, y + 1, x + 16, y + 9);
		Rectangle(hDC, x + 1, y + 8, x + 9, y + 16);

		DeleteObject(hbrWhite);
		DeleteObject(hbrGray);
	}

	SelectObject(hDC, hOldPen);
	SelectObject(hDC, hOldBrush);
}

void PaintTextureTileEditor(HDC hDC, TEXTURE *texture, int tileX, int tileY, int colorIndex, int alphaIndex) {
	//first paint 4x4 tile (scaled 32x)
	unsigned char tileBuffer[128]; //big enough for an 8x8 texture of any format
	unsigned short indexBuffer[4] = { 0 }; //big enough for an 8x8 4x4 texture
	COLOR32 rendered[64];

	int param = texture->texels.texImageParam;
	int format = FORMAT(param), width = TEXW(param), height = TEXH(param);
	int offset = getTextureOffsetByTileCoordinates(&texture->texels, tileX, tileY);
	unsigned char *texelSrc = texture->texels.texel + offset;
	if (format == CT_4x4) indexBuffer[0] = texture->texels.cmp[offset / 4];

	int bits[] = { 0, 8, 2, 4, 8, 2, 8, 16 };
	int nBytesPerRow = 8 * bits[format] / 8;

	if (format != CT_4x4) {
		for (int y = 0; y < 4; y++) {
			memcpy(tileBuffer + y * nBytesPerRow, texelSrc + (y * width * bits[format] / 8), nBytesPerRow / 2);
		}
	} else {
		memcpy(tileBuffer, texelSrc, 4);
	}

	//assemble texture struct
	TEXTURE temp;
	temp.texels.cmp = indexBuffer;
	temp.texels.texel = tileBuffer;
	temp.texels.texImageParam = format << 26;
	temp.palette.nColors = texture->palette.nColors;
	temp.palette.pal = texture->palette.pal;
	textureRender(rendered, &temp.texels, &temp.palette, 0);

	//convert back to 4x4
	memmove(rendered + 0, rendered + 0, 16);
	memmove(rendered + 4, rendered + 8, 16);
	memmove(rendered + 8, rendered + 16, 16);
	memmove(rendered + 12, rendered + 24, 16);

	const scale = 32;
	COLOR32 *preview = (COLOR32 *) calloc(4 * 4 * scale * scale, sizeof(COLOR32));
	for (int y = 0; y < 4 * scale; y++) {
		for (int x = 0; x < 4 * scale; x++) {
			int sampleX = x / scale;
			int sampleY = y / scale;
			COLOR32 c = rendered[sampleX + sampleY * 4];

			int gray = ((x / 4) ^ (y / 4)) & 1;
			gray = gray ? 255 : 192;
			int alpha = (c >> 24) & 0xFF;
			if (alpha == 0) {
				preview[x + y * 4 * scale] = gray | (gray << 8) | (gray << 16);
			} else if (alpha == 255) {
				preview[x + y * 4 * scale] = c & 0xFFFFFF;
			} else {
				int r = c & 0xFF;
				int g = (c >> 8) & 0xFF;
				int b = (c >> 16) & 0xFF;
				r = (r * alpha + gray * (255 - alpha) + 127) / 255;
				g = (g * alpha + gray * (255 - alpha) + 127) / 255;
				b = (b * alpha + gray * (255 - alpha) + 127) / 255;
				preview[x + y * 4 * scale] = r | (g << 8) | (b << 16);
			}
		}
	}

	HBITMAP hBitmap = CreateBitmap(4 * scale, 4 * scale, 1, 32, preview);
	HDC hOffDC = CreateCompatibleDC(hDC);
	SelectObject(hOffDC, hBitmap);
	BitBlt(hDC, 0, 0, 4 * scale, 4 * scale, hOffDC, 0, 0, SRCCOPY);
	DeleteObject(hOffDC);
	DeleteObject(hBitmap);
	free(preview);

	//draw palette
	int nColors = texture->palette.nColors, transparentIndex = COL0TRANS(texture->texels.texImageParam) - 1;
	COLOR *pal = texture->palette.pal;
	COLOR stackPaletteBuffer[4];
	if (format == CT_4x4) {
		unsigned short mode = indexBuffer[0] & 0xC000;
		pal = stackPaletteBuffer;
		nColors = 4;
		transparentIndex = (mode == 0x0000 || mode == 0x4000) ? 3 : -1;
		int paletteIndex = (indexBuffer[0] & 0x3FFF) << 1;
		COLOR *palSrc = texture->palette.pal + paletteIndex;

		pal[0] = palSrc[0];
		pal[1] = palSrc[1];
		switch (mode) {
			case 0x0000:
				pal[2] = palSrc[2];
				pal[3] = 0;
				break;
			case 0x4000:
				pal[2] = ColorInterpolate(pal[0], pal[1], 0.5f);
				pal[3] = 0;
				break;
			case 0x8000:
				pal[2] = palSrc[2];
				pal[3] = palSrc[3];
				break;
			case 0xC000:
				pal[2] = ColorInterpolate(pal[0], pal[1], 0.375f);
				pal[3] = ColorInterpolate(pal[0], pal[1], 0.625f);
				break;
		}
	}

	//draw palette entries if not direct
	int selectedColor = colorIndex;
	HPEN hBlack = (HPEN) GetStockObject(BLACK_PEN);
	HPEN hWhite = (HPEN) GetStockObject(WHITE_PEN);
	if (format != CT_DIRECT) {
		for (int i = 0; i < nColors; i++) {
			if(i != selectedColor) SelectObject(hDC, hBlack);
			else SelectObject(hDC, hWhite);

			int x = 4 * scale + 10 + (i % 16) * 16;
			int y = (i / 16) * 16;
			DrawColorEntryAlpha(hDC, (i != selectedColor) ? hBlack : hWhite, pal[i], i == transparentIndex ? 0.0f : 1.0f, x, y);
		}
	}

	//draw alpha levels if a3i5 or a5i3
	int selectedAlpha = alphaIndex;
	if (format == CT_A3I5 || format == CT_A5I3) {
		COLOR selected = pal[selectedColor];
		int nLevels = (format == CT_A3I5) ? 8 : 32;
		for (int i = 0; i < nLevels; i++) {
			int x = 4 * scale + 10 + (i % 16) * 16;
			int y = 42 + (i / 16) * 16;
			float alpha = ((float) i) / (nLevels - 1);
			DrawColorEntryAlpha(hDC, (i != selectedAlpha) ? hBlack : hWhite, selected, alpha, x, y);
		}
	}
}

void swapRedBlueChannels(COLOR32 *px, int nPx) {
	for (int i = 0; i < nPx; i++) {
		COLOR32 c = px[i];
		px[i] = REVERSE(c);
	}
}

LRESULT CALLBACK TextureTileEditorWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	HWND hWndTextureEditor = (HWND) GetWindowLongPtr(hWnd, 0);
	if (hWndTextureEditor == NULL) return DefMDIChildProc(hWnd, msg, wParam, lParam);

	TEXTUREEDITORDATA *data = (TEXTUREEDITORDATA *) GetWindowLongPtr(hWndTextureEditor, 0);
	int tileX = GetWindowLongPtr(hWnd, sizeof(LONG_PTR));
	int tileY = GetWindowLongPtr(hWnd, 2 * sizeof(LONG_PTR));

	switch (msg) {
		case WM_CREATE:
			SetWindowSize(hWnd, 398, 260);
			break;
		case WM_PAINT:
		{
			PAINTSTRUCT ps;
			HDC hDC = BeginPaint(hWnd, &ps);
			
			if(hWndTextureEditor != NULL)
				PaintTextureTileEditor(hDC, &data->textureData, tileX, tileY, data->selectedColor, data->selectedAlpha);

			EndPaint(hWnd, &ps);
			break;
		}
		case NV_INITIALIZE:
		{
			SetWindowSize(hWnd, 398, 260);

			TEXELS *texels = &data->textureData.texels;
			int format = FORMAT(texels->texImageParam);
			if (format == CT_4x4) {
				data->hWndInterpolate = CreateWindow(L"BUTTON", L"Interpolate", WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX, 5, 133, 100, 22, hWnd, NULL, NULL, NULL);
				data->hWndTransparent = CreateWindow(L"BUTTON", L"Transparent", WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX, 5, 160, 100, 22, hWnd, NULL, NULL, NULL);
				CreateWindow(L"STATIC", L"Palette base:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, 5, 187, 60, 22, hWnd, NULL, NULL, NULL);
				data->hWndPaletteBase = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"0", WS_VISIBLE | WS_CHILD | ES_NUMBER, 70, 187, 50, 22, hWnd, NULL, NULL, NULL);
				EnumChildWindows(hWnd, SetFontProc, (LPARAM) (HFONT) GetStockObject(DEFAULT_GUI_FONT));

				//populate fields
				WCHAR buffer[8];
				int tilesX = TEXW(texels->texImageParam) / 4;
				uint16_t idx = texels->cmp[tileX + tileY * tilesX];
				if (!(idx & 0x8000))
					SendMessage(data->hWndTransparent, BM_SETCHECK, 1, 0);
				if (idx & 0x4000)
					SendMessage(data->hWndInterpolate, BM_SETCHECK, 1, 0);
				wsprintfW(buffer, L"%d", idx & 0x3FFF);
				SendMessage(data->hWndPaletteBase, WM_SETTEXT, wcslen(buffer), (LPARAM) buffer);
			}
			break;
		}
		case WM_COMMAND:
		{
			HWND hWndControl = (HWND) lParam;
			if (hWndControl != NULL) {
				TEXELS *texels = &data->textureData.texels;
				int width = TEXW(texels->texImageParam), height = TEXH(texels->texImageParam);
				int nPx = width * height, tilesX = width / 4;
				uint16_t *pIdx = &texels->cmp[tileX + tileY * tilesX];

				int notification = HIWORD(wParam);
				if (notification == BN_CLICKED && hWndControl == data->hWndTransparent) {
					int state = SendMessage(hWndControl, BM_GETCHECK, 0, 0) == BST_CHECKED;
					*pIdx = ((*pIdx) & 0x7FFF) | ((!state) << 15);
					textureRender(data->px, texels, &data->textureData.palette, 0);
					swapRedBlueChannels(data->px, nPx);
					InvalidateRect(data->hWnd, NULL, FALSE);
					InvalidateRect(hWnd, NULL, FALSE);
				} else if (notification == BN_CLICKED && hWndControl == data->hWndInterpolate) {
					int state = SendMessage(hWndControl, BM_GETCHECK, 0, 0) == BST_CHECKED;
					*pIdx = ((*pIdx) & 0xBFFF) | (state << 14);
					textureRender(data->px, texels, &data->textureData.palette, 0);
					swapRedBlueChannels(data->px, nPx);
					InvalidateRect(data->hWnd, NULL, FALSE);
					InvalidateRect(hWnd, NULL, FALSE);
				} else if (notification == EN_CHANGE && hWndControl == data->hWndPaletteBase) {
					WCHAR buffer[8];
					SendMessage(hWndControl, WM_GETTEXT, 8, (LPARAM) buffer);
					*pIdx = ((*pIdx) & 0xC000) | (_wtol(buffer) & 0x3FFF);
					textureRender(data->px, texels, &data->textureData.palette, 0);
					swapRedBlueChannels(data->px, nPx);
					InvalidateRect(data->hWnd, NULL, FALSE);
					InvalidateRect(hWnd, NULL, FALSE);
				}
			}
			break;
		}
		case WM_NCHITTEST:
		{
			int ht = DefMDIChildProc(hWnd, msg, wParam, lParam);
			if (ht == HTTOP || ht == HTBOTTOM || ht == HTLEFT || ht == HTRIGHT || ht == HTTOPLEFT || ht == HTTOPRIGHT
				|| ht == HTBOTTOMLEFT || ht == HTBOTTOMRIGHT) {
				return HTBORDER;
			}
			return ht;
		}
		case WM_LBUTTONDOWN:
		{
			SetCapture(hWnd);
			data->tileMouseDown = 1;
		}
		case WM_MOUSEMOVE:
		{
			if (!data->tileMouseDown) break;

			TEXELS *texels = &data->textureData.texels;
			PALETTE *palette = &data->textureData.palette;
			int format = FORMAT(texels->texImageParam);
			int width = TEXW(texels->texImageParam);
			int height = TEXH(texels->texImageParam);

			POINT pt;
			GetCursorPos(&pt);
			ScreenToClient(hWnd, &pt);

			if (pt.x >= 0 && pt.y >= 0 && pt.x < 128 && pt.y < 128) { //draw pixel
				int masks[] = { 0, 0xFF, 0x03, 0x0F, 0xFF, 0x03, 0xFF, 0xFFFF };
				int depths[] = { 0, 8, 2, 4, 8, 2, 8, 16 };
				int x = pt.x / 32;
				int y = pt.y / 32;

				//get pointer to containing u32
				int offset = getTextureOffsetByTileCoordinates(texels, tileX, tileY);
				unsigned char *pTile = texels->texel + offset;
				if (format == CT_4x4) {
					int index = x + y * 4;
					uint32_t *pTileCmp = (uint32_t *) pTile;
					*pTileCmp = ((*pTileCmp) & ~(3 << (index * 2))) | (data->selectedColor << (index * 2));
				} else {
					if (format == CT_A3I5 || format == CT_A5I3) {
						int alphaShift = (format == CT_A3I5) ? 5 : 3;
						int value = data->selectedColor | (data->selectedAlpha << alphaShift);
						unsigned char *pPx = pTile + y * width + x;
						*pPx = value;
					} else if (format == CT_4COLOR || format == CT_16COLOR || format == CT_256COLOR) {
						int depth = depths[format];
						unsigned char *pTexel = pTile + (y * width * depth / 8) + x * depth / 8;
						int pxAdvance = x % (8 / depth);
						int mask = (1 << depth) - 1;
						*pTexel = ((*pTexel) & ~(mask << (pxAdvance * depth))) | (data->selectedColor << (pxAdvance * depth));
					} else {
						if (msg == WM_LBUTTONDOWN) { //we don't really want click+drag for this one
							HWND hWndMain = getMainWindow(hWnd);
							COLOR *color = (COLOR *) (pTile + y * width * 2 + x * 2);
							CHOOSECOLOR cc = { 0 };
							cc.lStructSize = sizeof(cc);
							cc.hInstance = (HWND) (HINSTANCE) GetWindowLong(hWnd, GWL_HINSTANCE); //weird struct definition?
							cc.hwndOwner = hWndMain;
							cc.rgbResult = ColorConvertFromDS(*color);
							cc.lpCustColors = data->tmpCust;
							cc.Flags = 0x103;
							BOOL(__stdcall *ChooseColorFunction) (CHOOSECOLORW *) = ChooseColorW;
							if (GetMenuState(GetMenu(hWndMain), ID_VIEW_USE15BPPCOLORCHOOSER, MF_BYCOMMAND)) ChooseColorFunction = CustomChooseColor;
							if (ChooseColorFunction(&cc)) {
								COLOR32 result = cc.rgbResult;
								*color = 0x8000 | ColorConvertToDS(result);
							}
						}
					}
				}
				textureRender(data->px, texels, &data->textureData.palette, 0);
				swapRedBlueChannels(data->px, width * height);
				InvalidateRect(data->hWnd, NULL, FALSE);
				InvalidateRect(hWnd, NULL, FALSE);
			} else if (pt.x >= 138 && pt.y >= 0) { //select palette/alpha
				int nColors = palette->nColors;
				if (format == CT_4x4) nColors = 4;
				
				int x = (pt.x - 138) / 16;
				int y = pt.y / 16;
				int index = x + y * 16;
				if (index < nColors) {
					data->selectedColor = index;
					InvalidateRect(hWnd, NULL, FALSE);
				} else if((format == CT_A3I5 || format == CT_A5I3) && pt.y >= 42)  {
					int nLevels = (format == CT_A3I5) ? 8 : 32;
					y = (pt.y - 42) / 16;
					index = x + y * 16;
					if (index < nLevels) {
						data->selectedAlpha = index;
						InvalidateRect(hWnd, NULL, FALSE);
					}
				}
			}
			
			
			break;
		}
		case WM_LBUTTONUP:
		{
			data->tileMouseDown = 0;
			ReleaseCapture();
			break;
		}
	}
	return DefMDIChildProc(hWnd, msg, wParam, lParam);
}

LRESULT CALLBACK TexturePreviewWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	TEXTUREEDITORDATA *data = (TEXTUREEDITORDATA *) GetWindowLongPtr((HWND) GetWindowLong(hWnd, GWL_HWNDPARENT), 0);
	int contentWidth = 0, contentHeight = 0;
	if (data) {
		contentWidth = getDimension2(data->width / 4, data->showBorders, data->scale, 4);
		contentHeight = getDimension2(data->height / 4, data->showBorders, data->scale, 4);
	}

	//little hack for code reuse >:)
	FRAMEDATA *frameData = (FRAMEDATA *) GetWindowLongPtr(hWnd, 0);
	if (!frameData) {
		frameData = calloc(1, sizeof(FRAMEDATA));
		SetWindowLongPtr(hWnd, 0, (LONG_PTR) frameData);
	}
	frameData->contentWidth = contentWidth;
	frameData->contentHeight = contentHeight;

	UpdateScrollbarVisibility(hWnd);
	switch (msg) {
		case WM_CREATE:
		{
			ShowScrollBar(hWnd, SB_BOTH, FALSE);
			break;
		}
		case WM_PAINT:
		{
			PAINTSTRUCT ps;
			HDC hDC = BeginPaint(hWnd, &ps);

			RECT rcClient;
			GetClientRect(hWnd, &rcClient);

			SCROLLINFO horiz, vert;
			horiz.cbSize = sizeof(horiz);
			vert.cbSize = sizeof(vert);
			horiz.fMask = SIF_ALL;
			vert.fMask = SIF_ALL;
			GetScrollInfo(hWnd, SB_HORZ, &horiz);
			GetScrollInfo(hWnd, SB_VERT, &vert);

			HDC hOffDC = CreateCompatibleDC(hDC);
			HBITMAP hOffBitmap = CreateCompatibleBitmap(hDC, rcClient.right, rcClient.bottom);
			SelectObject(hOffDC, hOffBitmap);
			SelectObject(hOffDC, GetSysColorBrush(GetClassLong(hWnd, GCL_HBRBACKGROUND) - 1));
			SelectObject(hOffDC, GetStockObject(NULL_PEN));
			Rectangle(hOffDC, 0, 0, rcClient.right + 1, rcClient.bottom + 1);
			
			int width, height;
			HBITMAP hBitmap = CreateTileBitmap2(data->px, data->width, data->height, data->hoverX, data->hoverY, &width, &height, data->scale, data->showBorders, 4, TRUE, TRUE);
			HDC hCompat = CreateCompatibleDC(hDC);
			SelectObject(hCompat, hBitmap);
			BitBlt(hOffDC, -horiz.nPos, -vert.nPos, width, height, hCompat, 0, 0, SRCCOPY);
			DeleteObject(hCompat);
			DeleteObject(hBitmap);

			BitBlt(hDC, 0, 0, rcClient.right, rcClient.bottom, hOffDC, 0, 0, SRCCOPY);
			DeleteObject(hOffDC);
			DeleteObject(hOffBitmap);

			EndPaint(hWnd, &ps);
			break;
		}
		case WM_LBUTTONDOWN:
		{
			POINT mousePos;
			SCROLLINFO horiz, vert;
			horiz.cbSize = sizeof(horiz);
			vert.cbSize = sizeof(vert);
			horiz.fMask = SIF_ALL;
			vert.fMask = SIF_ALL;
			GetScrollInfo(hWnd, SB_HORZ, &horiz);
			GetScrollInfo(hWnd, SB_VERT, &vert);
			GetCursorPos(&mousePos);
			ScreenToClient(hWnd, &mousePos);
			mousePos.x += horiz.nPos;
			mousePos.y += vert.nPos;

			//find the tile coordinates.
			int x = 0, y = 0;
			if (data->showBorders) {
				mousePos.x -= 1;
				mousePos.y -= 1;
				if (mousePos.x < 0) mousePos.x = 0;
				if (mousePos.y < 0) mousePos.y = 0;
				int cellWidth = 4 * data->scale + 1;
				mousePos.x /= cellWidth;
				mousePos.y /= cellWidth;
				x = mousePos.x;
				y = mousePos.y;
			} else {
				int cellWidth = 4 * data->scale;
				mousePos.x /= cellWidth;
				mousePos.y /= cellWidth;
				x = mousePos.x;
				y = mousePos.y;
			}

			int texImageParam = data->textureData.texels.texImageParam;
			int tilesX = TEXW(texImageParam) / 4;
			int tilesY = TEXH(texImageParam) / 4;
			if (x >= 0 && y >= 0 && x < tilesX && y < tilesY) {
				HWND hWndEditor = (HWND) GetWindowLongPtr(hWnd, GWL_HWNDPARENT);
				
				if (data->hWndTileEditor != NULL) DestroyChild(data->hWndTileEditor);
				data->hWndTileEditor = CreateTextureTileEditor(hWndEditor, x, y);
			}
			break;
		}
		case WM_RBUTTONDOWN:
		{
			POINT mouse;
			GetCursorPos(&mouse);
			HMENU hPopup = GetSubMenu(LoadMenu(GetModuleHandle(NULL), MAKEINTRESOURCE(IDR_MENU2)), 4);
			TrackPopupMenu(hPopup, TPM_TOPALIGN | TPM_LEFTALIGN | TPM_RIGHTBUTTON, mouse.x, mouse.y, 0, hWnd, NULL);
			break;
		}
		case WM_MOUSEMOVE:
		case WM_NCMOUSEMOVE:
		{
			TRACKMOUSEEVENT evt;
			evt.cbSize = sizeof(evt);
			evt.hwndTrack = hWnd;
			evt.dwHoverTime = 0;
			evt.dwFlags = TME_LEAVE;
			TrackMouseEvent(&evt);
		}
		case WM_MOUSELEAVE:
		{
			int oldHovered = data->hoverIndex;
			POINT mousePos;
			GetCursorPos(&mousePos);
			ScreenToClient(hWnd, &mousePos);

			SCROLLINFO horiz, vert;
			horiz.cbSize = sizeof(horiz);
			vert.cbSize = sizeof(vert);
			horiz.fMask = SIF_ALL;
			vert.fMask = SIF_ALL;
			GetScrollInfo(hWnd, SB_HORZ, &horiz);
			GetScrollInfo(hWnd, SB_VERT, &vert);
			mousePos.x += horiz.nPos;
			mousePos.y += vert.nPos;

			int hoverX = -1, hoverY = -1, hoverIndex = -1;
			if (mousePos.x >= 0 && mousePos.y >= 0 && mousePos.x < contentWidth && mousePos.y < contentHeight) {
				//find the tile coordinates.
				int x = 0, y = 0;
				if (data->showBorders) {
					mousePos.x -= 1;
					mousePos.y -= 1;
					if (mousePos.x < 0) mousePos.x = 0;
					if (mousePos.y < 0) mousePos.y = 0;
					int cellWidth = 4 * data->scale + 1;
					mousePos.x /= cellWidth;
					mousePos.y /= cellWidth;
					x = mousePos.x;
					y = mousePos.y;
				} else {
					int cellWidth = 4 * data->scale;
					mousePos.x /= cellWidth;
					mousePos.y /= cellWidth;
					x = mousePos.x;
					y = mousePos.y;
				}
				hoverX = x, hoverY = y;
				hoverIndex = hoverX + hoverY * (data->width / 4);
			}
			if (msg == WM_MOUSELEAVE) {
				hoverX = -1, hoverY = -1, hoverIndex = -1;
			}
			data->hoverX = hoverX;
			data->hoverY = hoverY;
			data->hoverIndex = hoverIndex;
			if (data->hoverIndex != oldHovered) {
				HWND hWndViewer = (HWND) GetWindowLong(hWnd, GWL_HWNDPARENT);
				HWND hWndMain = (HWND) GetWindowLong((HWND) GetWindowLong(hWndViewer, GWL_HWNDPARENT), GWL_HWNDPARENT);
				NITROPAINTSTRUCT *nitroPaintStruct = (NITROPAINTSTRUCT *) GetWindowLong(hWndMain, 0);
				InvalidateRect(hWndViewer, NULL, FALSE);
			}

			break;
		}
		case NV_RECALCULATE:
		{
			SCROLLINFO info;
			info.cbSize = sizeof(info);
			info.nMin = 0;
			info.nMax = contentWidth;
			info.fMask = SIF_RANGE;
			SetScrollInfo(hWnd, SB_HORZ, &info, TRUE);

			info.nMax = contentHeight;
			SetScrollInfo(hWnd, SB_VERT, &info, TRUE);
			RECT rcClient;
			GetClientRect(hWnd, &rcClient);
			SendMessage(hWnd, WM_SIZE, 0, rcClient.right | (rcClient.bottom << 16));
			break;
		}
		case WM_COMMAND:
		{
			HWND hWndControl = (HWND) lParam;
			if (hWndControl == NULL && HIWORD(wParam) == 0) {
				switch (LOWORD(wParam)) {
					case ID_TEXTUREMENU_COPY:
					{
						OpenClipboard(hWnd);
						EmptyClipboard();
						copyBitmap(data->px, data->width, data->height);
						CloseClipboard();
						break;
					}
				}
			}
		}
		case WM_ERASEBKGND:
			return 1;
		case WM_HSCROLL:
		case WM_VSCROLL:
		case WM_MOUSEWHEEL:
			return DefChildProc(hWnd, msg, wParam, lParam);
		case WM_SIZE:
		{
			SCROLLINFO info;
			info.cbSize = sizeof(info);
			info.nMin = 0;
			info.nMax = contentWidth;
			info.fMask = SIF_RANGE;
			SetScrollInfo(hWnd, SB_HORZ, &info, TRUE);

			info.nMax = contentHeight;
			SetScrollInfo(hWnd, SB_VERT, &info, TRUE);
			return DefChildProc(hWnd, msg, wParam, lParam);
		}
	}
	return DefWindowProc(hWnd, msg, wParam, lParam);
}

int isTranslucent(COLOR32 *px, int nWidth, int nHeight) {
	for (int i = 0; i < nWidth * nHeight; i++) {
		int a = px[i] >> 24;
		if (a >= 5 && a <= 250) return 1;
	}
	return 0;
}

int guessFormat(COLOR32 *px, int nWidth, int nHeight) {
	//Guess a good format for the data. Default to 4x4.
	int fmt = CT_4x4;
	
	//if the texture is 1024x1024, do not choose 4x4.
	if (nWidth * nHeight == 1024 * 1024) fmt = CT_256COLOR;

	//is there translucency?
	if (isTranslucent(px, nWidth, nHeight)) {
		//then choose a3i5 or a5i3. Do this by using color count.
		int colorCount = countColors(px, nWidth * nHeight);
		if (colorCount < 16) {
			//colors < 16, choose a5i3.
			fmt = CT_A5I3;
		} else {
			//otherwise, choose a3i5.
			fmt = CT_A3I5;
		}
	} else {
		//weigh the other format options for optimal size.
		int nColors = countColors(px, nWidth * nHeight);
		
		//if <= 4 colors, choose 4-color.
		if (nColors <= 4) {
			fmt = CT_4COLOR;
		} else {
			//weigh 16-color, 256-color, and 4x4. 
			//take the number of pixels per color. 
			int pixelsPerColor = 2 * nWidth * nHeight / nColors;
			if (pixelsPerColor >= 3 && !(nWidth * nHeight >= 1024 * 512)) {
				fmt = CT_4x4;
			} else if (nColors < 32) {
				//otherwise, 4x4 probably isn't a good option.
				fmt = CT_16COLOR;
			}
		}
	}

	return fmt;
}
void createPaletteName(WCHAR *buffer, WCHAR *file) {
	//find the last \ or /
	int index = -1;
	unsigned int i;
	for (i = 0; i < wcslen(file); i++) {
		if (file[i] == L'\\' || file[i] == L'/') index = i;
	}
	file += index + 1;
	//copy up to 12 characters of the file name
	for (i = 0; i < 12; i++) {
		WCHAR c = file[i];
		if (c == L'\0' || c == L'.') break;
		buffer[i] = c;
	}
	//suffix _pl
	memcpy(buffer + i, L"_pl", 6);
}

float mylog2(float d) { //UGLY!
	float ans;
	_asm {
		fld1
		fld dword ptr [d]
		fyl2x
		fstp dword ptr [ans]
	}
	return ans;
}
#define log2 mylog2

int chooseColorCount(int bWidth, int bHeight) {
	int area = bWidth * bHeight;

	//for textures smaller than 256x256, use 8*sqrt(area)
	if (area < 256 * 256) {
		int nColors = (int) (8 * sqrt((float) area));
		nColors = (nColors + 15) & ~15;
		return nColors;
	}

	//larger sizes, increase by 256 every width/height increment
	return (int) (256 * (log2((float) area) - 10));
}

void updateConvertDialog(TEXTUREEDITORDATA *data) {
	HWND hWndFormat = data->hWndFormat;
	int sel = SendMessage(hWndFormat, CB_GETCURSEL, 0, 0);
	int fixedPalette = SendMessage(data->hWndFixedPalette, BM_GETCHECK, 0, 0) == BST_CHECKED;
	int dither = SendMessage(data->hWndDither, BM_GETCHECK, 0, 0) == BST_CHECKED;
	int ditherAlpha = SendMessage(data->hWndDitherAlpha, BM_GETCHECK, 0, 0) == BST_CHECKED;
	int limitPalette = SendMessage(data->hWndLimitPalette, BM_GETCHECK, 0, 0) == BST_CHECKED;
	int fmt = sel + 1;
	//some things are only applicable to certain formats!
	BOOL disables[] = {TRUE, FALSE, TRUE, FALSE, FALSE, FALSE};
	switch (fmt) {
		case CT_A3I5:
		case CT_A5I3:
		{
			disables[0] = FALSE;
			disables[1] = FALSE;
			disables[2] = TRUE;
			disables[3] = FALSE;
			break;
		}
		case CT_4x4:
		{
			disables[0] = TRUE;
			disables[2] = FALSE;
			disables[3] = FALSE;
			break;
		}
		case CT_DIRECT:
		{
			disables[0] = TRUE;
			disables[1] = FALSE;
			disables[2] = TRUE;
			disables[3] = TRUE;
			disables[4] = TRUE;
			disables[5] = TRUE;
			break;
		}
	}
	if (!disables[4]) {
		if (!fixedPalette) disables[5] = TRUE;
	}
	if (fixedPalette && !disables[5]) {
		disables[2] = TRUE;
	}
	setStyle(data->hWndDitherAlpha, disables[0], WS_DISABLED);
	setStyle(data->hWndDither, disables[1], WS_DISABLED);
	setStyle(data->hWndColorEntries, disables[2] || !limitPalette, WS_DISABLED);
	setStyle(data->hWndOptimizationSlider, disables[2], WS_DISABLED);
	setStyle(data->hWndPaletteName, disables[3], WS_DISABLED);
	setStyle(data->hWndFixedPalette, disables[4], WS_DISABLED);
	setStyle(data->hWndPaletteInput, disables[5], WS_DISABLED);
	setStyle(data->hWndPaletteBrowse, disables[5], WS_DISABLED);
	setStyle(data->hWndPaletteSize, (fmt == CT_4x4 || fmt == CT_DIRECT) || fixedPalette, WS_DISABLED);
	setStyle(data->hWndLimitPalette, (fmt != CT_4x4) || fixedPalette, WS_DISABLED);
	setStyle(data->hWndBalance, fmt == CT_DIRECT, WS_DISABLED);
	setStyle(data->hWndColorBalance, fmt == CT_DIRECT, WS_DISABLED);
	setStyle(data->hWndEnhanceColors, fmt == CT_DIRECT, WS_DISABLED);

	setStyle(data->hWndDiffuseAmount, !((dither && !disables[1]) || (ditherAlpha && !disables[0])), WS_DISABLED);
	SetFocus(data->hWndConvertDialog);
	InvalidateRect(data->hWndConvertDialog, NULL, FALSE);
}

void conversionCallback(void *p) {
	TEXTUREEDITORDATA *data = (TEXTUREEDITORDATA *) p;
	InvalidateRect(data->hWndPreview, NULL, FALSE);
	data->isNitro = TRUE;

	HWND hWndMain = (HWND) GetWindowLong((HWND) GetWindowLong(data->hWnd, GWL_HWNDPARENT), GWL_HWNDPARENT);
	setStyle(hWndMain, FALSE, WS_DISABLED);
	SendMessage(data->hWndProgress, WM_CLOSE, 0, 0);
	SetForegroundWindow(hWndMain);
	data->hWndProgress = NULL;

	UpdatePaletteLabel(data->hWnd);
	int fmt = FORMAT(data->textureData.texels.texImageParam);
	data->selectedAlpha = (fmt == CT_A3I5) ? 7 : ((fmt == CT_A5I3) ? 31 : 0);
	data->selectedColor = 0;
}

LRESULT CALLBACK ConvertDialogWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	TEXTUREEDITORDATA *data = (TEXTUREEDITORDATA *) GetWindowLongPtr(hWnd, 0);
	switch (msg) {
		case WM_CREATE:
			SetWindowSize(hWnd, 490, 417);
			break;
		case NV_INITIALIZE:
		{
			int boxWidth = 100 + 100 + 10 + 10 + 10; //box width
			int boxHeight = 4 * 27 - 5 + 10 + 10 + 10; //first row height
			int boxHeight2 = 3 * 27 - 5 + 10 + 10 + 10; //second row height
			int boxHeight3 = 3 * 27 - 5 + 10 + 10 + 10; //third row height
			int width = 30 + 2 * boxWidth; //window width
			int height = 10 + boxHeight + 10 + boxHeight2 + 10 + boxHeight3 + 10 + 22 + 10; //window height

			int leftX = 10 + 10; //left box X
			int rightX = 10 + boxWidth + 10 + 10; //right box X
			int topY = 10 + 10 + 8; //top box Y
			int middleY = boxHeight + 10 + topY; //middle box Y
			int bottomY = boxHeight + 10 + boxHeight2 + 10 + topY; //bottom box Y

			CreateWindow(L"STATIC", L"Format:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, leftX, topY, 75, 22, hWnd, NULL, NULL, NULL);
			data->hWndFormat = CreateWindow(WC_COMBOBOX, L"", WS_VISIBLE | WS_CHILD | CBS_HASSTRINGS | CBS_DROPDOWNLIST, leftX + 85, topY, 100, 100, hWnd, NULL, NULL, NULL);
			data->hWndDither = CreateWindow(L"BUTTON", L"Dither", WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX, leftX, topY + 27, 100, 22, hWnd, NULL, NULL, NULL);
			data->hWndDitherAlpha = CreateWindow(L"BUTTON", L"Dither Alpha", WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX, leftX, topY + 27 * 2, 100, 22, hWnd, NULL, NULL, NULL);
			CreateWindow(L"STATIC", L"Diffusion:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, leftX, topY + 27 * 3, 75, 22, hWnd, NULL, NULL, NULL);
			data->hWndDiffuseAmount = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"100", WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL | ES_NUMBER, leftX + 85, topY + 27 * 3, 100, 22, hWnd, NULL, NULL, NULL);

			CreateWindow(L"STATIC", L"Palette Name:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, rightX, topY, 75, 22, hWnd, NULL, NULL, NULL);
			data->hWndPaletteName = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL, rightX + 85, topY, 100, 22, hWnd, NULL, NULL, NULL);
			data->hWndFixedPalette = CreateWindow(L"BUTTON", L"Use Fixed Palette", WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX, rightX, topY + 27, 100, 22, hWnd, NULL, NULL, NULL);
			CreateWindow(L"STATIC", L"Palette File:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, rightX, topY + 27 * 2, 75, 22, hWnd, NULL, NULL, NULL);
			data->hWndPaletteInput = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL, rightX + 85, topY + 27 * 2, 75, 22, hWnd, NULL, NULL, NULL);
			data->hWndPaletteBrowse = CreateWindow(L"BUTTON", L"...", WS_VISIBLE | WS_CHILD, rightX + 85 + 75, topY + 27 * 2, 25, 22, hWnd, NULL, NULL, NULL);
			CreateWindow(L"STATIC", L"Colors:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, rightX, topY + 27 * 3, 75, 22, hWnd, NULL, NULL, NULL);
			data->hWndPaletteSize = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"256", WS_VISIBLE | WS_CHILD | ES_NUMBER, rightX + 85, topY + 27 * 3, 100, 22, hWnd, NULL, NULL, NULL);

			data->hWndLimitPalette = CreateWindow(L"BUTTON", L"Limit Palette Size", WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX, leftX, middleY, 100, 22, hWnd, NULL, NULL, NULL);
			CreateWindow(L"STATIC", L"Maximum Colors:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, leftX, middleY + 27, 100, 22, hWnd, NULL, NULL, NULL);
			data->hWndColorEntries = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"256", WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL | ES_NUMBER, leftX + 110, middleY + 27, 100, 22, hWnd, NULL, NULL, NULL);
			CreateWindow(L"STATIC", L"Optimization:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, leftX, middleY + 27 * 2, 100, 22, hWnd, NULL, NULL, NULL);
			data->hWndOptimizationSlider = CreateWindow(TRACKBAR_CLASS, L"", WS_VISIBLE | WS_CHILD | TBS_NOTIFYBEFOREMOVE, leftX + 110, middleY + 27 * 2, 210, 22, hWnd, NULL, NULL, NULL);
			data->hWndOptimizationLabel = CreateWindow(L"STATIC", L"0", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, leftX + 330, middleY + 27 * 2, 50, 22, hWnd, NULL, NULL, NULL);
			SendMessage(data->hWndLimitPalette, BM_SETCHECK, BST_CHECKED, 0);

			CreateWindow(L"STATIC", L"Balance:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, leftX, bottomY, 100, 22, hWnd, NULL, NULL, NULL);
			CreateWindow(L"STATIC", L"Color Balance:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, leftX, bottomY + 27, 100, 22, hWnd, NULL, NULL, NULL);
			data->hWndEnhanceColors = CreateWindow(L"BUTTON", L"Enhance Colors", WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX, leftX, bottomY + 27 * 2, 200, 22, hWnd, NULL, NULL, NULL);
			CreateWindow(L"STATIC", L"Lightness", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE | SS_RIGHT, leftX + 110, bottomY, 50, 22, hWnd, NULL, NULL, NULL);
			CreateWindow(L"STATIC", L"Color", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, leftX + 110 + 50 + 200, bottomY, 50, 22, hWnd, NULL, NULL, NULL);
			CreateWindow(L"STATIC", L"Green", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE | SS_RIGHT, leftX + 110, bottomY + 27, 50, 22, hWnd, NULL, NULL, NULL);
			CreateWindow(L"STATIC", L"Red", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, leftX + 110 + 50 + 200, bottomY + 27, 50, 22, hWnd, NULL, NULL, NULL);
			data->hWndBalance = CreateWindow(TRACKBAR_CLASS, L"", WS_VISIBLE | WS_CHILD, leftX + 110 + 50, bottomY, 200, 22, hWnd, NULL, NULL, NULL);
			data->hWndColorBalance = CreateWindow(TRACKBAR_CLASS, L"", WS_VISIBLE | WS_CHILD, leftX + 110 + 50, bottomY + 27, 200, 22, hWnd, NULL, NULL, NULL);

			CreateWindow(L"BUTTON", L"Texture", WS_VISIBLE | WS_CHILD | BS_GROUPBOX, leftX - 10, topY - 18, boxWidth, boxHeight, hWnd, NULL, NULL, NULL);
			CreateWindow(L"BUTTON", L"Palette", WS_VISIBLE | WS_CHILD | BS_GROUPBOX, rightX - 10, topY - 18, boxWidth, boxHeight, hWnd, NULL, NULL, NULL);
			CreateWindow(L"BUTTON", L"4x4 Compression", WS_VISIBLE | WS_CHILD | BS_GROUPBOX, leftX - 10, middleY - 18, rightX + boxWidth - leftX, boxHeight2, hWnd, NULL, NULL, NULL);
			CreateWindow(L"BUTTON", L"Color", WS_VISIBLE | WS_CHILD | BS_GROUPBOX, leftX - 10, bottomY - 18, rightX + boxWidth - leftX, boxHeight2, hWnd, NULL, NULL, NULL);

			data->hWndDoConvertButton = CreateWindow(L"BUTTON", L"Convert", WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON, width / 2 - 100, height - 32, 200, 22, hWnd, NULL, NULL, NULL);

			//populate the dropdown list
			WCHAR bf[16];
			int len;
			for (int i = 1; i <= CT_DIRECT; i++) {
				char *str = stringFromFormat(i);
				len = 0;
				while (*str) {
					bf[len] = *str;
					str++;
					len++;
				}
				bf[len] = L'\0';
				SendMessage(data->hWndFormat, CB_ADDSTRING, len, (LPARAM) bf);
			}

			int format = guessFormat(data->px, data->width, data->height);
			SendMessage(data->hWndFormat, CB_SETCURSEL, format - 1, 0);

			//pick default 4x4 color count
			int maxColors = chooseColorCount(data->width, data->height);
			len = wsprintfW(bf, L"%d", maxColors);
			SendMessage(data->hWndColorEntries, WM_SETTEXT, len, (LPARAM) bf);
			SendMessage(data->hWndOptimizationSlider, TBM_SETPOS, 0, 0);

			//init balance
			SendMessage(data->hWndBalance, TBM_SETRANGE, TRUE, BALANCE_MIN | (BALANCE_MAX << 16));
			SendMessage(data->hWndBalance, TBM_SETPOS, TRUE, BALANCE_DEFAULT);
			SendMessage(data->hWndColorBalance, TBM_SETRANGE, TRUE, BALANCE_MIN | (BALANCE_MAX << 16));
			SendMessage(data->hWndColorBalance, TBM_SETPOS, TRUE, BALANCE_DEFAULT);

			//fill palette name
			WCHAR pname[16] = { 0 };
			createPaletteName(pname, data->szInitialFile);
			SendMessage(data->hWndPaletteName, WM_SETTEXT, wcslen(pname), (LPARAM) pname);

			updateConvertDialog(data);
			EnumChildWindows(hWnd, SetFontProc, (LPARAM) GetStockObject(DEFAULT_GUI_FONT));
			break;
		}
		case WM_COMMAND:
		{
			HWND hWndControl = (HWND) lParam;
			if (hWndControl) {
				int controlCode = HIWORD(wParam);
				if (hWndControl == data->hWndFormat && controlCode == LBN_SELCHANGE) {
					updateConvertDialog(data);
					
					//color count - update for paletted textures
					WCHAR buffer[4];
					int format = SendMessage(hWndControl, CB_GETCURSEL, 0, 0) + 1;
					if (format != CT_DIRECT && format != CT_4x4) {
						int colorCounts[] = { 0, 32, 4, 16, 256, 0, 8, 0 };
						int len = wsprintfW(buffer, L"%d", colorCounts[format]);
						SendMessage(data->hWndPaletteSize, WM_SETTEXT, len, (LPARAM) buffer);
					}
				} else if (hWndControl == data->hWndFixedPalette && controlCode == BN_CLICKED) {
					updateConvertDialog(data);
				} else if (hWndControl == data->hWndDither && controlCode == BN_CLICKED) {
					updateConvertDialog(data);
				} else if (hWndControl == data->hWndDitherAlpha && controlCode == BN_CLICKED) {
					updateConvertDialog(data);
				} else if (hWndControl == data->hWndLimitPalette && controlCode == BN_CLICKED) {
					updateConvertDialog(data);
				} else if (hWndControl == data->hWndPaletteBrowse && controlCode == BN_CLICKED) {
					LPWSTR path = openFileDialog(hWnd, L"Select palette", L"Palette Files\0*.nclr;*ncl.bin;*.ntfp\0All Files\0*.*\0\0", L"");
					if (path != NULL) {
						SendMessage(data->hWndPaletteInput, WM_SETTEXT, wcslen(path), (LPARAM) path);
						free(path);
					}
				} else if (hWndControl == data->hWndDoConvertButton && controlCode == BN_CLICKED) {
					int fmt = SendMessage(data->hWndFormat, CB_GETCURSEL, 0, 0) + 1;
					BOOL fixedPalette = SendMessage(data->hWndFixedPalette, BM_GETCHECK, 0, 0) == BST_CHECKED;

					WCHAR path[MAX_PATH];
					SendMessage(data->hWndPaletteInput, WM_GETTEXT, MAX_PATH, (LPARAM) path);

					NCLR paletteFile = { 0 };
					if (fixedPalette) {
						int status = 1;
						if (path[0]) {
							status = nclrReadFile(&paletteFile, path);
						}
						if (status) {
							MessageBox(hWnd, L"Invalid palette file.", L"Invalid file", MB_ICONERROR);
							break;
						}
					}

					WCHAR bf[32];
					SendMessage(data->hWndColorEntries, WM_GETTEXT, 31, (LPARAM) bf);
					int colorEntries = _wtol(bf); //for 4x4
					SendMessage(data->hWndDiffuseAmount, WM_GETTEXT, 31, (LPARAM) bf);
					float diffuse = _wtol(bf) / 100.0f;
					SendMessage(data->hWndPaletteSize, WM_GETTEXT, 31, (LPARAM) bf);
					int paletteSize = _wtol(bf); //for non-4x4
					int optimization = SendMessage(data->hWndOptimizationSlider, TBM_GETPOS, 0, 0);
					SendMessage(data->hWndPaletteName, WM_GETTEXT, 17, (LPARAM) bf);

					BOOL dither = SendMessage(data->hWndDither, BM_GETCHECK, 0, 0) == BST_CHECKED;
					BOOL ditherAlpha = SendMessage(data->hWndDitherAlpha, BM_GETCHECK, 0, 0) == BST_CHECKED;
					int balance = SendMessage(data->hWndBalance, TBM_GETPOS, 0, 0);
					int colorBalance = SendMessage(data->hWndColorBalance, TBM_GETPOS, 0, 0);
					BOOL enhanceColors = SendMessage(data->hWndEnhanceColors, BM_GETCHECK, 0, 0) == BST_CHECKED;
					BOOL limitPalette = SendMessage(data->hWndLimitPalette, BM_GETCHECK, 0, 0) == BST_CHECKED;

					//if we set to not limit palette, set the max size to the max allowed
					if (!limitPalette) {
						colorEntries = 32768;
					}

					char mbpnam[16];
					for (int i = 0; i < 16; i++) {
						mbpnam[i] = (char) bf[i];
					}

					HWND hWndMain = (HWND) GetWindowLong(hWnd, GWL_HWNDPARENT);
					data->hWndProgress = CreateWindow(L"CompressionProgress", L"Compressing", WS_OVERLAPPEDWINDOW & ~(WS_THICKFRAME | WS_MAXIMIZEBOX | WS_MINIMIZEBOX), 
													  CW_USEDEFAULT, CW_USEDEFAULT, 500, 150, hWndMain, NULL, NULL, NULL);
					ShowWindow(data->hWndProgress, SW_SHOW);
					SendMessage(hWnd, WM_CLOSE, 0, 0);
					SetActiveWindow(data->hWndProgress);
					textureConvertThreaded(data->px, data->width, data->height, fmt, dither, diffuse, ditherAlpha, 
									fixedPalette ? paletteFile.nColors : (fmt == CT_4x4 ? colorEntries : paletteSize), 
									fixedPalette, paletteFile.colors, optimization, balance, colorBalance, enhanceColors,
									mbpnam, &data->textureData, conversionCallback, (void *) data);

					SetWindowLong(hWndMain, GWL_STYLE, GetWindowLong(hWndMain, GWL_STYLE) | WS_DISABLED);
				}
			}
			break;
		}
		case WM_HSCROLL:
		{
			HWND hWndControl = (HWND) lParam;
			 if (hWndControl == data->hWndOptimizationSlider) {
				 WCHAR bf[8];
				 int len = wsprintfW(bf, L"%d", SendMessage(hWndControl, TBM_GETPOS, 0, 0));
				 SendMessage(data->hWndOptimizationLabel, WM_SETTEXT, len, (LPARAM) bf);
			 }
			break;
		}
		case WM_CLOSE:
		{
			HWND hWndEditor = data->hWnd;
			HWND hWndMain = getMainWindow(hWndEditor);
			SetWindowLong(hWndMain, GWL_STYLE, GetWindowLong(hWndMain, GWL_STYLE) & ~WS_DISABLED);
			SetActiveWindow(hWndMain);
			break;
		}
		case WM_DESTROY:
		{
			break;
		}
	}
	return DefWindowProc(hWnd, msg, wParam, lParam);
}

LRESULT CALLBACK CompressionProgressProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	switch (msg) {
		case WM_CREATE:
		{
			CreateWindow(L"STATIC", L"Progress:", WS_VISIBLE | WS_CHILD | SS_CENTERIMAGE, 10, 10, 100, 22, hWnd, NULL, NULL, NULL);
			HWND hWndProgress = CreateWindow(PROGRESS_CLASSW, L"", WS_VISIBLE | WS_CHILD, 10, 42, 400, 22, hWnd, NULL, NULL, NULL);
			SendMessage(hWndProgress, PBM_DELTAPOS, 1, 0);
			SetWindowLong(hWnd, 0, (LONG) hWndProgress);
			SetWindowSize(hWnd, 420, 74);
			EnumChildWindows(hWnd, SetFontProc, (LPARAM) GetStockObject(DEFAULT_GUI_FONT));

			SetTimer(hWnd, 1, 16, NULL);
			break;
		}
		case WM_TIMER:
		{
			if (g_texCompressionProgressMax) {
				HWND hWndProgress = (HWND) GetWindowLong(hWnd, 0);
				SendMessage(hWndProgress, PBM_SETRANGE, 0, g_texCompressionProgressMax << 16);
				SendMessage(hWndProgress, PBM_SETPOS, g_texCompressionProgress, 0);
			}
			break;
		}
		case WM_CLOSE:
		{
			if (g_texCompressionFinished) {
				KillTimer(hWnd, 1);
				break;
			} else {
				return 0;
			}
			break;
		}
	}
	return DefWindowProc(hWnd, msg, wParam, lParam);
}

typedef struct {
	FRAMEDATA frameData;
	TEXTUREEDITORDATA *data;
	int hoverX;
	int hoverY;
	int hoverIndex;
	int contextHoverIndex;
}TEXTUREPALETTEEDITORDATA;

LRESULT CALLBACK TexturePaletteEditorWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	TEXTUREPALETTEEDITORDATA *data = (TEXTUREPALETTEEDITORDATA *) GetWindowLongPtr(hWnd, 0);
	if (data == NULL) {
		data = (TEXTUREPALETTEEDITORDATA *) calloc(1, sizeof(TEXTUREPALETTEEDITORDATA));
		SetWindowLongPtr(hWnd, 0, (LONG_PTR) data);
	}
	switch (msg) {
		case NV_INITIALIZE:
		{
			data->data = (TEXTUREEDITORDATA *) lParam;
			data->frameData.contentWidth = 256;
			data->frameData.contentHeight = ((data->data->textureData.palette.nColors + 15) / 16) * 16;
			data->hoverX = -1;
			data->hoverY = -1;
			data->hoverIndex = -1;

			SCROLLINFO info;
			info.cbSize = sizeof(info);
			info.nMin = 0;
			info.nMax = data->frameData.contentHeight;
			info.fMask = SIF_RANGE;
			SetScrollInfo(hWnd, SB_VERT, &info, TRUE);
			SetWindowSize(hWnd, 256 + GetSystemMetrics(SM_CXVSCROLL) + 4, 256 + 4);
			break;
		}
		case WM_NCHITTEST:
		{
			LRESULT hit = DefChildProc(hWnd, msg, wParam, lParam);
			if (hit == HTLEFT || hit == HTRIGHT) return HTBORDER;
			if (hit == HTTOPLEFT) return HTTOP;
			if (hit == HTBOTTOMLEFT) return HTBOTTOM;
			if (hit == HTTOPRIGHT) return HTTOP;
			if (hit == HTBOTTOMRIGHT) return HTBOTTOM;
			return hit;
		}
		case WM_PAINT:
		{
			RECT rcClient;
			GetClientRect(hWnd, &rcClient);

			SCROLLINFO vert;
			vert.cbSize = sizeof(vert);
			vert.fMask = SIF_ALL;
			GetScrollInfo(hWnd, SB_VERT, &vert);

			PAINTSTRUCT ps;
			HDC hDC = BeginPaint(hWnd, &ps);

			HDC hOffDC = CreateCompatibleDC(hDC);
			HBITMAP hBitmap = CreateCompatibleBitmap(hDC, rcClient.right, rcClient.bottom);
			SelectObject(hOffDC, hBitmap);
			HBRUSH hBackground = GetSysColorBrush(GetClassLong(hWnd, GCL_HBRBACKGROUND) - 1);
			SelectObject(hOffDC, hBackground);
			HPEN hBlackPen = SelectObject(hOffDC, GetStockObject(NULL_PEN));
			Rectangle(hOffDC, 0, 0, rcClient.right + 1, rcClient.bottom + 1);
			SelectObject(hOffDC, hBlackPen);

			HPEN hRowPen = CreatePen(PS_SOLID, 1, RGB(127, 127, 127));
			HPEN hWhitePen = GetStockObject(WHITE_PEN);

			COLOR *palette = data->data->textureData.palette.pal;
			int nColors = data->data->textureData.palette.nColors;
			for (int i = 0; i < nColors; i++) {
				int x = i & 0xF, y = i >> 4;

				if (y * 16 + 16 - vert.nPos >= 0 && y * 16 - vert.nPos < rcClient.bottom) {
					HBRUSH hbr = CreateSolidBrush(ColorConvertFromDS(palette[i]));
					SelectObject(hOffDC, hbr);
					if (x + y * 16 == data->hoverIndex) SelectObject(hOffDC, hWhitePen);
					else if (y == data->hoverY) SelectObject(hOffDC, hRowPen);
					else SelectObject(hOffDC, hBlackPen);
					Rectangle(hOffDC, x * 16, y * 16 - vert.nPos, x * 16 + 16, y * 16 + 16 - vert.nPos);
					DeleteObject(hbr);
				}
			}

			BitBlt(hDC, 0, 0, rcClient.right, rcClient.bottom, hOffDC, 0, 0, SRCCOPY);
			EndPaint(hWnd, &ps);

			DeleteObject(hOffDC);
			DeleteObject(hBitmap);
			DeleteObject(hBackground);
			DeleteObject(hRowPen);
			break;
		}
		case WM_ERASEBKGND:
		{
			return 1;
		}
		case WM_MOUSEMOVE:
		case WM_NCMOUSEMOVE:
		{
			POINT mousePos;
			GetCursorPos(&mousePos);
			ScreenToClient(hWnd, &mousePos);

			SCROLLINFO horiz, vert;
			horiz.cbSize = sizeof(horiz);
			vert.cbSize = sizeof(vert);
			horiz.fMask = SIF_ALL;
			vert.fMask = SIF_ALL;
			GetScrollInfo(hWnd, SB_HORZ, &horiz);
			GetScrollInfo(hWnd, SB_VERT, &vert);
			mousePos.x += horiz.nPos;
			mousePos.y += vert.nPos;

			TRACKMOUSEEVENT evt;
			evt.cbSize = sizeof(evt);
			evt.hwndTrack = hWnd;
			evt.dwHoverTime = 0;
			evt.dwFlags = TME_LEAVE;
			TrackMouseEvent(&evt);

		}
		case WM_MOUSELEAVE:
		{
			POINT mousePos;
			GetCursorPos(&mousePos);
			ScreenToClient(hWnd, &mousePos);

			SCROLLINFO horiz, vert;
			horiz.cbSize = sizeof(horiz);
			vert.cbSize = sizeof(vert);
			horiz.fMask = SIF_ALL;
			vert.fMask = SIF_ALL;
			GetScrollInfo(hWnd, SB_HORZ, &horiz);
			GetScrollInfo(hWnd, SB_VERT, &vert);
			mousePos.x += horiz.nPos;
			mousePos.y += vert.nPos;

			int nRows = (data->data->textureData.palette.nColors + 15) / 16;

			int hoverX = -1, hoverY = -1, hoverIndex = -1;
			if (mousePos.x >= 0 && mousePos.x < 256 && mousePos.y >= 0) {
				hoverX = mousePos.x / 16;
				hoverY = mousePos.y / 16;
				hoverIndex = hoverX + hoverY * 16;
				if (hoverY >= nRows) {
					hoverX = -1, hoverY = -1, hoverIndex = -1;
				}
			}
			if (msg == WM_MOUSELEAVE) {
				hoverX = -1, hoverY = -1, hoverIndex = -1;
			}
			data->hoverX = hoverX;
			data->hoverY = hoverY;
			data->hoverIndex = hoverIndex;

			InvalidateRect(hWnd, NULL, FALSE);
			break;
		}
		case WM_LBUTTONDOWN:
		case WM_RBUTTONDOWN:
		{
			POINT pos;
			GetCursorPos(&pos);
			ScreenToClient(hWnd, &pos);

			SCROLLINFO vert;
			vert.cbSize = sizeof(vert);
			vert.fMask = SIF_ALL;
			GetScrollInfo(hWnd, SB_VERT, &vert);
			pos.y += vert.nPos;

			int x = pos.x / 16, y = pos.y / 16;
			if (x < 16) {
				int index = y * 16 + x;
				if (index < data->data->textureData.palette.nColors) {
					//if left click, open color editor dialogue.
					if (msg == WM_LBUTTONDOWN) {
						COLOR c = data->data->textureData.palette.pal[index];
						DWORD ex = ColorConvertFromDS(c);

						HWND hWndMain = getMainWindow(hWnd);
						CHOOSECOLOR cc = { 0 };
						cc.lStructSize = sizeof(cc);
						cc.hInstance = (HWND) (HINSTANCE) GetWindowLong(hWnd, GWL_HINSTANCE); //weird struct definition
						cc.hwndOwner = hWndMain;
						cc.rgbResult = ex;
						cc.lpCustColors = data->data->tmpCust;
						cc.Flags = 0x103;
						BOOL(__stdcall *ChooseColorFunction) (CHOOSECOLORW *) = ChooseColorW;
						if (GetMenuState(GetMenu(hWndMain), ID_VIEW_USE15BPPCOLORCHOOSER, MF_BYCOMMAND)) ChooseColorFunction = CustomChooseColor;
						if (ChooseColorFunction(&cc)) {
							DWORD result = cc.rgbResult;
							data->data->textureData.palette.pal[index] = ColorConvertToDS(result);
							InvalidateRect(hWnd, NULL, FALSE);

							textureRender(data->data->px, &data->data->textureData.texels, &data->data->textureData.palette, 0);
							int param = data->data->textureData.texels.texImageParam;
							int width = TEXW(param);
							int height = 8 << ((param >> 23) & 7);
							//textureRender outputs red and blue in the opposite order, so flip them here.
							for (int i = 0; i < width * height; i++) {
								DWORD p = data->data->px[i];
								data->data->px[i] = REVERSE(p);
							}
							InvalidateRect(data->data->hWnd, NULL, FALSE);
						}
					} else if (msg == WM_RBUTTONDOWN) {
						//otherwise open context menu
						data->contextHoverIndex = data->hoverIndex;
						HMENU hPopup = GetSubMenu(LoadMenu(GetModuleHandle(NULL), MAKEINTRESOURCE(IDR_MENU2)), 3);
						POINT mouse;
						GetCursorPos(&mouse);
						TrackPopupMenu(hPopup, TPM_TOPALIGN | TPM_LEFTALIGN | TPM_RIGHTBUTTON, mouse.x, mouse.y, 0, hWnd, NULL);
					}
				}
			}
			break;
		}
		case WM_COMMAND:
		{
			HWND hWndControl = (HWND) lParam;
			if (hWndControl == NULL && HIWORD(wParam) == 0) {
				WORD notification = LOWORD(wParam);
				switch (notification) {
					case ID_PALETTEMENU_PASTE:
					{
						int offset = data->contextHoverIndex & (~15);
						int maxOffset = data->data->textureData.palette.nColors;

						OpenClipboard(hWnd);
						PastePalette(data->data->textureData.palette.pal + offset, maxOffset - offset);
						CloseClipboard();

						TEXTURE *texture = &data->data->textureData;
						textureRender(data->data->px, &texture->texels, &texture->palette, 0);
						for (int i = 0; i < data->data->width * data->data->height; i++) {
							COLOR32 col = data->data->px[i];
							data->data->px[i] = REVERSE(col);
						}
						
						InvalidateRect(hWnd, NULL, FALSE);
						InvalidateRect(data->data->hWnd, NULL, FALSE);
						break;
					}
					case ID_PALETTEMENU_COPY:
					{
						int offset = data->contextHoverIndex & (~15);
						int length = 16;
						int maxOffset = data->data->textureData.palette.nColors;
						if (offset + length >= maxOffset) {
							length = maxOffset - offset;
							if (length < 0) length = 0;
						}

						OpenClipboard(hWnd);
						EmptyClipboard();
						CopyPalette(data->data->textureData.palette.pal + offset, length);
						CloseClipboard();
						break;
					}
					case ID_FILE_EXPORT:
					{
						//export as NTFP
						COLOR *colors = data->data->textureData.palette.pal;
						int nColors = data->data->textureData.palette.nColors;

						HWND hWndMain = (HWND) GetWindowLong((HWND) GetWindowLong(data->data->hWnd, GWL_HWNDPARENT), GWL_HWNDPARENT);
						LPWSTR path = saveFileDialog(hWndMain, L"Save NTFP", L"NTFP files (*.ntfp)\0*.ntfp\0All Files\0*.*\0\0", L"ntfp");
						if (path == NULL) break;

						DWORD dwWritten;
						HANDLE hFile = CreateFile(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
						WriteFile(hFile, colors, nColors * 2, &dwWritten, NULL);
						CloseHandle(hFile);

						free(path);
						break;
					}
					case ID_FILE_SAVE:
					case ID_FILE_SAVEAS:
					{
						SendMessage(data->data->hWnd, msg, notification, 0);
						break;
					}
				}
			}
			break;
		}
		case WM_DESTROY:
		{
			data->data->hWndPaletteEditor = NULL;
			free(data);
			SetWindowLongPtr(hWnd, 0, 0);
			return DefMDIChildProc(hWnd, msg, wParam, lParam);
		}
	}
	if (data->data != NULL) {
		return DefChildProc(hWnd, msg, wParam, lParam);
	} else {
		return DefMDIChildProc(hWnd, msg, wParam, lParam);
	}

}


typedef struct BATCHTEXCONVDATA_ {
	HWND hWndDirectory;
	HWND hWndBrowse;
	HWND hWndConvert;
	HWND hWndClean;
} BATCHTEXCONVDATA;

int EnumAllFiles(LPCWSTR path, BOOL(CALLBACK *fileCallback) (LPCWSTR, void *), BOOL(CALLBACK *dirCallback) (LPCWSTR, void *),
	BOOL(CALLBACK *preprocessDirCallback) (LPCWSTR, void *), void *param) {
	//copy string to add \*
	int pathlen = wcslen(path);
	WCHAR cpy[MAX_PATH + 2] = { 0 };
	memcpy(cpy, path, 2 * (pathlen + 1));

	//add \*
	if (pathlen == 0 || cpy[pathlen - 1] != L'\\') {
		cpy[pathlen++] = L'\\';
	}
	cpy[pathlen++] = L'*';

	WIN32_FIND_DATA ffd;
	HANDLE hFind = FindFirstFile(cpy, &ffd);

	//process all the things (if they exist)
	if (hFind == INVALID_HANDLE_VALUE) return 1;

	int status = 1;
	do {
		//if name is . or .., ignore
		if (_wcsicmp(ffd.cFileName, L".") == 0 || _wcsicmp(ffd.cFileName, L"..") == 0)
			continue;

		//get full name
		WCHAR fullPath[MAX_PATH] = { 0 };
		memcpy(fullPath, cpy, 2 * (pathlen - 1)); //cut off *
		memcpy(fullPath + pathlen - 1, ffd.cFileName, 2 * (wcslen(ffd.cFileName) + 1));

		//if a directory, procses it recursively. If a file, prcoess it normally.
		DWORD attr = ffd.dwFileAttributes;
		if (attr & FILE_ATTRIBUTE_DIRECTORY) {
			if (preprocessDirCallback == NULL || preprocessDirCallback(fullPath, param)) { //nonexistent or returns TRUE, process recurse
				status = EnumAllFiles(fullPath, fileCallback, dirCallback, preprocessDirCallback, param) && status; //prevent short-circuiting
			}
		} else {
			status = fileCallback(fullPath, param) && status;
		}
	} while (FindNextFile(hFind, &ffd));
	FindClose(hFind);

	//if we've succeeded, process the directory.
	if (status) {
		dirCallback(path, param);
	}
	return status;
}

BOOL CALLBACK DeleteFileCallback(LPCWSTR path, void *param) {
	return DeleteFile(path);
}

BOOL CALLBACK RemoveDirectoryCallback(LPCWSTR path, void *param) {
	return RemoveDirectory(path);
}

int BatchTexDelete(LPCWSTR path) {
	return EnumAllFiles(path, DeleteFileCallback, RemoveDirectoryCallback, NULL, NULL);
}

//some global state for the current batch operation
int g_batchTexConvertedTex = 0; //number of textures converted
LPCWSTR g_batchTexOut = NULL;
HWND g_hWndBatchTexWindow;

BOOL BatchTexReadOptions(LPCWSTR path, int *fmt, int *dither, int *ditherAlpha, float *diffuse, int *paletteSize, char *pnam,
	int *balance, int *colorBalance, int *enhanceColors) {

	char narrow[MAX_PATH] = { 0 };
	WCHAR buffer[MAX_PATH] = { 0 };
	BOOL hasMissing = FALSE; //any missing entries?

	//format
	GetPrivateProfileString(L"Texture", L"Format", L"", buffer, MAX_PATH, path);
	for (unsigned int i = 0; i <= wcslen(buffer); i++) {
		narrow[i] = (char) buffer[i];
	}
	int foundFormat = 0;
	for (int i = CT_A3I5; i <= CT_DIRECT; i++) {
		char *fname = stringFromFormat(i);
		if (strcmp(fname, narrow) == 0) {
			*fmt = i;
			foundFormat = 1;
			break;
		}
	}
	if (!foundFormat) hasMissing = TRUE;

	//dithering
	int rawInt = GetPrivateProfileInt(L"Texture", L"Dither", -1, path);
	if (rawInt == -1) hasMissing = TRUE;
	else *dither = rawInt;
	rawInt = GetPrivateProfileInt(L"Texture", L"DitherAlpha", -1, path);
	if (rawInt == -1) hasMissing = TRUE;
	else *ditherAlpha = rawInt;
	rawInt = GetPrivateProfileInt(L"Texture", L"Diffuse", -1, path);
	if (rawInt == -1) hasMissing = TRUE;
	else *diffuse = ((float) rawInt) / 100.0f;

	//palette
	rawInt = GetPrivateProfileInt(L"Texture", L"PaletteSize", -1, path);
	if (rawInt == -1) hasMissing = TRUE;
	else *paletteSize = rawInt;
	GetPrivateProfileString(L"Texture", L"PaletteName", L"", buffer, MAX_PATH, path);
	if (*buffer == L'\0') hasMissing = TRUE;
	else {
		memset(pnam, 0, 17);
		for (unsigned int i = 0; i < 16; i++) {
			pnam[i] = (char) buffer[i];
			if (buffer[i] == L'\0') break;
		}
	}

	//balance
	rawInt = GetPrivateProfileInt(L"Texture", L"Balance", -1, path);
	if (rawInt == -1) hasMissing = TRUE;
	else *balance = rawInt;
	rawInt = GetPrivateProfileInt(L"Texture", L"ColorBalance", -1, path);
	if (rawInt == -1) hasMissing = TRUE;
	else *colorBalance = rawInt;
	rawInt = GetPrivateProfileInt(L"Texture", L"EnhanceColors", -1, path);
	if (rawInt == -1) hasMissing = TRUE;
	else *enhanceColors = rawInt;

	return hasMissing;
}

void BatchTexWriteOptions(LPCWSTR path, int fmt, int dither, int ditherAlpha, float diffuse, int paletteSize, char *pnam, 
	int balance, int colorBalance, int enhanceColors) {

	//format
	WCHAR buffer[MAX_PATH] = { 0 };
	wsprintfW(buffer, L"%S", stringFromFormat(fmt));
	WritePrivateProfileString(L"Texture", L"Format", buffer, path);

	//dithering
	wsprintfW(buffer, L"%d", dither);
	WritePrivateProfileString(L"Texture", L"Dither", buffer, path);
	wsprintfW(buffer, L"%d", ditherAlpha);
	WritePrivateProfileString(L"Texture", L"DitherAlpha", buffer, path);
	wsprintfW(buffer, L"%d", (int) (diffuse * 100.0f + 0.5f));
	WritePrivateProfileString(L"Texture", L"Diffuse", buffer, path);

	//palette
	wsprintfW(buffer, L"%d", paletteSize);
	WritePrivateProfileString(L"Texture", L"PaletteSize", buffer, path);
	wsprintfW(buffer, L"%S", pnam);
	WritePrivateProfileString(L"Texture", L"PaletteName", buffer, path);

	//balance
	wsprintfW(buffer, L"%d", balance);
	WritePrivateProfileString(L"Texture", L"Balance", buffer, path);
	wsprintfW(buffer, L"%d", colorBalance);
	WritePrivateProfileString(L"Texture", L"ColorBalance", buffer, path);
	wsprintfW(buffer, L"%d", enhanceColors);
	WritePrivateProfileString(L"Texture", L"EnhanceColors", buffer, path);
}

BOOL BatchTexShouldConvert(LPCWSTR path, LPCWSTR configPath, LPCWSTR outPath) {
	HANDLE hTextureFile = CreateFile(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	HANDLE hConfigFile = CreateFile(configPath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	HANDLE hOutFile = CreateFile(outPath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	
	//if input doesn't exist, we can't possibly 
	BOOL shouldWrite = FALSE;
	FILETIME srcTime = { 0 }, configTime = { 0 }, destTime = { 0 };
	if (hTextureFile == INVALID_HANDLE_VALUE) {
		shouldWrite = FALSE;
		goto cleanup;
	}

	//if output doesn't exist or config doesn't exist, do output
	if (hOutFile == INVALID_HANDLE_VALUE || hConfigFile == INVALID_HANDLE_VALUE) {
		shouldWrite = TRUE;
		goto cleanup;
	}

	//if either of srcTime or configTime are greater than or equal to destTime, do write
	LARGE_INTEGER srcInt, configInt, destInt;
	GetFileTime(hTextureFile, NULL, NULL, &srcTime);
	GetFileTime(hConfigFile, NULL, NULL, &configTime);
	GetFileTime(hOutFile, NULL, NULL, &destTime);
	srcInt.LowPart = srcTime.dwLowDateTime;
	srcInt.HighPart = srcTime.dwHighDateTime;
	configInt.LowPart = configTime.dwLowDateTime;
	configInt.HighPart = configTime.dwHighDateTime;
	destInt.LowPart = destTime.dwLowDateTime;
	destInt.HighPart = destTime.dwHighDateTime;
	if (destInt.QuadPart <= srcInt.QuadPart || destInt.QuadPart <= configInt.QuadPart) {
		shouldWrite = TRUE;
		goto cleanup;
	}
	

cleanup:
	if (hTextureFile != INVALID_HANDLE_VALUE) CloseHandle(hTextureFile);
	if (hConfigFile != INVALID_HANDLE_VALUE) CloseHandle(hConfigFile);
	if (hOutFile != INVALID_HANDLE_VALUE) CloseHandle(hOutFile);
	return shouldWrite;
}

void BatchTexCheckFormatDir(LPCWSTR path, int *fmt) {
	LPCWSTR end = path + wcslen(path);
	int nSlashCounted = 0;
	while (end > path) {
		end--;
		if (*end == L'\\') {
			nSlashCounted++;
			if (nSlashCounted == 2) break; //second slash from end
		}
	}
	if (*end != L'\0') end++; //skip separator

	WCHAR dirNameBuf[MAX_PATH] = { 0 };
	for (unsigned int i = 0; i < wcslen(end); i++) {
		if (end[i] == L'\\') break;
		dirNameBuf[i] = end[i];
	}

	for (int i = CT_A3I5; i <= CT_DIRECT; i++) {
		WCHAR wideFmt[16] = { 0 };
		char *name = stringFromFormat(i);
		for (unsigned int j = 0; j < strlen(name); j++) wideFmt[j] = name[j];
		
		//case insensitive check
		if (_wcsicmp(dirNameBuf, wideFmt) == 0) {
			*fmt = i;
			break;
		}
	}
}

BOOL CALLBACK BatchTexConvertFileCallback(LPCWSTR path, void *param) {
	//read image
	int width, height;
	COLOR32 *px = gdipReadImage(path, &width, &height);

	//invalid image?
	if (px == NULL) {
		return TRUE; //just skip the file by reporting a success
	}

	//invalid texture size?
	if (!textureDimensionIsValid(width) || !textureDimensionIsValid(height)) {
		if (px) free(px);
		return FALSE; //report actual error
	}

	//construct output path (ensure .TGA extension)
	WCHAR outPath[MAX_PATH] = { 0 };
	LPWSTR filename = GetFileName(path);
	int outPathLen = wcslen(g_batchTexOut);
	memcpy(outPath, g_batchTexOut, 2 * (outPathLen + 1));
	outPath[outPathLen++] = L'\\';
	memcpy(outPath + outPathLen, filename, 2 * wcslen(filename) + 2);

	//ensure extension
	int extensionIndex = 0;
	for (unsigned int i = 0; i < wcslen(outPath); i++) {
		if (outPath[i] == L'.') extensionIndex = i;
	}
	memcpy(outPath + extensionIndex, L".TGA", 5 * sizeof(WCHAR));

	//construct congfiguration path; used to read/write for this texture
	WCHAR configPath[MAX_PATH] = { 0 };
	memcpy(configPath, path, 2 * (wcslen(path) + 1));
	extensionIndex = 0;
	for (unsigned int i = 0; i < wcslen(configPath); i++) {
		if (configPath[i] == L'.') extensionIndex = i;
	}
	memcpy(configPath + extensionIndex, L".INI", 5 * sizeof(WCHAR));

	//check: should we re-convert?
	BOOL doConvert = BatchTexShouldConvert(path, configPath, outPath);
	if (!doConvert) return TRUE; //skip

	int i;
	char pnam[17] = { 0 };
	for (i = 0; i < 12; i++) { //add _pl, max 15 chars
		if (filename[i] == L'\0') break;
		if (filename[i] == L'.') break;
		pnam[i] = (char) filename[i];
	}
	memcpy(pnam + i, "_pl", 4);

	//setup texture params
	int dither = 0, ditherAlpha = 0;
	float diffuse = 0.0f;

	//palette settings
	int useFixedPalette = 0;
	COLOR *fixedPalette = NULL;

	//4x4 settings
	int fmt = guessFormat(px, width, height);
	int colorEntries = chooseColorCount(width, height);
	int threshold4x4 = 0;

	//check the directory. Last directory name after base should be the name
	//of a format.
	BatchTexCheckFormatDir(path, &fmt);

	//max color entries for the selected format
	switch (fmt) {
		case CT_4COLOR:
			colorEntries = 4;
			break;
		case CT_16COLOR:
			colorEntries = 16;
			break;
		case CT_256COLOR:
			colorEntries = 256;
			break;
		case CT_A3I5:
			colorEntries = 32;
			break;
		case CT_A5I3:
			colorEntries = 8;
			break;
	}

	//balance settings
	int balance = BALANCE_DEFAULT, colorBalance = BALANCE_DEFAULT;
	int enhanceColors = 0;

	//read overrides from file.
	BOOL hasMissing = BatchTexReadOptions(configPath, &fmt, &dither, &ditherAlpha, &diffuse, &colorEntries, pnam,
		&balance, &colorBalance, &enhanceColors);

	//write back options to file (if there were any missing entries)
	if (hasMissing) {
		BatchTexWriteOptions(configPath, fmt, dither, ditherAlpha, diffuse, colorEntries, pnam, balance, colorBalance, enhanceColors);
	}

	HWND hWndMain = g_hWndBatchTexWindow;
	HWND hWndProgress = CreateWindow(L"CompressionProgress", L"Compressing", WS_OVERLAPPEDWINDOW & ~(WS_THICKFRAME | WS_MAXIMIZEBOX | WS_MINIMIZEBOX),
		CW_USEDEFAULT, CW_USEDEFAULT, 500, 150, hWndMain, NULL, NULL, NULL);
	ShowWindow(hWndProgress, SW_SHOW);

	TEXTURE texture = { 0 };
	HANDLE hThread = textureConvertThreaded(px, width, height, fmt, dither, diffuse, ditherAlpha, colorEntries,
		useFixedPalette, fixedPalette, threshold4x4, balance, colorBalance, enhanceColors, pnam, &texture,
		NULL, NULL);
	DoModalWait(hWndProgress, hThread); //modal wait progress window
	
	//write file out
	writeNitroTGA(outPath, &texture.texels, &texture.palette);

	//free texture memory
	if (texture.texels.texel != NULL) free(texture.texels.texel);
	if (texture.texels.cmp != NULL) free(texture.texels.cmp);
	if (texture.palette.pal != NULL) free(texture.palette.pal);
	return TRUE;
}

BOOL CALLBACK BatchTexConvertDirectoryCallback(LPCWSTR path, void *param) {
	return TRUE;
}

BOOL CALLBACK BatchTexConvertDirectoryExclusion(LPCWSTR path, void *param) {
	//if name ends in converted or converted\, reject (return FALSE)
	int len = wcslen(path);

	//check both endings
	LPCWSTR end = path + len;
	if (len >= 11 && _wcsicmp(end - 11, L"\\converted\\") == 0) return FALSE;
	if (len >= 10 && _wcsicmp(end - 10, L"\\converted") == 0) return FALSE;
	return TRUE;
}

int BatchTexConvert(LPCWSTR path, LPCWSTR convertedDir) {
	//ensure output directory exists
	BOOL b = CreateDirectory(convertedDir, NULL);
	if (!b && GetLastError() != ERROR_ALREADY_EXISTS) return 0; //failure

	//recursively process all the textures in this directory. Ugh ummm
	g_batchTexOut = convertedDir;
	int status = EnumAllFiles(path, BatchTexConvertFileCallback, BatchTexConvertDirectoryCallback, BatchTexConvertDirectoryExclusion, NULL);
	g_batchTexOut = NULL;
	return status;
}

BOOL CALLBACK BatchTexAddTexture(LPCWSTR path, void *param) {
	NSBTX *nsbtx = (NSBTX *) param;

	//read file and determine if valid
	int size;
	void *pf = fileReadWhole(path, &size);
	int valid = nitrotgaIsValid(pf, size);
	free(pf);
	if (!valid) return TRUE;

	//read texture
	TEXELS texture = { 0 };
	PALETTE palette = { 0 };
	nitroTgaRead(path, &texture, &palette);

	//add to NSBTX
	int fmt = FORMAT(texture.texImageParam);
	nsbtx->nTextures++;
	nsbtx->textures = (TEXELS *) realloc(nsbtx->textures, nsbtx->nTextures * sizeof(TEXELS));
	memcpy(nsbtx->textures + nsbtx->nTextures - 1, &texture, sizeof(texture));
	if (fmt != CT_DIRECT) {
		nsbtx->nPalettes++;
		nsbtx->palettes = (PALETTE *) realloc(nsbtx->palettes, nsbtx->nPalettes * sizeof(PALETTE));
		memcpy(nsbtx->palettes + nsbtx->nPalettes - 1, &palette, sizeof(palette));
	}
	return TRUE;
}

BOOL CALLBACK BatchTexAddDir(LPCWSTR path, void *param) {
	//do nothing
	return TRUE;
}

void BatchTexShowVramStatistics(HWND hWnd, LPCWSTR convertedDir) {
	//enumerate files in this folder and construct a temporary texture archive of them
	NSBTX nsbtx;
	nsbtxInit(&nsbtx, NSBTX_TYPE_NNS);
	EnumAllFiles(convertedDir, BatchTexAddTexture, BatchTexAddDir, NULL, (void *) &nsbtx);

	//create dialog
	CreateVramUseWindow(hWnd, &nsbtx);

	//free
	fileFree(&nsbtx.header);
}

LRESULT CALLBACK BatchTextureWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	BATCHTEXCONVDATA *data = (BATCHTEXCONVDATA *) GetWindowLongPtr(hWnd, 0);
	if (data == NULL) {
		data = (BATCHTEXCONVDATA *) calloc(1, sizeof(BATCHTEXCONVDATA));
		SetWindowLongPtr(hWnd, 0, (LONG_PTR) data);
	}

	switch (msg) {
		case WM_CREATE:
		{
			g_hWndBatchTexWindow = hWnd;
			CreateGroupbox(hWnd, L"Batch Conversion", 10, 10, 350, 78);
			CreateStatic(hWnd, L"Texture Directory:", 20, 28, 100, 22);
			data->hWndDirectory = CreateEdit(hWnd, L"", 125, 28, 200, 22, FALSE);
			data->hWndBrowse = CreateButton(hWnd, L"...", 325, 28, 25, 22, FALSE);
			data->hWndConvert = CreateButton(hWnd, L"Convert", 20, 55, 100, 22, TRUE);
			data->hWndClean = CreateButton(hWnd, L"Clean", 125, 55, 100, 22, FALSE);

			SetGUIFont(hWnd);
			SetWindowSize(hWnd, 370, 97);
			break;
		}
		case WM_COMMAND:
		{
			HWND hWndControl = (HWND) lParam;
			int notif = HIWORD(wParam);
			if (notif == BN_CLICKED && hWndControl == data->hWndBrowse) { //browse button
				WCHAR path[MAX_PATH]; //we will overwrite this with the *real* path

				BROWSEINFO bf;
				bf.hwndOwner = getMainWindow(hWnd);
				bf.pidlRoot = NULL;
				bf.pszDisplayName = path;
				bf.lpszTitle = L"Select output folder...";
				bf.ulFlags = BIF_RETURNONLYFSDIRS | BIF_EDITBOX | BIF_VALIDATE; //I don't much like the new dialog style
				bf.lpfn = NULL;
				bf.lParam = 0;
				bf.iImage = 0;
				PIDLIST_ABSOLUTE idl = SHBrowseForFolder(&bf);

				if (idl == NULL) {
					break;
				}
				SHGetPathFromIDList(idl, path);
				CoTaskMemFree(idl);
				SendMessage(data->hWndDirectory, WM_SETTEXT, wcslen(path), (LPARAM) path);
			} else if (notif == BN_CLICKED && (hWndControl == data->hWndClean || hWndControl == data->hWndConvert)) { //clean and convert buttons
				//delete directory\converted, if it exists
				WCHAR path[MAX_PATH] = { 0 };
				WCHAR convertedDir[MAX_PATH] = { 0 };
				int len = SendMessage(data->hWndDirectory, WM_GETTEXT, MAX_PATH, (LPARAM) path);
				if (len == 0) {
					MessageBox(hWnd, L"Enter a path.", L"No path", MB_ICONERROR);
					break;
				}

				//append \converted
				memcpy(convertedDir, path, 2 * (len + 1));
				if (convertedDir[len - 1] != L'\\') {
					convertedDir[len++] = L'\\';
				}
				memcpy(convertedDir + len, L"converted", 22);
				len += 10;

				if (hWndControl == data->hWndClean) {
					int status = BatchTexDelete(convertedDir);
					if (status) {
						MessageBox(hWnd, L"The operation completed successfully.", L"Result", MB_ICONINFORMATION);
					} else {
						MessageBox(hWnd, L"An error occurred.", L"Error", MB_ICONERROR);
					}
				} else {
					int status = BatchTexConvert(path, convertedDir);
					if (status) {
						BatchTexShowVramStatistics(hWnd, convertedDir);
					} else {
						MessageBox(hWnd, L"An error occurred.", L"Error", MB_ICONERROR);
					}
				}
			}
			break;
		}
		case WM_DESTROY:
		{
			free(data);
			break;
		}
	}
	return DefWindowProc(hWnd, msg, wParam, lParam);
}

int BatchTextureDialog(HWND hWndParent) {
	HWND hWnd = CreateWindow(L"BatchTextureClass", L"Batch Texture Conversion", WS_CAPTION | WS_SYSMENU,
		CW_USEDEFAULT, CW_USEDEFAULT, 300, 300, hWndParent, NULL, NULL, NULL);
	ShowWindow(hWnd, SW_SHOW);
	DoModal(hWnd);
	return 0;
}

void RegisterBatchTextureDialogClass() {
	RegisterGenericClass(L"BatchTextureClass", BatchTextureWndProc, sizeof(LPVOID));
}


VOID RegisterTexturePreviewClass(VOID) {
	WNDCLASSEX wcex = { 0 };
	wcex.cbSize = sizeof(wcex);
	wcex.style = CS_HREDRAW | CS_VREDRAW;
	wcex.hbrBackground = g_useDarkTheme? CreateSolidBrush(RGB(32, 32, 32)): (HBRUSH) COLOR_WINDOW;
	wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
	wcex.lpszClassName = L"TexturePreviewClass";
	wcex.lpfnWndProc = TexturePreviewWndProc;
	wcex.cbWndExtra = sizeof(LPVOID);
	wcex.hIcon = g_appIcon;
	wcex.hIconSm = g_appIcon;
	RegisterClassEx(&wcex);
}

VOID RegisterConvertDialogClass(VOID) {
	WNDCLASSEX wcex = { 0 };
	wcex.cbSize = sizeof(wcex);
	wcex.style = CS_HREDRAW | CS_VREDRAW;
	wcex.hbrBackground = g_useDarkTheme? CreateSolidBrush(RGB(32, 32, 32)): (HBRUSH) COLOR_WINDOW;
	wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
	wcex.lpszClassName = L"ConvertDialogClass";
	wcex.lpfnWndProc = ConvertDialogWndProc;
	wcex.cbWndExtra = sizeof(LPVOID);
	wcex.hIcon = g_appIcon;
	wcex.hIconSm = g_appIcon;
	RegisterClassEx(&wcex);
}

VOID RegisterCompressionProgressClass(VOID) {
	WNDCLASSEX wcex = { 0 };
	wcex.cbSize = sizeof(wcex);
	wcex.style = CS_HREDRAW | CS_VREDRAW;
	wcex.hbrBackground = g_useDarkTheme? CreateSolidBrush(RGB(32, 32, 32)): (HBRUSH) COLOR_WINDOW;
	wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
	wcex.lpszClassName = L"CompressionProgress";
	wcex.lpfnWndProc = CompressionProgressProc;
	wcex.cbWndExtra = sizeof(LPVOID);
	wcex.hIcon = g_appIcon;
	wcex.hIconSm = g_appIcon;
	RegisterClassEx(&wcex);
}

VOID RegisterTexturePaletteEditorClass(VOID) {
	WNDCLASSEX wcex = { 0 };
	wcex.cbSize = sizeof(wcex);
	wcex.style = CS_HREDRAW | CS_VREDRAW;
	wcex.hbrBackground = g_useDarkTheme? CreateSolidBrush(RGB(32, 32, 32)): (HBRUSH) COLOR_WINDOW;
	wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
	wcex.lpszClassName = L"TexturePaletteEditorClass";
	wcex.lpfnWndProc = TexturePaletteEditorWndProc;
	wcex.cbWndExtra = sizeof(LPVOID);
	wcex.hIcon = g_appIcon;
	wcex.hIconSm = g_appIcon;
	RegisterClassEx(&wcex);
}

VOID RegisterTextureTileEditorClass(VOID) {
	WNDCLASSEX wcex = { 0 };
	wcex.cbSize = sizeof(wcex);
	wcex.style = CS_HREDRAW | CS_VREDRAW;
	wcex.hbrBackground = g_useDarkTheme? CreateSolidBrush(RGB(32, 32, 32)): (HBRUSH) COLOR_WINDOW;
	wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
	wcex.lpszClassName = L"TextureTileEditorClass";
	wcex.lpfnWndProc = TextureTileEditorWndProc;
	wcex.cbWndExtra = 3 * sizeof(LONG_PTR);
	wcex.hIcon = g_appIcon;
	wcex.hIconSm = g_appIcon;
	RegisterClassEx(&wcex);
}

VOID RegisterTextureEditorClass(VOID) {
	WNDCLASSEX wcex = { 0 };
	wcex.cbSize = sizeof(wcex);
	wcex.style = CS_HREDRAW | CS_VREDRAW;
	wcex.hbrBackground = g_useDarkTheme? CreateSolidBrush(RGB(32, 32, 32)): (HBRUSH) COLOR_WINDOW;
	wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
	wcex.lpszClassName = L"TextureEditorClass";
	wcex.lpfnWndProc = TextureEditorWndProc;
	wcex.cbWndExtra = sizeof(LPVOID);
	wcex.hIcon = g_appIcon;
	wcex.hIconSm = g_appIcon;
	RegisterClassEx(&wcex);
	RegisterTexturePreviewClass();
	RegisterConvertDialogClass();
	RegisterCompressionProgressClass();
	RegisterTexturePaletteEditorClass();
	RegisterTextureTileEditorClass();
	RegisterBatchTextureDialogClass();
}

HWND CreateTexturePaletteEditor(int x, int y, int width, int height, HWND hWndParent, TEXTUREEDITORDATA *data) {
	HWND h = CreateWindowEx(WS_EX_CLIENTEDGE | WS_EX_MDICHILD, L"TexturePaletteEditorClass", L"Palette Editor", WS_VISIBLE | WS_CLIPSIBLINGS | WS_CAPTION | WS_CLIPCHILDREN | WS_VSCROLL, x, y, width, height, hWndParent, NULL, NULL, NULL);
	SendMessage(h, NV_INITIALIZE, 0, (LPARAM) data);
	return h;
}

HWND CreateTextureEditor(int x, int y, int width, int height, HWND hWndParent, LPCWSTR path) {
	int bWidth, bHeight;
	DWORD *bits = gdipReadImage(path, &bWidth, &bHeight);
	if (bits == NULL) {
		MessageBox(hWndParent, L"An invalid image file was specified.", L"Invalid Image", MB_ICONERROR);
		return NULL;
	}
	if (!textureDimensionIsValid(bWidth) || !textureDimensionIsValid(bHeight)) {
		free(bits);
		MessageBox(hWndParent, L"Textures must have dimensions as powers of two greater than or equal to 8, and not exceeding 1024.", L"Invalid dimensions", MB_ICONERROR);
		return NULL;
	}
	HWND h = CreateWindowEx(WS_EX_CLIENTEDGE | WS_EX_MDICHILD, L"TextureEditorClass", L"Texture Editor", WS_VISIBLE | WS_CLIPSIBLINGS | WS_CAPTION | WS_CLIPCHILDREN, x, y, width, height, hWndParent, NULL, NULL, NULL);
	SendMessage(h, NV_SETPATH, wcslen(path), (LPARAM) path);
	SendMessage(h, NV_INITIALIZE, bWidth | (bHeight << 16), (LPARAM) bits);
	return h;
}

HWND CreateTextureEditorImmediate(int x, int y, int width, int height, HWND hWndParent, TEXTURE *texture) {
	HWND h = CreateWindowEx(WS_EX_CLIENTEDGE | WS_EX_MDICHILD, L"TextureEditorClass", L"Texture Editor", 
		WS_VISIBLE | WS_CLIPSIBLINGS | WS_CAPTION | WS_CLIPCHILDREN, x, y, width, height, hWndParent, NULL, NULL, NULL);
	SendMessage(h, NV_INITIALIZE_IMMEDIATE, 0, (LPARAM) texture);
	return h;
}
