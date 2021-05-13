#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <fcntl.h>
#include <sys/inotify.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <time.h>
#include <limits.h>
#include <errno.h>
#include <error.h>
#include <string.h>
#include <termios.h>

#include "grapher.h"

#define MAXLOGSZ		(64*1024*1024)
#define MAXLINESZ		1024

#define WAIT_BETWEEN_SELECT_US	1000000L

#define	MAXHIST			( 4 * 24 * 7 )	// A week's worth of quarter-hours.
#define MAXENTR			( 12 * 15 )	// We expect 6 per minute, worst-case: 12 per min, 180 per quarter-hr.

typedef struct quarterhr
{
	time_t	stamps[ MAXENTR ];
	int	eligib[ MAXENTR ];
	int	proofs[ MAXENTR ];
	float	durati[ MAXENTR ];
	int	sz;
	time_t	timelo;
	time_t	timehi;
} quarterhr_t;


quarterhr_t quarters[ MAXHIST ];

static int entries_added=0;	// How many log entries have we added in total?

static time_t newest_stamp=0;	// The stamp of the latest entry.

static time_t refresh_stamp=0;	// When did we update the image, last?

static struct termios orig_termios;

static const uint8_t ramp[256][3] =
{
0,0,0, 68,2,85, 68,3,87, 69,5,88, 69,6,90, 69,8,91,
70,9,92, 70,11,94, 70,12,95, 70,14,97, 71,15,98, 71,17,99,
71,18,101, 71,20,102, 71,21,103, 71,22,105, 71,24,106, 72,25,107,
72,26,108, 72,28,110, 72,29,111, 72,30,112, 72,32,113, 72,33,114,
72,34,115, 72,35,116, 71,37,117, 71,38,118, 71,39,119, 71,40,120,
71,42,121, 71,43,122, 71,44,123, 70,45,124, 70,47,124, 70,48,125,
70,49,126, 69,50,127, 69,52,127, 69,53,128, 69,54,129, 68,55,129,
68,57,130, 67,58,131, 67,59,131, 67,60,132, 66,61,132, 66,62,133,
66,64,133, 65,65,134, 65,66,134, 64,67,135, 64,68,135, 63,69,135,
63,71,136, 62,72,136, 62,73,137, 61,74,137, 61,75,137, 61,76,137,
60,77,138, 60,78,138, 59,80,138, 59,81,138, 58,82,139, 58,83,139,
57,84,139, 57,85,139, 56,86,139, 56,87,140, 55,88,140, 55,89,140,
54,90,140, 54,91,140, 53,92,140, 53,93,140, 52,94,141, 52,95,141,
51,96,141, 51,97,141, 50,98,141, 50,99,141, 49,100,141, 49,101,141,
49,102,141, 48,103,141, 48,104,141, 47,105,141, 47,106,141, 46,107,142,
46,108,142, 46,109,142, 45,110,142, 45,111,142, 44,112,142, 44,113,142,
44,114,142, 43,115,142, 43,116,142, 42,117,142, 42,118,142, 42,119,142,
41,120,142, 41,121,142, 40,122,142, 40,122,142, 40,123,142, 39,124,142,
39,125,142, 39,126,142, 38,127,142, 38,128,142, 38,129,142, 37,130,142,
37,131,141, 36,132,141, 36,133,141, 36,134,141, 35,135,141, 35,136,141,
35,137,141, 34,137,141, 34,138,141, 34,139,141, 33,140,141, 33,141,140,
33,142,140, 32,143,140, 32,144,140, 32,145,140, 31,146,140, 31,147,139,
31,148,139, 31,149,139, 31,150,139, 30,151,138, 30,152,138, 30,153,138,
30,153,138, 30,154,137, 30,155,137, 30,156,137, 30,157,136, 30,158,136,
30,159,136, 30,160,135, 31,161,135, 31,162,134, 31,163,134, 32,164,133,
32,165,133, 33,166,133, 33,167,132, 34,167,132, 35,168,131, 35,169,130,
36,170,130, 37,171,129, 38,172,129, 39,173,128, 40,174,127, 41,175,127,
42,176,126, 43,177,125, 44,177,125, 46,178,124, 47,179,123, 48,180,122,
50,181,122, 51,182,121, 53,183,120, 54,184,119, 56,185,118, 57,185,118,
59,186,117, 61,187,116, 62,188,115, 64,189,114, 66,190,113, 68,190,112,
69,191,111, 71,192,110, 73,193,109, 75,194,108, 77,194,107, 79,195,105,
81,196,104, 83,197,103, 85,198,102, 87,198,101, 89,199,100, 91,200,98,
94,201,97, 96,201,96, 98,202,95, 100,203,93, 103,204,92, 105,204,91,
107,205,89, 109,206,88, 112,206,86, 114,207,85, 116,208,84, 119,208,82,
121,209,81, 124,210,79, 126,210,78, 129,211,76, 131,211,75, 134,212,73,
136,213,71, 139,213,70, 141,214,68, 144,214,67, 146,215,65, 149,215,63,
151,216,62, 154,216,60, 157,217,58, 159,217,56, 162,218,55, 165,218,53,
167,219,51, 170,219,50, 173,220,48, 175,220,46, 178,221,44, 181,221,43,
183,221,41, 186,222,39, 189,222,38, 191,223,36, 194,223,34, 197,223,33,
199,224,31, 202,224,30, 205,224,29, 207,225,28, 210,225,27, 212,225,26,
215,226,25, 218,226,24, 220,226,24, 223,227,24, 225,227,24, 228,227,24,
231,228,25, 233,228,25, 236,228,26, 238,229,27, 241,229,28, 243,229,30,
246,230,31, 248,230,33, 250,230,34, 253,231,36,
};


static void init_quarters( time_t now )
{
	time_t q = now / 900;
	time_t q_lo = (q+0) * 900;
	time_t q_hi = (q+1) * 900;
	for ( int i=MAXHIST-1; i>=0; --i )	// [0..MAXHIST)
	{
		const int ir = MAXHIST-1-i;	// [MAXHIST-1..0]
		quarters[i].sz = 0;
		quarters[i].timelo = q_lo - 900 * ir;
		quarters[i].timehi = q_hi - 900 * ir;
	}
}


static void shift_quarters( void )
{
	fprintf( stderr, "Shifting quarters...\n" );
	for ( int i=0; i<MAXHIST-1; ++i )
		quarters[i] = quarters[i+1];
	const int last = MAXHIST-1;
	memset( quarters+last, 0, sizeof(quarterhr_t) );
	quarters[ last ].timelo = quarters[ last-1 ].timelo + 900;
	quarters[ last ].timehi = quarters[ last-1 ].timehi + 900;
}


static int too_old( time_t t )
{
	return t < quarters[ 0 ].timelo;
}


static int too_new( time_t t )
{
	const int last = MAXHIST-1;
	return t >= quarters[last].timehi;
}


static int quarterslot( time_t tim )
{
	const int last = MAXHIST-1;
	const time_t d = tim - quarters[last].timehi;
	if ( d >= 0 )
		return INT_MAX;
	return MAXHIST - 1 + ( d / 900 );
}


static int add_entry( time_t t, int eligi, int proof, float durat )
{
	while ( too_new( t ) )
		shift_quarters();
	if ( too_old( t ) )
		return 0;
	int s = quarterslot( t );
	assert( s>=0 );
	assert( s<MAXHIST );
	const int i = quarters[s].sz;
	assert( i < MAXENTR );
	quarters[s].stamps[i] = t;
	quarters[s].eligib[i] = eligi;
	quarters[s].proofs[i] = proof;
	quarters[s].durati[i] = durat;
	quarters[s].sz += 1;
	return 1;
}


static FILE* f_log = 0;


static FILE* open_log_file(const char* dirname)
{
	if ( f_log )
		fclose( f_log );
	char fname[PATH_MAX+1];
	snprintf( fname, sizeof(fname), "%s/debug.log", dirname );
	f_log = fopen( fname, "rb" );
	if ( !f_log )
	{
		fprintf( stderr, "Failed to open log file '%s'\n", fname );
		return 0;
	}

#if 0	// No need for non blocking IO.
	const int fd = fileno( f_log );
	assert( fd );
	int flags = fcntl( fd, F_GETFL, 0 );
	fcntl( fd, F_SETFL, flags | O_NONBLOCK );
#endif

	return f_log;
}

// Parses log entries that look like this:
// 2021-05-13T09:14:35.538 harvester chia.harvester.harvester: INFO     0 plots were eligible for farming c1c8456f7a... Found 0 proofs. Time: 0.00201 s. Total 36 plots

static void analyze_line(const char* line, ssize_t length)
{
	if ( length > 60 )
	{
		if ( !strncmp( line+24, "harvester ", 10 ) )
		{
			int year=-1;
			int month=-1;	
			int day=-1;
			int hours=-1;
			int minut=-1;
			float secon=-1;
			int eligi = -1;
			int proof = -1;
			float durat = -1.0f;
			int plots = -1;
			char key[128];
			const int num = sscanf
			(
				line,
				"%04d-%02d-%02dT%02d:%02d:%f harvester chia.harvester.harvester: INFO "
				"%d plots were eligible for farming %s Found %d proofs. Time: %f s. Total %d plots",
				&year,
				&month,
				&day,
				&hours,
				&minut,
				&secon,
				&eligi,
				key,
				&proof,
				&durat,
				&plots
			);
			if ( num == 11 )
			{
				struct tm tim =
				{
					(int)secon,	// seconds 0..60
					minut,		// minutes 0..59
					hours,		// hours 0..23
					day,		// day 1..31
					month-1,	// month 0..11
					year-1900,	// year - 1900
					-1,
					-1,
					-1
				};
				const time_t logtim = mktime( &tim );
				assert( logtim != (time_t) -1 );

				if ( logtim > newest_stamp )
				{
					const int added = add_entry( logtim, eligi, proof, durat );
					if ( added )
						newest_stamp = logtim;
					entries_added += added;
				}
				else
				{
					// Sometimes a whole bunch of harvester runs are done in the very same second. Why?
					//fprintf(stderr, "Spurious entry: %s", line);
				}
			}
		}
	}
}


static int read_log_file(void)
{
	assert( f_log );
	static char* line = 0;
	static size_t linesz=MAXLINESZ;
	if ( !line )
		line = (char*)malloc(MAXLINESZ);

	int linesread = 0;

	do
	{
		struct timeval tv = { 0L, WAIT_BETWEEN_SELECT_US };
		fd_set rdset;
		FD_ZERO(&rdset);
		int log_fds = fileno( f_log );
		FD_SET( log_fds, &rdset );
		const int ready = select( log_fds+1, &rdset, NULL, NULL, &tv);

		if ( ready < 0 )
			error( EXIT_FAILURE, errno, "select() failed" );

		if ( ready == 0 )
		{
			//fprintf( stderr, "No descriptors ready for reading.\n" );
			return linesread;
		}

		const ssize_t ll = getline( &line, &linesz, f_log );
		if ( ll <= 0 )
		{
			//fprintf( stderr, "getline() returned %zd\n", ll );
			clearerr( f_log );
			return linesread;
		}

		linesread++;
		analyze_line( line, ll );
	} while(1);
}


static void draw_column( int nr, uint32_t* img, int h )
{
	const int q = MAXHIST-1-nr;
	if ( q<0 )
		return;
	const time_t qlo = quarters[q].timelo;
	const int sz = quarters[q].sz;
	for ( int y=0; y<h; ++y )
	{
		const time_t s0 = qlo + 900 * (y+0) / h;
		const time_t s1 = qlo + 900 * (y+1) / h;
		int checks=0;
		int eligib=0;
		for ( int i=0; i<sz; ++i )
		{
			const time_t t = quarters[q].stamps[i];
			if ( t >= s0 && t < s1 )
			{
				checks++;
				eligib += quarters[q].eligib[i];
			}
		}
		const time_t span = s1-s0;
		const float expected = span * (0.1f);
		float achieved = checks / expected;
		achieved = achieved > 1.0f ? 1.0f : achieved;
		const uint8_t idx = (uint8_t) ( achieved * 255 );
		const uint32_t c = (0xff<<24) | (ramp[idx][2]<<16) | (ramp[idx][1]<<8) | (ramp[idx][0]<<0);
		img[ y*imw ] = c;
	}
}


static int update_image(void)
{
	int redraw=0;

	if ( grapher_resized )
	{
		grapher_adapt_to_new_size();
		redraw=1;
	}

	// Compose the image.
	if ( newest_stamp > refresh_stamp )
		redraw=1;

	if (redraw)
	{
		for ( int col=0; col<imw-2; ++col )
		{
			draw_column( col, im + (1*imw) + (imw-2-col), imh-2 );
		}
		grapher_update();
		refresh_stamp = newest_stamp;
	}
	return 0;
}


static void disableRawMode()
{
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}


static void enableRawMode()
{
	tcgetattr(STDIN_FILENO, &orig_termios);
	atexit(disableRawMode);
	struct termios raw = orig_termios;
	raw.c_lflag &= ~(ECHO);				// Don't echo key presses.
	raw.c_lflag &= ~(ICANON);			// Read by char, not by line.
	raw.c_cc[VMIN] = 0;				// No minimum nr of chars.
	raw.c_cc[VTIME] = 0;				// No waiting time.
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}


int main(int argc, char *argv[])
{
	const char* dirname = 0;

	if (argc != 2)
	{
		fprintf( stderr, "Usage: %s ~/.chia/mainet/log\n", argv[0] );
		exit( 1 );
	}
	else
	{
		dirname = argv[ 1 ];
	}
	fprintf( stderr, "Monitoring directory %s\n", dirname );

	init_quarters( time(0) );

	if ( open_log_file( dirname ) )
	{
		// Log file exists, we should read what is in it, currently.
		const int numl = read_log_file();
		fprintf( stderr, "read %d lines from log.\n", numl );
	}

	int fd;
	if ( (fd = inotify_init()) < 0 )
		error( EXIT_FAILURE, errno, "failed to initialize inotify instance" );

	int wd;
	if ( (wd = inotify_add_watch ( fd, dirname, IN_MODIFY | IN_CREATE | IN_DELETE ) ) < 0 )
		error( EXIT_FAILURE, errno, "failed to add inotify watch for '%s'", dirname );


	int result = grapher_init();
	if ( result < 0 )
	{
		fprintf( stderr, "Failed to intialize grapher(), maybe we are not running in a terminal?\n" );
		exit(2);
	}

	enableRawMode();

	// Read notifications.
	char buf[ sizeof(struct inotify_event) + PATH_MAX ];
	int len;
	while ( (len = read(fd, buf, sizeof(buf))) > 0 )
	{
		int i = 0;
		while (i < len)
		{
			struct inotify_event *ie = (struct inotify_event*) &buf[i];
			if ( ie->mask & IN_CREATE )
			{
				// A file got created. It could be our new log file!
				if ( !strcmp( ie->name, "debug.log" ) )
				{
					fprintf( stderr, "Reopening logfile.\n" );
					open_log_file( dirname );
					const int numl = read_log_file();
					fprintf( stderr, "read %d lines from log.\n", numl );
				}
			}
			else if ( ie->mask & IN_MODIFY )
			{
				if ( !strcmp( ie->name, "debug.log" ) )
				{
					//fprintf( stderr, "Modified.\n" );
					const int numl = read_log_file();
					(void) numl;
					//fprintf( stderr, "read %d lines from log.\n", numl );
				}
			}
			else if (ie->mask & IN_DELETE)
			{
				printf("%s was deleted\n",  ie->name);
			}

			i += sizeof(struct inotify_event) + ie->len;
		}

		update_image();

		static int done=0;
		char c;
		const int numr = read( STDIN_FILENO, &c, 1 );
		if ( numr == 1 && ( c == 27 || c == 'q' || c == 'Q' ) )
			done=1;

		if ( done )
		{
			grapher_exit();
			exit(0);
		}
	}

	error(EXIT_FAILURE, len == 0 ? 0 : errno, "failed to read inotify event");
}

