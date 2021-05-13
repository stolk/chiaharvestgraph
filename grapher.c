// grapher.c
//
// by Abraham Stolk.

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <math.h>
#include <time.h>
#include <unistd.h>
#include <string.h>
#include <inttypes.h>
#include <signal.h>
#include <termios.h>
#include <errno.h>
#include <sys/ioctl.h>

#include "grapher.h"

static int termw = 0, termh = 0;

int imw = 0;
int imh = 0;
uint32_t* im = 0;
char* overlay = 0;

char postscript[256];

int grapher_resized = 1;


static void get_terminal_size(void)
{
	struct winsize tmp;
	ioctl(STDOUT_FILENO, TIOCGWINSZ, &tmp);
	termw = tmp.ws_col;
	termh = tmp.ws_row;
}


static void setup_image(void)
{
	if (im) free(im);
	if (overlay) free(overlay);

	imw = termw;
	imh = 2 * (termh-1);
	const size_t sz = imw * imh * 4;
	im = (uint32_t*) malloc(sz);
	memset( im, 0x00, sz );

	overlay = (char*) malloc( imw * (imh/2) );
	memset( overlay, 0x00, imw * (imh/2) );

	// Draw border into image.
	for ( int y = 0; y<imh; ++y )
		for ( int x = 0; x<imw; ++x )
		{
			uint32_t b = 0x30; //  + (y/4) * 0xff / imh;
			uint32_t g = b;
			uint32_t r = b;
			uint32_t a = 0xff;
			uint32_t colour = a<<24 | b<<16 | g<<8 | r<<0;
			im[y * imw + x] = x == 0 || x == imw - 1 || y == 0 || y == imh - 1 ? colour : 0x0;
		}
}


static void sigwinchHandler(int sig)
{
	grapher_resized = 1;
}




#define RESETALL  	"\x1b[0m"

#define CURSORHOME	"\x1b[H"

#define CLEARSCREEN	"\e[H\e[2J\e[3J"

#define SETFG		"\x1b[38;2;"

#define SETBG		"\x1b[48;2;"


#define HALFBLOCK "▀"		// Uses Unicode char U+2580



static void print_image_double_res( int w, int h, unsigned char* data, char* overlay )
{
	if ( h & 1 )
		h--;
	const int linesz = 32768;
	char line[ linesz ];

	for ( int y = 0; y<h; y += 2 )
	{
		const unsigned char* row0 = data + (y + 0) * w * 4;
		const unsigned char* row1 = data + (y + 1) * w * 4;
		line[0] = 0;
		for ( int x = 0; x<w; ++x )
		{
			char overlaychar = overlay ? *overlay++ : 0;
			// foreground colour.
			strncat( line, "\x1b[38;2;", sizeof(line) - strlen(line) - 1 );
			char tripl[80];
			unsigned char r = *row0++;
			unsigned char g = *row0++;
			unsigned char b = *row0++;
			unsigned char a = *row0++;
			if ( overlaychar ) r = g = b = a = 0xff;
			snprintf( tripl, sizeof(tripl), "%d;%d;%dm", r,g,b );
			strncat( line, tripl, sizeof(line) - strlen(line) - 1 );
			// background colour.
			strncat( line, "\x1b[48;2;", sizeof(line) - strlen(line) - 1 );
			r = *row1++;
			g = *row1++;
			b = *row1++;
			a = *row1++;
			if ( overlaychar ) r = g = b = a = 0x00;
			if ( overlaychar )
				snprintf( tripl, sizeof(tripl), "%d;%d;%dm%c", r,g,b,overlaychar );
			else
				snprintf( tripl, sizeof(tripl), "%d;%d;%dm" HALFBLOCK, r,g,b );
			strncat( line, tripl, sizeof(line) - strlen(line) - 1 );
		}
		strncat( line, RESETALL, sizeof(line) - strlen(line) - 1 );
		if ( y == h - 1 )
			printf( "%s", line );
		else
			puts( line );
	}
}



int grapher_init( void )
{
	if ( system("tty -s 1> /dev/null 2> /dev/null") )
		return -1;

	// Listen to changes in terminal size
	struct sigaction sa;
	sigemptyset( &sa.sa_mask );
	sa.sa_flags = 0;
	sa.sa_handler = sigwinchHandler;
	if ( sigaction( SIGWINCH, &sa, 0 ) == -1 )
		perror( "sigaction" );

	return 0;
}


void grapher_adapt_to_new_size(void)
{
	printf(CLEARSCREEN);
	get_terminal_size();
	setup_image();
	grapher_resized = 0;
}


void grapher_update( void )
{
	printf( CURSORHOME );
	print_image_double_res( imw, imh, (unsigned char*) im, overlay );

//	printf( "%s", postscript );
//	fflush( stdout );
}


void grapher_exit(void)
{
	free(im);
	printf( CLEARSCREEN );
}


