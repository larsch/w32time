w32time.exe: w32time.c
	gcc -O2 -o $@ -Wall $<

install: w32time.exe
	strip -s w32time.exe
	cp w32time.exe \gh\bin\w32\w32time.exe
