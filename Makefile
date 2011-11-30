OPT = -Wall -m32 -static -g -I.

.PHONY: all clean
all: v4l2n raw2pnm

v4l2n: v4l2n.c
	gcc $(OPT) $@.c -o $@

raw2pnm: raw2pnm.c
	gcc $(OPT) $@.c -o $@

clean:
	rm -f v4l2n raw2pnm


