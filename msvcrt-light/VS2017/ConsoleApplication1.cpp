#include <windows.h>	// SetConsoleOutputCP, ExitProcess
#define _NO_CRT_STDIO_INLINE
#pragma comment(linker,"/NOD")
#include <stdio.h>	// printf

void mainCRTStartup() {
 SetConsoleOutputCP(CP_UTF8);
 printf("Hallo ÄÖÜäöüß°§你好\n");
 ExitProcess(0);
}

// Problem: Unter Windows 10 gibt es keine Schriftart, die sowohl Umlaute als auch chinesische Zeichen enthält.
// Mit Schriftart "Terminal" kommt Müll, aber mit "Consolas" gehen zumindest die Umlaute.
// Diese Datei ist UTF-8 ohne BOM. So funktioniert die Konsolenausgabe; nur die chinesischen Zeichen erscheinen nicht.
