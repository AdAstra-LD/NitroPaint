Just in case you have no luck with my self-made msvcrt-light*.lib libraries,
try these out of the Windows 2003 DDK.
* Your C/C++ program starts with main/WinMain, not mainCRTStartup/WinMainCRTStartup.
  A console program would be more compatible to Linux.
  The included startup code is then still quite small (but not zero of course).
* In Project settings:
  * Don't let the compiler include a manifest dictating loading MSVCR90.dll or similar
  * Switch on "/NODEFAULTLIB"
  * Add one of these files (depending on bitness) to the library dependencies list
That's all — and should work.
Static C++ constructors will run before main/WinMain entry, destructors afterwards.

henni, 2019-12-05
