/*
 * Copyright (C) 2006 Christian Stigen Larsen, http://csl.sublevel3.org
 * Distributed under the GNU General Public License (GPL) v2 or later.
 *
 * Project homepage on http://jp2a.sf.net
 *
 * $Id$
 */

#include <stdio.h>
#include "jpeglib.h"

#ifdef HAVE_CONFIG_H

 // jpeglib (may) set this
 #ifdef HAVE_STDLIB_H
 #undef HAVE_STDLIB_H
 #endif

 #include "config.h"
#endif

#include <stdlib.h>
#include <string.h>

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#ifdef FEAT_CURL
 #ifdef HAVE_CURL_CURL_H
 #include "curl/curl.h"
 #endif

 #ifdef WIN32
 #include <io.h> // _pipe
 #include <fcntl.h> // O_BINARY
 #endif
#endif

#ifdef __MINGW32__
 #undef rpl_malloc
#endif

#define ROUND(x) (int) ( 0.5f + x )

const char* version   = PACKAGE_STRING;
const char* copyright = "Copyright (C) 2006 Christian Stigen Larsen";
const char* license   = "Distributed under the GNU General Public License (GPL) v2 or later.";
const char* url       = "http://jp2a.sf.net";

typedef struct Image_ {
	int width;
	int height;
	float *pixel;
	int *yadds;
	float resize_y;
	float resize_x;
	int *lookup_resx;
} Image;

// Options with defaults
int verbose = 0;
int auto_height = 1;
int auto_width = 0;
int width = 78;
int height = 0;
int progress_barlength = 56;
int border = 0;
int invert = 0;
int flipx = 0;
int flipy = 0;
int html = 0;
int html_fontsize = 4;
int debug = 0;

#define ASCII_PALETTE_SIZE 256

char ascii_palette[ASCII_PALETTE_SIZE+1] = "";
const char* default_palette = "   ...',;:clodxkO0KXNWM";


void print_version() {
	fprintf(stderr, "%s\n%s\n%s\n", version, copyright, license);
}

void help() {
	print_version();

	fputs("\n"
#ifdef FEAT_CURL
	"Usage: jp2a [ options ] [ file(s) | URL(s) ]\n\n"

	"Convert files or URLs from JPEG format to ASCII.\n\n"
#else
	"Usage: jp2a [ options ] [ file(s) ]\n\n"

	"Convert files in JPEG format to ASCII.\n\n"
#endif
	"OPTIONS\n"
	"  -                Read JPEG image from standard input.\n"
	"  -b, --border     Print a border around the output image.\n"
	"      --chars=...  Select character palette used to paint the image.\n"
	"                   Leftmost character corresponds to black pixel, right-\n"
	"                   most to white.  Minimum two characters must be specified.\n"
	"  -d, --debug      Print additional debug information.\n"
	"      --flipx      Flip image in X direction.\n"
	"      --flipy      Flip image in Y direction.\n"
	"      --height=N   Set output height, calculate width from aspect ratio.\n"
	"  -h, --help       Print program help.\n"
	"      --html       Produce strict XHTML 1.0 output.\n"
	"      --html-fontsize=N  Set fontsize to N pt when using --html, default is 4.\n"
	"  -i, --invert     Invert output image.  Use if your display has a dark\n"
	"                   background.\n"
	"      --size=WxH   Set output width and height.\n"
	"  -v, --verbose    Verbose output.\n"
	"  -V, --version    Print program version.\n"
	"      --width=N    Set output width, calculate height from ratio.\n\n"

	"  The default running mode is `jp2a --width=78'.  See the man page for jp2a\n"
	"  to see detailed usage examples.\n\n" , stderr);

	fprintf(stderr, "Project homepage on %s\n", url);
	fprintf(stderr, "Report bugs to <%s>\n", PACKAGE_BUGREPORT);
}

// returns positive error code, or -1 for parsing OK
int parse_options(const int argc, char** argv) {

	// define some shorthand defines
	#define IF_OPTS(shortopt, longopt) if ( !strcmp(s, shortopt) || !strcmp(s, longopt) )
	#define IF_OPT(shortopt) if ( !strcmp(s, shortopt) )
	#define IF_VARS(format, var1, var2) if ( sscanf(s, format, var1, var2) == 2 )
	#define IF_VAR(format, var) if ( sscanf(s, format, var) == 1 )

	int n, files;
	for ( n=1, files=0; n<argc; ++n ) {
		const char *s = argv[n];

		if ( *s != '-' ) { // count files to read
			++files; continue;
		}
	
		IF_OPT("-")			{ ++files; continue; }
		IF_OPTS("-h", "--help")		{ help(); return 0; }
		IF_OPTS("-v", "--verbose")	{ verbose = 1; continue; }
		IF_OPTS("-d", "--debug")	{ debug = 1; continue; }
		IF_OPT("--html") 		{ html = 1; continue; }
		IF_OPTS("-b", "--border") 	{ border = 1; continue; }
		IF_OPTS("-i", "--invert") 	{ invert = 1; continue; }
		IF_OPT("--flipx") 		{ flipx = 1; continue; }
		IF_OPT("--flipy") 		{ flipy = 1; continue; }
		IF_OPTS("-V", "--version")	{ print_version(); return 0; }
		IF_VAR("--width=%d", &width)	{ auto_height += 1; continue; }
		IF_VAR("--height=%d", &height)	{ auto_width += 1; continue; }
		IF_VAR("--html-fontsize=%d", &html_fontsize) { continue; }
		IF_VARS("--size=%dx%d", &width, &height) { auto_width = auto_height = 0; continue; }

		if ( !strncmp(s, "--chars=", 8) ) {
			if ( strlen(s-8) > ASCII_PALETTE_SIZE ) {
				fprintf(stderr, "Too many ascii characters specified.\n");
				return 1;
			}
	
			// don't use sscanf, we need to read spaces as well
			strcpy(ascii_palette, s+8);
			continue;
		}

		fprintf(stderr, "Unknown option %s\n\n", s);
		help();
		return 1;

	} // args ...

	if ( !files ) {
		fprintf(stderr, "No files specified.\n\n");
		help();
		return 1;
	}

	// only --width specified, calc width
	if ( auto_width==1 && auto_height == 1 )
		auto_height = 0;

	// --width and --height is the same as using --size
	if ( auto_width==2 && auto_height==1 )
		auto_width = auto_height = 0;

	if ( strlen(ascii_palette) < 2 ) {
		fprintf(stderr, "You must specify at least two characters in --chars.\n");
		return 1;
	}
	
	if ( (width < 1 && !auto_width) || (height < 1 && !auto_height) ) {
		fprintf(stderr, "Invalid width or height specified.\n");
		return 1;
	}

	return -1;
}

void calc_aspect_ratio(const int jpeg_width, const int jpeg_height) {

	// Calculate width or height, but not both

	if ( auto_width && !auto_height ) {
		width = ROUND(2.0f * (float) height * (float) jpeg_width / (float) jpeg_height);

		// adjust for too small dimensions	
		while ( width==0 ) {
			++height;
			calc_aspect_ratio(jpeg_width, jpeg_height);
		}
	}

	if ( !auto_width && auto_height ) {
		height = ROUND(0.5f * (float) width * (float) jpeg_height / (float) jpeg_width);

		// adjust for too small dimensions
		while ( height==0 ) {
			++width;
			calc_aspect_ratio(jpeg_width, jpeg_height);
		}
	}
}

void print_html_start() {
	printf("<?xml version='1.0' encoding='ISO-8859-1'?>\n"
	"<!DOCTYPE html PUBLIC '-//W3C//DTD XHTML 1.0 Strict//EN'"
	"  'http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd'>\n"
	"<html xmlns='http://www.w3.org/1999/xhtml' lang='en' xml:lang='en'>\n"
	"<head>\n"
	"<title>jp2a converted image</title>\n"
	"<style type='text/css'>\n"
	".ascii {\n"
	"   font-size:%dpt;\n"
	"}\n"
	"</style>\n"
	"</head>\n"
	"<body>\n"
	"<div class='ascii'>\n"
	"<pre>\n"
		, html_fontsize);
}

void print_html_end() {
	printf("</pre>\n");
	printf("</div>\n");
	printf("</body>\n");
	printf("</html>\n");
}

void print_border(const int width) {
	#ifdef WIN32
	char *bord = (char*) malloc(width+3);
	#else
	char bord[width + 3];
	#endif

	memset(bord, '-', width+2);
	bord[0] = bord[width+1] = '+';
	bord[width+2] = 0;
	puts(bord);

	#ifdef WIN32
	free(bord);
	#endif
}

void print_image(const Image* i, const int chars) {
	int x, y;
	const int w = i->width;
	const int h = i->height;

	#ifdef WIN32
	char *line = (char*) malloc(w+1);
	#else
	char line[w + 1];
	#endif

	line[w] = 0;

	for ( y=0; y < h; ++y ) {
		for ( x=0; x < w; ++x ) {
			float intensity = i->pixel[(!flipy? y : h-y-1)*w + x];
			int pos = ROUND( (float) chars * intensity );
			line[!flipx? x : w-x-1] = ascii_palette[ !invert ? chars - pos : pos ];
		}

		printf(!border? "%s\n" : "|%s|\n", line);
	}

	#ifdef WIN32
	free(line);
	#endif
}

void clear(Image* i) {
	memset(i->pixel, 0, i->width * i->height * sizeof(float));
	memset(i->yadds, 0, i->height * sizeof(int) );
}

void normalize(Image* i) {
	const int w = i->width;
	const int h = i->height;

	register int x, y, yoffs;

	for ( y=0, yoffs=0; y < h; ++y, yoffs += w )
	for ( x=0; x < w; ++x ) {
		if ( i->yadds[y] != 0 )
			i->pixel[yoffs + x] /= (float) i->yadds[y];
	}
}

void print_progress(const struct jpeg_decompress_struct* jpg) {
 	float progress = (float) (jpg->output_scanline + 1.0f) / (float) jpg->output_height;
	int pos = ROUND( (float) progress_barlength * progress );

	#ifdef WIN32
	char *s = (char*) malloc(progress_barlength + 1);
	#else
	char s[progress_barlength + 1];
	#endif

	memset(s, '.', progress_barlength);
	memset(s, '#', pos);

	s[progress_barlength] = 0;

	fprintf(stderr, "Decompressing image [%s]\r", s);

	#ifdef WIN32
	free(s);
	#endif
}

inline
void store_intensity(const JSAMPLE* source, float* dest, const int components) {
	float v = source[0];

	int c=1;
	while ( c < components )
		v += source[c++];

	v /= 255.0f * components;
	*dest += v;
}

void print_info(const struct jpeg_decompress_struct* cinfo) {
	fprintf(stderr, "Source width: %d\n", cinfo->output_width);
	fprintf(stderr, "Source height: %d\n", cinfo->output_height);
	fprintf(stderr, "Source color components: %d\n", cinfo->output_components);
	fprintf(stderr, "Output width: %d\n", width);
	fprintf(stderr, "Output height: %d\n", height);
	fprintf(stderr, "Output palette (%d chars): '%s'\n", (int) strlen(ascii_palette), ascii_palette);
}

#ifdef FEAT_CURL
//! return 1 if `s' is an URL, 0 if not
int is_url(const char* s) {
	int r = 0;
	r |= !strncmp(s, "ftp://", 6);
	r |= !strncmp(s, "ftps://", 7);
	r |= !strncmp(s, "file://", 7);
	r |= !strncmp(s, "http://", 7);
	r |= !strncmp(s, "tftp://", 7);
	r |= !strncmp(s, "https://", 8);
	return r;
}

// Fork and return filedescriptor of read-pipe for downloaded data
// Returns -1 in case of errors
// You must close() the filedescriptor after using it.
int curl_download(const char* url, const int debug) {
	int p, fd[2];

#ifdef WIN32
	if ( (p = _pipe(fd, 256, O_BINARY) ) != 0 ) {
#else
	if ( (p = pipe(fd)) != 0 ) {
#endif
		fprintf(stderr, "Could not create pipe (returned %d)\n", p);
		return -1;
	}

	pid_t pid;
	if ( (pid = fork()) == 0 ) {
		// CHILD process
		close(fd[0]); // close read end

		FILE *fw = fdopen(fd[1], "wb");

		if ( fw == NULL ) {
			fprintf(stderr, "Could not write to pipe\n");
			exit(1);
		}

		curl_global_init(CURL_GLOBAL_ALL);
		atexit(curl_global_cleanup);

		CURL *curl = curl_easy_init();
		curl_easy_setopt(curl, CURLOPT_URL, url);

		if ( debug )
			curl_easy_setopt(curl, CURLOPT_VERBOSE, 1);

		curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1); // fail silently on errors
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, fw);

		curl_easy_perform(curl);
		curl_easy_cleanup(curl);
		fflush(fw);
		fclose(fw);
		close(fd[1]);
		exit(0);
	} else if ( pid < 0 ) {
		fprintf(stderr, "Failed to fork\n");
		return -1;
	}

	// PARENT process

	close(fd[1]); // close write end of pipe
	return fd[0];
}
#endif

inline
void process_scanline(const struct jpeg_decompress_struct *jpg, const JSAMPLE* scanline, Image* image) {
	static int lasty = 0;
	const int y = ROUND(image->resize_y * (float) (jpg->output_scanline-1));

	// include all scanlines since last call
	while ( lasty <= y ) {
		const int y_w = lasty * image->width;
		int x;

		for ( x=0; x < image->width; ++x ) {
			store_intensity(&scanline[ image->lookup_resx[x] ],
				&image->pixel[y_w + x],
				jpg->out_color_components);
		}

		++image->yadds[lasty++];
	}

	lasty = y;
}

void free_image(Image* i) {
	if ( i->pixel ) free(i->pixel);
	if ( i->yadds ) free(i->yadds);
	if ( i->lookup_resx ) free(i->lookup_resx);
}

void malloc_image(Image* i) {
	i->pixel = NULL;
	i->yadds = NULL;
	i->lookup_resx = NULL;

	i->width = width;
	i->height = height;

	if ( (i->pixel = (float*) malloc(width * height * sizeof(float))) == NULL ) {
		fprintf(stderr, "Not enough memory for given output dimension\n");
		exit(1);
	}

	if ( (i->yadds = (int*) malloc(height * sizeof(int))) == NULL ) {
		fprintf(stderr, "Not enough memory for given output dimension (for yadds)\n");
		free_image(i);
		exit(1);
	}

	if ( (i->lookup_resx = (int*) malloc(width * sizeof(int))) == NULL ) {
		fprintf(stderr, "Not enough memory for given output dimension (lookup_resx)\n");
		free_image(i);
		exit(1);
	}
}

void init_image(Image *i, const struct jpeg_decompress_struct *jpg) {
	i->resize_y = (float) (i->height - 1) / (float) (jpg->output_height-1);
	i->resize_x = (float) jpg->output_width / (float) i->width;

	int dst_x;
	for ( dst_x=0; dst_x < i->width; ++dst_x ) {
		i->lookup_resx[dst_x] = (int)( (float) dst_x * i->resize_x );
		i->lookup_resx[dst_x] *= jpg->out_color_components;
	}
}

int decompress(FILE *fp) {
	struct jpeg_error_mgr jerr;
	struct jpeg_decompress_struct jpg;

	jpg.err = jpeg_std_error(&jerr);

	jpeg_create_decompress(&jpg);
	jpeg_stdio_src(&jpg, fp);
	jpeg_read_header(&jpg, TRUE);
	jpeg_start_decompress(&jpg);

	int row_stride = jpg.output_width * jpg.output_components;

	JSAMPARRAY buffer = (*jpg.mem->alloc_sarray)
		((j_common_ptr) &jpg, JPOOL_IMAGE, row_stride, 1);

	calc_aspect_ratio(jpg.output_width, jpg.output_height);

	Image image;
	malloc_image(&image);
	clear(&image);

	if ( verbose ) print_info(&jpg);

	init_image(&image, &jpg);

	while ( jpg.output_scanline < jpg.output_height ) {
		jpeg_read_scanlines(&jpg, buffer, 1);
		process_scanline(&jpg, buffer[0], &image);

		if ( verbose ) print_progress(&jpg);
	}

	if ( verbose ) fprintf(stderr, "\n");

	normalize(&image);

	if ( html ) print_html_start();
	if ( border ) print_border(image.width);

	print_image(&image, strlen(ascii_palette) - 1);

	if ( border ) print_border(image.width);
	if ( html ) print_html_end();

	free_image(&image);

	jpeg_finish_decompress(&jpg);
	jpeg_destroy_decompress(&jpg);

	return 0;
}

int main(int argc, char** argv) {
	strcpy(ascii_palette, default_palette);
	int r = parse_options(argc, argv);
	if ( r >= 0 ) return r;

	int n;
	for ( n=1; n<argc; ++n ) {

		// Skip options
		if ( argv[n][0]=='-' && argv[n][1]!=0 )
			continue;

		// Read from stdin
		if ( argv[n][0]=='-' && argv[n][1]==0 ) {
			int r = decompress(stdin);
			if ( r == 0 )
				continue;
		}

#ifdef FEAT_CURL
		if ( is_url(argv[n]) ) {
			if ( verbose )
				fprintf(stderr, "URL: %s\n", argv[n]);

			int fd;
			if ( (fd = curl_download(argv[n], debug)) < 0 )
				return 1;

			FILE *fr;
			if ( (fr = fdopen(fd, "rb")) == NULL ) {
				fprintf(stderr, "Could not fdopen read pipe\n");
				return 1;
			}

			int r = decompress(fr);

			fclose(fr);
			close(fd);

			if ( r != 0 ) return r;
			continue;
		}
#endif

		FILE *fp;
		if ( (fp = fopen(argv[n], "rb")) != NULL ) {

			if ( verbose )
				fprintf(stderr, "File: %s\n", argv[n]);

			int r = decompress(fp);
			fclose(fp);

			if ( r != 0 )
				return r;
		} else {
			fprintf(stderr, "Can't open %s\n", argv[n]);
			return 1;
		}
	}

	return 0;
}
