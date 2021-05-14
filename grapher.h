// grapher.h
//
// by Abraham Stolk.

extern int imw;
extern int imh;
extern uint32_t* im;
extern char* overlay;

extern char postscript[256];

extern int grapher_resized;


extern int grapher_init( void );

extern void grapher_adapt_to_new_size( void );

extern void grapher_update( void );

extern void grapher_exit( void );


#define RESETALL  	"\x1b[0m"

#define CURSORHOME	"\x1b[H"

#define CLEARSCREEN	"\e[H\e[2J\e[3J"

#define SETFG		"\x1b[38;2;"

#define SETBG		"\x1b[48;2;"


