#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdarg.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include "linux/videodev2.h"

#define MIN(a,b)	((a) <= (b) ? (a) : (b))
#define MAX(a,b)	((a) >= (b) ? (a) : (b))
#define CLAMP(a,lo,hi)	((a) < (lo) ? (lo) : (a) > (hi) ? (hi) : (a))
#define CLAMPB(a)	CLAMP(a, 0, 255)

char *name = "raw2pnm";

static int verbosity = 2;

struct symbol_list {
	int id;
	const char *symbol;
};
#define SYMBOL_END	{ -1, NULL }

#define V4L2_PIX_FMT	"V4L2_PIX_FMT_"
#define PIXFMT(id)	{ V4L2_PIX_FMT_##id, (#id) }
static const struct symbol_list pixelformats[] = {
	PIXFMT(RGB332),
	PIXFMT(RGB444),
	PIXFMT(RGB555),
	PIXFMT(RGB565),
	PIXFMT(RGB555X),
	PIXFMT(RGB565X),
	PIXFMT(BGR24),
	PIXFMT(RGB24),
	PIXFMT(BGR32),
	PIXFMT(RGB32),
	PIXFMT(GREY),
	PIXFMT(Y4),
	PIXFMT(Y6),
	PIXFMT(Y10),
	PIXFMT(Y16),
	PIXFMT(PAL8),
	PIXFMT(YVU410),
	PIXFMT(YVU420),
	PIXFMT(YUYV),
	PIXFMT(YYUV),
	PIXFMT(YVYU),
	PIXFMT(UYVY),
	PIXFMT(VYUY),
	PIXFMT(YUV422P),
	PIXFMT(YUV411P),
	PIXFMT(Y41P),
	PIXFMT(YUV444),
	PIXFMT(YUV555),
	PIXFMT(YUV565),
	PIXFMT(YUV32),
	PIXFMT(YUV410),
	PIXFMT(YUV420),
	PIXFMT(HI240),
	PIXFMT(HM12),
	PIXFMT(NV12),
	PIXFMT(NV21),
	PIXFMT(NV16),
	PIXFMT(NV61),
	PIXFMT(SBGGR8),
	PIXFMT(SGBRG8),
	PIXFMT(SGRBG8),
	PIXFMT(SRGGB8),
	PIXFMT(SBGGR10),
	PIXFMT(SGBRG10),
	PIXFMT(SGRBG10),
	PIXFMT(SRGGB10),
	PIXFMT(SBGGR12),
	PIXFMT(SGBRG12),
	PIXFMT(SGRBG12),
	PIXFMT(SRGGB12),
	PIXFMT(SGRBG10DPCM8),
	PIXFMT(SBGGR16),
	PIXFMT(MJPEG),
	PIXFMT(JPEG),
	PIXFMT(DV),
	PIXFMT(MPEG),
	PIXFMT(CPIA1),
	PIXFMT(WNVA),
	PIXFMT(SN9C10X),
	PIXFMT(SN9C20X_I420),
	PIXFMT(PWC1),
	PIXFMT(PWC2),
	PIXFMT(ET61X251),
	PIXFMT(SPCA501),
	PIXFMT(SPCA505),
	PIXFMT(SPCA508),
	PIXFMT(SPCA561),
	PIXFMT(PAC207),
	PIXFMT(MR97310A),
	PIXFMT(SN9C2028),
	PIXFMT(SQ905C),
	PIXFMT(PJPG),
	PIXFMT(OV511),
	PIXFMT(OV518),
	PIXFMT(STV0680),
	PIXFMT(TM6000),
	SYMBOL_END
};

static void print(int lvl, char *msg, ...)
{
	va_list ap;

	if (verbosity < lvl)
		return;

	va_start(ap, msg);
	vprintf(msg, ap);
	va_end(ap);
}

static void error(char *msg, ...)
{
	FILE *f = stdout;
	va_list ap;
	int e = errno;
 
	va_start(ap, msg);
	fprintf(f, "%s: ", name);
	vfprintf(f, msg, ap);
	if (e)
		fprintf(f, ": %s (%i)", strerror(e), e);
	fprintf(f, "\n");
	va_end(ap);
	exit(1);
}

static void usage(void)
{
	print(1,"Usage: %s [-x width] [-y height] [-s stride] [-f format] [inputfile] [outputfile]\n", name);
}

static const char *symbol_str(int id, const struct symbol_list list[])
{
	static char buffer[200];
	int i;

	for (i=0; list[i].symbol; i++)
		if (list[i].id == id)
			break;

	if (list[i].symbol) {
		if (id < 1000)
			sprintf(buffer, "%s [%i]", list[i].symbol, id);
		else
			sprintf(buffer, "%s [0x%08X]", list[i].symbol, id);
	} else {
		sprintf(buffer, "%i", id);
	}
	return buffer;
}

static void symbol_dump(const char *prefix, const struct symbol_list *list)
{
	int i;
	for (i = 0; list[i].symbol != NULL; i++)
		print(0, "%s%s [0x%08X]\n", prefix, list[i].symbol, list[i].id);
}

static int symbol_get(const struct symbol_list *list, const char **symbol)
{
	const char *start;
	const char *end;
	int r, i;

	if (!symbol || !symbol[0])
		error("symbol missing");

	start = *symbol;

	if (isdigit(start[0])) {
		r = strtol(start, (char **)&end, 0);
		if (end == start)
			error("zero-length symbol value");
	} else {
		end = start;
		while (isalnum(*end) || *end == '_') end++;
		if (start == end)
			error("zero-length symbol");
		if (!list)
			error("only numeric value allowed");
		for (i=0; list[i].symbol!=NULL; i++) {
			if (strncasecmp(start, list[i].symbol, end-start) == 0)
				break;
		}
		if (list[i].symbol == NULL)
			error("symbol `%s' not found", start);
		r = list[i].id;
	}

	*symbol = end;
	return r;
}

static long fsize(FILE *f)
{
	int i;
	long p, s, r = 0;

	p = ftell(f);
	if (p == -1)
		return -1;
	i = fseek(f, 0, SEEK_END);
	if (i == -1)
		r = -1;
	s = ftell(f);
	if (s == -1)
		r = -1;
	i = fseek(f, p, SEEK_SET);
	if (i != p)
		r = -1;

	return r == 0 ? s : r;
}

static void *duplicate_buffer(void *buffer, int size, int new_size)
{
	void *b = calloc(1, new_size);
	if (!b)
		error("out of memory, can not allocate %i bytes", new_size);
	memcpy(b, buffer, MIN(size, new_size));
	if (new_size > size)
		print(1, "warning: input buffer too small by %i bytes, setting the rest to zero\n",
			new_size - size);
	return b;
}

static void inline yuv_to_rgb(unsigned char rgb[3], int y, int cb, int cr)
{
	static const int R = 0;
	static const int G = 1;
	static const int B = 2;
	int u = cb;
	int v = cr;
#if 0
	/* http://www.fourcc.org/fccyvrgb.php */
	rgb[B] = CLAMPB(1.164*(y - 16)                   + 2.018*(u - 128));
	rgb[G] = CLAMPB(1.164*(y - 16) - 0.813*(v - 128) - 0.391*(u - 128));
	rgb[R] = CLAMPB(1.164*(y - 16) + 1.596*(v - 128));
#else
	/* http://en.wikipedia.org/wiki/YUV
	 * conversion from Y'UV to RGB (NTSC version): */
	int c = y - 16;
	int d = u - 128;
	int e = v - 128;
	rgb[R] = CLAMPB((298*c + 409*e + 128) >> 8);
	rgb[G] = CLAMPB((298*c - 100*d - 208*e + 128) >> 8);
	rgb[B] = CLAMPB((298*c + 516*d + 128) >> 8);
#endif
}

static inline unsigned int get_word(unsigned char *src)
{
	unsigned int lo = src[0];
	unsigned int hi = src[1];
	unsigned int w = (hi << 8) | lo;
	w >>= 2;		/* 10 bits per pixel to byte */
	return w;
}

/* Return 24 bits-per-pixel RGB image */
static int convert(void *in_buffer, int in_size, int width, int height, int stride, __u32 format, void *out_buffer)
{
	static const int dbpp = 3;
	int y, x, r, g, b;
	int oddrow, oddpix, initrow, initpix;
	unsigned char *src = NULL;
	unsigned char *s, *u;
	unsigned char *d = out_buffer;
	unsigned int dstride = width * dbpp;

	switch (format) {
	case V4L2_PIX_FMT_NV12:
		if (stride <= 0) stride = width;
		s = src = duplicate_buffer(in_buffer, in_size, stride * height*3/2);
		u = &s[height * stride];
		for (y = 0; y < height; y++) {
			unsigned char *s1 = s;
			unsigned char *u1 = u;
			unsigned char *d1 = d;
			for (x = 0; x < width; x++) {
				int b = *s1;
				int cb = u1[0];
				int cr = u1[1];
				yuv_to_rgb(d1, b, cb, cr);
				s1 += 1;
				d1 += dbpp;
				if (x & 1)
					u1 += 2;
			}
			s += stride;
			if (y & 1)
				u += stride;
			d += dstride;
		}
		break;

	case V4L2_PIX_FMT_GREY:
		if (stride <= 0) stride = width;
		s = src = duplicate_buffer(in_buffer, in_size, stride * height);
		for (y = 0; y < height; y++) {
			unsigned char *s1 = s;
			unsigned char *d1 = d;
			for (x = 0; x < width; x++) {
				int b = *s1;
				d1[0] = b;
				d1[1] = b;
				d1[2] = b;
				s1 += 1;
				d1 += dbpp;
			}
			s += stride;
			d += dstride;
		}
		break;

	case V4L2_PIX_FMT_SBGGR10:
	case V4L2_PIX_FMT_SGBRG10:
	case V4L2_PIX_FMT_SRGGB10:
	case V4L2_PIX_FMT_SGRBG10:		/* 2 bytes per pixel, little endian */
		if (format == V4L2_PIX_FMT_SBGGR10) { initrow = 1; initpix = 0; } else
		if (format == V4L2_PIX_FMT_SGBRG10) { initrow = 1; initpix = 1; } else
		if (format == V4L2_PIX_FMT_SRGGB10) { initrow = 0; initpix = 1; } else
		if (format == V4L2_PIX_FMT_SGRBG10) { initrow = 0; initpix = 0; }
		if (stride <= 0) stride = width * 2;
		s = src = duplicate_buffer(in_buffer, in_size, stride * height);
		r = g = b = 0;
		oddrow = initrow;
		for (y = 0; y < height; y++) {
			unsigned char *s1 = s;
			unsigned char *d1 = d;
			oddpix = initpix;
			for (x = 0; x < width; x++) {
				if (!oddrow && !oddpix) g = get_word(s1);
				if (!oddrow &&  oddpix) r = get_word(s1);
				if ( oddrow && !oddpix) b = get_word(s1);
				if ( oddrow &&  oddpix) g = get_word(s1);
				d1[0] = r;
				d1[1] = g;
				d1[2] = b;
				s1 += 2;
				d1 += dbpp;
				oddpix ^= 1;
			}
			s += stride;
			d += dstride;
			oddrow ^= 1;
		}
		break;

	default:
		errno = EINVAL;
		return -1;
	}

	free(src);
	return 0;
}

int main(int argc, char *argv[])
{
	char *in_name = NULL;
	char *out_name = NULL;
	void *in_buffer;
	void *out_buffer;
	int in_size, out_size;
	FILE *f;
	int i;

	int opt;
	int width = -1;
	int height = -1;
	int stride = -1;
	__u32 format = 0;

	while ((opt = getopt(argc, argv, "hf:x:y:s:")) != -1) {
		switch (opt) {
		case 'f': {
			const char *t = optarg;
			format = symbol_get(pixelformats, &t);
			break;
		}
		case 'x':
			width = atoi(optarg);
			break;
		case 'y':
			height = atoi(optarg);
			break;
		case 's':
			stride = atoi(optarg);
			break;
		default:
			usage();
			print(1, "Available formats:\n");
			symbol_dump(V4L2_PIX_FMT, pixelformats);
			return -1;
	        }
	}

	if (optind+2 > argc)
		error("input or output filename missing");

	in_name = argv[optind++];
	out_name = argv[optind++];

	print(1, "Reading file `%s', %ix%i stride %i format %s\n",
		in_name, width, height, stride, symbol_str(format, pixelformats));
	f = fopen(in_name, "rb");
	if (!f) error("failed opening file");
	in_size = fsize(f);
	if (in_size == -1) error("error checking file size");
	print(2, "File size %i bytes\n", in_size);
	in_buffer = malloc(in_size);
	if (!in_buffer) error("can not allocate input buffer");
	i = fread(in_buffer, in_size, 1, f);
	if (i != 1) error("failed reading file");
	fclose(f);

	out_size = width * height * 3;
	out_buffer = malloc(out_size);
	if (!out_buffer) error("can not allocate output buffer");
	i = convert(in_buffer, in_size, width, height, stride, format, out_buffer);
	if (i < 0) error("failed to convert image");
	free(in_buffer);

	print(1, "Writing file `%s', %i bytes\n", out_name, out_size);
	f = fopen(out_name, "wb");
	if (!f) error("failed opening file");
	i = fprintf(f, "P6\n%i %i 255\n", width, height);
	if (i < 0) error("can not write file header");
	i = fwrite(out_buffer, out_size, 1, f);
	if (i != 1) error("failed writing file");
	fclose(f);

	free(out_buffer);
	return 0;
}
