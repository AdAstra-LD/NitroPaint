# gnu make, not nmake

all: ../msvcrt-static.lib

objects:=$(patsubst %.a86,%.obj,$(wildcard *.a86))
PATH:=$(PATH);c:/Programme/MSVC/2019/VC/Tools/MSVC/14.27.29110/bin/Hostx64/x86

%.obj: %.a86
	ml /c $<
#ml is the x86 microsoft assembler, known as masm.
#Must be in path. See above for my example.

../msvcrt-static.lib: $(objects)
	lib /out:$@ $^
	rm -f $^
