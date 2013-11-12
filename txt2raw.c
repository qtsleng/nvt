#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

static void error(char *s)
{
	printf("%s\n", s);
	exit(1);
}

static int hextoval(int v)
{
	if (v >= '0' && v <= '9')
		return v - '0';
	if (v >= 'a' && v <= 'f')
		return v - 'a' + 10;
	if (v >= 'A' && v <= 'F')
		return v - 'A' + 10;
	error("not a hex char");
	return -1;
}

static void write_file(const char *name, const unsigned char *data, int size)
{
	FILE *f;
	int r;

	f = fopen(name, "wb");
	if (!f)
		error("can not open file");
	r = fwrite(data, size, 1, f);
	if (r != 1)
		error("failed to write data to file");
	r = fclose(f);
	if (r != 0)
		error("failed to close file");
}

static unsigned char *txt2buf(FILE *f, int size[2])
{
	int thiswidth = 0;
	int bufsize = 4096;
	unsigned char *buf;
	int len = 0;
	int val = 0;
	int bits = 0;
	int c;

	size[0] = size[1] = 0;

	buf = malloc(bufsize);
	if (!buf) error("out of memory");

	do {
		c = fgetc(f);

		if (!isxdigit(c) && bits > 0) {
			/* Completed value */
			if (bits != 8) error("only 8 bits per value supported");
			len++;
			thiswidth++;
			if (len > bufsize) {
				bufsize *= 2;
				buf = realloc(buf, bufsize);
				if (!buf) error("out of memory");
			}
			buf[len-1] = val;
			val = 0;
			bits = 0;
		}

		if (c == '\n' || c == EOF) {
			/* End of line */
			if (size[0] <= 0) {
				size[0] = thiswidth;
				printf("detected line length %i bytes\n", size[0]);
			}
			if (thiswidth > 0) {
				if (thiswidth != size[0]) error("bad line length");
				size[1]++;
				thiswidth = 0;
			}
		}

		if (isxdigit(c)) {
			/* Value continues... */
			bits += 4;
			val <<= 4;
			val |= hextoval(c);
		}
	} while (c != EOF);

	return buf;
}

static unsigned char *uncompress(unsigned char *compr, int compr_size[2], int uncomp_size[2])
{
	unsigned char *buf;		/* Resulting buffer */
	unsigned char *sa;		/* Source byte address */
	unsigned char *da;		/* Destination byte address */
	int y, x, b;			/* y, x, and b counters */
	const int d_byp = 2;		/* Destination bytes per pixel */
	const int s_bpp = 10;		/* Source bits per pixel */

	uncomp_size[0] = compr_size[0] * 8 / s_bpp;	/* Size in pixels */
	uncomp_size[1] = compr_size[1];
	buf = calloc(uncomp_size[0] * d_byp * compr_size[1], 1);

	for (y = 0; y < uncomp_size[1]; y++) {
		unsigned int s;			/* Source byte */
		int s_bits = 0;			/* Bits left in source byte */
		sa = compr + compr_size[0] * y;
		da = buf + uncomp_size[0] * d_byp * y;
		for (x = 0; x < uncomp_size[0]; x++) {
			unsigned int d = 0;	/* Destination pixel */
			for (b = 0; b < s_bpp; b++) {
				if (s_bits <= 0) {
					s = *sa++;
					s_bits = 8;
				}
				d <<= 1;
				d |= s & 1;
				s >>= 1;
				s_bits--;

			}
			da[0] = d & 0xff;
			da[1] = d >> 8;
			da += d_byp;
		}
	}

	return buf;
}

int main(int argc, char *argv[])
{
	char *name;
	unsigned char *compr;
	int compr_size[2];
	unsigned char *uncomp;
	int uncomp_size[2];

	if (argc != 2) error("give exactly one argument for output filename\n");
	name = argv[1];

	compr = txt2buf(stdin, compr_size);
	printf("read %ix%i bytes of data\n", compr_size[0], compr_size[1]);

	uncomp = uncompress(compr, compr_size, uncomp_size);
	printf("uncompressed to %ix%i pixels of data\n", uncomp_size[0], uncomp_size[1]);

	printf("writing to %s\n", name);
	write_file(name, uncomp, uncomp_size[0] * uncomp_size[1] * 2);

	free(compr);
	free(uncomp);

	return 0;
}