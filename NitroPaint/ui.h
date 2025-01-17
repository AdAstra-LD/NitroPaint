#pragma once
#include <Windows.h>

//static control alignments
#define SCA_LEFT    0
#define SCA_RIGHT   1
#define SCA_CENTER  2

//
// Sets or clears a window style.
//
void setStyle(HWND hWnd, BOOL set, DWORD style);

//
// Do modal.
//
void DoModal(HWND hWnd);

//
// Do modal, but destroy the window when a handle is signaled.
//
void DoModalWait(HWND hWnd, HANDLE hWait);

//
// Create a button, optionally a default button.
//
HWND CreateButton(HWND hWnd, LPCWSTR text, int x, int y, int width, int height, BOOL def);

//
// Create a checkbox.
//
HWND CreateCheckbox(HWND hWnd, LPCWSTR text, int x, int y, int width, int height, BOOL checked);

//
// Create a groupbox.
//
HWND CreateGroupbox(HWND hWnd, LPCWSTR title, int x, int y, int width, int height);

//
// Create an edit control, optionally a number-only control.
//
HWND CreateEdit(HWND hWnd, LPCWSTR text, int x, int y, int width, int height, BOOL number);

//
// Create a static control.
//
HWND CreateStatic(HWND hWnd, LPCWSTR text, int x, int y, int width, int height);

HWND CreateStaticAligned(HWND hWnd, LPCWSTR text, int x, int y, int width, int height, int alignment);

//
// Create a combobox.
//
HWND CreateCombobox(HWND hWnd, LPCWSTR *items, int nItems, int x, int y, int width, int height, int def);

//
// Create a trackbar.
//
HWND CreateTrackbar(HWND hWnd, int x, int y, int width, int height, int vMin, int vMax, int vDef);


//
// Get if a checkbox is checked.
//
int GetCheckboxChecked(HWND hWnd);

//
// Get the number value of an edit control.
//
int GetEditNumber(HWND hWnd);

//
// Get trackbar position
//
int GetTrackbarPosition(HWND hWnd);


//
// Create a ListView in report mode.
//
HWND CreateListView(HWND hWnd, int x, int y, int width, int height);

//
// Add a column to a ListView.
//
void AddListViewColumn(HWND hWnd, LPWSTR name, int col, int width, int alignment);

//
// Add an item to a ListView.
//
void AddListViewItem(HWND hWnd, LPWSTR text, int row, int col);
