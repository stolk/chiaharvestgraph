// grapher.h
//
// by Abraham Stolk.

extern int imw;
extern int imh;
extern uint32_t* im;
extern char* legend;

extern char postscript[256];

extern int grapher_resized;


extern int grapher_init( void );

extern void grapher_adapt_to_new_size( void );

extern void grapher_update( void );

extern void grapher_exit( void );


