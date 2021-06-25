// chiaharvestgraph.c
//
// (c)2021 by Abraham Stolk.
// XCH Donations: xch1zfgqfqfdse3e2x2z9lscm6dx9cvd5j2jjc7pdemxjqp0xp05xzps602592

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <math.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/inotify.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <time.h>
#include <limits.h>
#include <errno.h>
#include <error.h>
#include <string.h>
#include <termios.h>

#include "grapher.h"
#include "colourmaps.h"


#define MAXLINESZ		1024

#define WAIT_BETWEEN_SELECT_US	500000L

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

static const rgb_t* ramp=0;

static double total_response_time_eligible=0.0;
static double worst_response_time_eligible=0.0;
static int total_eligible_responses=0;
static int plotcount=-1;
static time_t oldeststamp;


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
	return t <= quarters[ 0 ].timelo;
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
	const int slot = (int) ( MAXHIST - 1 + ( d / 900 ) );
	if ( slot < 0 )
	{
		fprintf
		(
			stderr,
			"ERROR - UNEXPECTED TIME VALUE.\n"
			"tim=%zd lasttimehi=%zd d=%zd slot=%d\n"
			"REPORT THIS MESSAGE TO %s\n",
			tim, quarters[last].timehi, d, slot,
			"https://github.com/stolk/chiaharvestgraph/issues/12"
		);
	}
	return slot;
}


static int add_entry( time_t t, int eligi, int proof, float durat, int plots )
{
	while ( too_new( t ) )
		shift_quarters();
	if ( too_old( t ) )
		return 0;	// signal not adding.
	int s = quarterslot( t );
	if ( s < 0 || s >= MAXHIST )
		return -1;	// signal failure.
	const int i = quarters[s].sz;
	assert( i < MAXENTR );
	quarters[s].stamps[i] = t;
	quarters[s].eligib[i] = eligi;
	quarters[s].proofs[i] = proof;
	quarters[s].durati[i] = durat;
	quarters[s].sz += 1;

	if ( eligi > 0 )
	{
		total_response_time_eligible += durat;
		worst_response_time_eligible = durat > worst_response_time_eligible ? durat : worst_response_time_eligible;
		total_eligible_responses += 1;
	}
	if ( plotcount == -1 )
		oldeststamp = t;
	plotcount = plots;
	oldeststamp = t < oldeststamp ? t : oldeststamp;
	return 1;
}


static FILE* f_log = 0;


static FILE* open_log_file(const char* dirname, const char* logname)
{
	if ( f_log )
		fclose( f_log );
	if ( !logname )
		logname = "debug.log";

	char fname[PATH_MAX+1];
	snprintf( fname, sizeof(fname), "%s/%s", dirname, logname );
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
			char crypto[128];
			int eligi = -1;
			int proof = -1;
			float durat = -1.0f;
			int plots = -1;
			char key[128];
			const int num = sscanf
			(
				line,
				"%04d-%02d-%02dT%02d:%02d:%f harvester %[^.].harvester.harvester: INFO "
				"%d plots were eligible for farming %s Found %d proofs. Time: %f s. Total %d plots",
				&year,
				&month,
				&day,
				&hours,
				&minut,
				&secon,
				crypto,
				&eligi,
				key,
				&proof,
				&durat,
				&plots
			);
			if ( num == 12 )
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
					const int added = add_entry( logtim, eligi, proof, durat, plots );
					if ( added < 0)
					{
						fprintf( stderr, "OFFENDING LOG LINE: %s\n", line );
						exit(3); // Stop right there, so the user can see the message.
					}
					if ( added > 0)
					{
						newest_stamp = logtim;
						entries_added += added;
					}
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
		else if ( ll <= 200)
		{
			analyze_line( line, ll );
		}
		linesread++;
	} while(1);
}


static void draw_column( int nr, uint32_t* img, int h, time_t now )
{
	const int q = MAXHIST-1-nr;
	if ( q<0 )
		return;
	const time_t qlo = quarters[q].timelo;
	const int sz = quarters[q].sz;
	const int band = ( ( qlo / 900 / 4 ) & 1 );
	for ( int y=0; y<h; ++y )
	{
		const int y0 = y>0   ?  y-1 : y+0;
		const int y1 = y<h-1 ?  y+2 : y+1;
		const time_t r0 = qlo + 900 * (y0 ) / h;
		const time_t r1 = qlo + 900 * (y1 ) / h;
		const time_t s0 = qlo + 900 * (y+0) / h;
		const time_t s1 = qlo + 900 * (y+1) / h;

		int checks=0;
		int eligib=0;
		int proofs=0;
		for ( int i=0; i<sz; ++i )
		{
			const time_t t = quarters[q].stamps[i];
			if ( t >= r0 && t < r1 )
				checks++;
			if ( t >= s0 && t < s1 )
			{
				eligib += quarters[q].eligib[i];
				proofs += quarters[q].proofs[i];
			}
		}
		const time_t span = r1-r0;
		const float nominalcheckspersecond = 9.375f;
		const float nominalsecondspercheck = 1 / nominalcheckspersecond;
		const float expected = span * nominalsecondspercheck;
		float achieved = 0.73f * checks / expected;
		achieved = achieved > 1.0f ? 1.0f : achieved;
		const uint8_t idx = (uint8_t) ( achieved * 255 );
		uint32_t red = ramp[idx][0];
		uint32_t grn = ramp[idx][1];
		uint32_t blu = ramp[idx][2];
		if ( s0 < oldeststamp || s1 > now )
		{
			red = grn = blu = 0x36;
		}
		if ( band )
		{
			red = red * 200 / 255;
			grn = grn * 200 / 255;
			blu = blu * 200 / 255;
		}
		if ( proofs )
		{
			// Eureka! We found a proof, and will probably get paid sweet XCH!
			red=0x40; grn=0x40; blu=0xff;
		}
		const uint32_t c = (0xff<<24) | (blu<<16) | (grn<<8) | (red<<0);
		img[ y*imw ] = c;
	}
}


static void setup_postscript(void)
{
	uint8_t c0[3] = {0xf0,0x00,0x00};
	uint8_t c1[3] = {0xf0,0xa0,0x00};
	uint8_t c2[3] = {0xf0,0xf0,0x00};
	uint8_t c3[3] = {0x40,0x40,0xff};
	const char* l0 = "RED: NO-HARVEST ";
	const char* l1 = "ORA: UNDER-HARVEST ";
	const char* l2 = "YLW: NOMINAL ";
	const char* l3 = "BLU: PROOF ";

	if ( ramp != cmap_heat )
	{
		c0[0] = ramp[  2][0]; c0[1] = ramp[  2][1]; c0[2] = ramp[  2][2];
		c1[0] = ramp[120][0]; c1[1] = ramp[120][1]; c1[2] = ramp[120][2];
		c2[0] = ramp[230][0]; c2[1] = ramp[230][1]; c2[2] = ramp[230][2];
		l0 = "NO-HARVEST  ";
		l1 = "UNDER-HARVEST  ";
		l2 = "NOMINAL  ";
		l3 = "PROOF  ";
		if ( ramp == cmap_viridis ) c3[0] = c3[1] = c3[2] = 0xff;
		if ( ramp == cmap_magma   ) { c3[0] = 0x00; c3[1] = 0xff; c3[2] = 0x00; }
		if ( ramp == cmap_plasma  ) { c3[0] = 0x00; c3[1] = 0xb0; c3[2] = 0xff; }
	}

	snprintf
	(
		postscript,
		sizeof(postscript),

		SETFG "%d;%d;%dm" SETBG "%d;%d;%dm%s"
		SETFG "%d;%d;%dm" SETBG "%d;%d;%dm%s"
		SETFG "%d;%d;%dm" SETBG "%d;%d;%dm%s"
		SETFG "%d;%d;%dm" SETBG "%d;%d;%dm%s"
		SETFG "255;255;255m",

		c0[0],c0[1],c0[2], 0,0,0, l0,
		c1[0],c1[1],c1[2], 0,0,0, l1,
		c2[0],c2[1],c2[2], 0,0,0, l2,
		c3[0],c3[1],c3[2], 0,0,0, l3
	);
}


static void setup_scale(void)
{
	strncpy( overlay + 2*imw - 4, "NOW", 4 );

	int x = 2*imw - 8;
	int h = 0;
	while( x >= imw )
	{
		char lab[8] = {0,0,0,0, 0,0,0,0};

		if ( h<12 )
			snprintf( overlay+x, sizeof(lab), "%2dh",  h+1);
		else if ( h%24==0 )
			snprintf( overlay+x, sizeof(lab), "%dDAY", h/24);

		x -= 4;
		h += 1;
	}
}


static void place_stats_into_overlay(void)
{
	double avg = total_response_time_eligible / total_eligible_responses;
	int avgms   = (int)round(avg * 1000);
	int worstms = (int)round(worst_response_time_eligible * 1000);

	const char* q_av = 0;
	if ( avgms < 80 )
		q_av = "fast";
	else if ( avgms < 300 )
		q_av = "ok";
	else
		q_av = "slow";

	const char* q_wo = 0;
	if ( worstms < 1000 )
		q_wo = "fast";
	else if ( worstms < 2000 )
		q_wo = "ok";
	else if ( worstms < 5000 )
		q_wo = "slow";
	else
		q_wo = "too-slow";

	snprintf
	(
		overlay+0,
		imw,
		"PLOTS:%d  AVG-CHECK:%dms[%s]  SLOWEST-CHECK:%dms[%s]   ",
		plotcount,
		avgms, q_av,
		worstms, q_wo
	);
}


static int update_image(void)
{
	int redraw=0;

	if ( grapher_resized )
	{
		grapher_adapt_to_new_size();
		setup_scale();
		redraw=1;
	}

	// Compose the image.
	if ( newest_stamp > refresh_stamp )
		redraw=1;

	if ( time(0) > refresh_stamp )
		redraw=1;

	if (redraw)
	{
		time_t now = time(0);
		for ( int col=0; col<imw-2; ++col )
		{
			draw_column( col, im + (5*imw) + (imw-2-col), imh-6, now );
		}
		place_stats_into_overlay();
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
		fprintf( stderr, "Usage: %s ~/.chia/mainnet/log\n", argv[0] );
		exit( 1 );
	}
	else
	{
		dirname = argv[ 1 ];
	}

	DIR* dir = opendir(dirname);
	if ( !dir )
	{
		if ( errno == ENOTDIR )
		{
			fprintf(stderr, "%s is not a directory.\n", dirname );
			exit(2);
		}
		error( EXIT_FAILURE, errno, "failed to open directory." );
	}
	else
	{
		closedir(dir);
		dir=0;
	}

	fprintf( stderr, "Monitoring directory %s\n", dirname );

	const int viridis = ( getenv( "CMAP_VIRIDIS" ) != 0 );
	const int magma   = ( getenv( "CMAP_MAGMA"   ) != 0 );
	const int plasma  = ( getenv( "CMAP_PLASMA"  ) != 0 );

	ramp = cmap_heat;
	if ( viridis ) ramp = cmap_viridis;
	if ( magma   ) ramp = cmap_magma;
	if ( plasma  ) ramp = cmap_plasma;

	init_quarters( time(0) );

	setup_postscript();

	int numdebuglogs=8;
	const char* str = getenv("NUM_DEBUG_LOGS");
	if ( str )
	{
		numdebuglogs=atoi(str);
		assert(numdebuglogs>0);
	}
	for ( int i=numdebuglogs-1; i>=0; --i )
	{
		char logfilename[80];
		if ( i )
			snprintf( logfilename, sizeof(logfilename), "debug.log.%d", i );
		else
			snprintf( logfilename, sizeof(logfilename), "debug.log" );
		if ( open_log_file( dirname, logfilename ) )
		{
			// Log file exists, we should read what is in it, currently.
			const int numl = read_log_file();
			fprintf( stderr, "read %d lines from log %s\n", numl, logfilename );
		}
	}

	int fd;
	if ( (fd = inotify_init()) < 0 )
		error( EXIT_FAILURE, errno, "failed to initialize inotify instance" );

	int flags = fcntl( fd, F_GETFL, 0 );
	fcntl( fd, F_SETFL, flags | O_NONBLOCK );

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
	update_image();

	// Read notifications.
	char buf[ sizeof(struct inotify_event) + PATH_MAX ];
	int done=0;

	do
	{
		const int len = read( fd, buf, sizeof(buf) );
		if ( len <= 0 )
		{
			if ( errno == EWOULDBLOCK )
			{
				const int numl = read_log_file();
				if ( !numl )
					sleep(6);
			}
			else if ( errno != EINTR )
				error( EXIT_FAILURE, len == 0 ? 0 : errno, "failed to read inotify event" );
		}
		int i=0;
		while (i < len)
		{
			struct inotify_event *ie = (struct inotify_event*) &buf[i];
			if ( ie->mask & IN_CREATE )
			{
				// A file got created. It could be our new log file!
				if ( !strcmp( ie->name, "debug.log" ) )
				{
					fprintf( stderr, "Reopening logfile.\n" );
					open_log_file( dirname, 0 );
					const int numl = read_log_file();
					fprintf( stderr, "read %d lines from log.\n", numl );
				}
			}
			else if ( ie->mask & IN_MODIFY )
			{
				// We used to only read on modify, but that would pause the graph.
			}
			else if (ie->mask & IN_DELETE)
			{
				// printf("%s was deleted\n",  ie->name);
			}

			i += sizeof(struct inotify_event) + ie->len;
		}

		update_image();

		char c=0;
		const int numr = read( STDIN_FILENO, &c, 1 );
		if ( numr == 1 && ( c == 27 || c == 'q' || c == 'Q' ) )
			done=1;
	} while (!done);

	grapher_exit();
	exit(0);
}

