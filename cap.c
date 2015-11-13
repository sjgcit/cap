
/*
 * C Auxilary Preprocessor
 *
 * $Id: cap.c,v 1.138 2015/11/13 11:40:10 sjg Exp $
 *
 * (c) Stephen Geary, Jan 2011
 *
 * A preprocessor to enable template like extensions to the
 * C pro-processor language.
 *
 * It shoud be run before the standard C preprocessor and is
 * intended to have it's output processed by the standard C
 * preprocessor.  It does not replace the C proprocessor.
 *
 * The intention of this system to to enable extensions of
 * the the basic proprocessor using the hash (#) prefix to
 * trigger the extension.
 *
 */

#include <stdio.h>
#include <ctype.h>
#include <malloc.h>
#include <string.h>

#include <stdlib.h>

#include <errno.h>

/* The following are requied for the Linux fork()/exec()/wait()
 * functions.
 */

#include <sys/types.h>
#include <sys/wait.h>

#include <unistd.h>

//#include <sys/stat.h>
//#include <sys/fcntl.h>
//#include <sched.h>



static char *cap_version = "$Revision: 1.138 $" ;

/*
#define DEBUGVER
 */


#ifdef DEBUGVER
   volatile static int debugme = 0 ;
#  define DBGLINEBASE() fprintf(stderr," Line % 5d  %20s " , __LINE__, __func__ )
#  define DBGLINE() if( debugme != 0 ) { DBGLINEBASE() ; fputc( (int)'\n', stderr ) ; }
#  define debugf(...)   { if( debugme != 0 ){  DBGLINEBASE() ; fprintf( stderr, __VA_ARGS__ ) ; } }
#  define debug_on()    { debugme = 1 ; }
#  define debug_off()   { debugme = 0 ; }
#else
#  define DBGLINE()
#  define debugf(...)
#  define debug_on()
#  define debug_off()
#endif /* DEBUGVER */


#define ERRC(c)  fputc((int)(c), stderr )


#ifndef boolean_t
  typedef unsigned int boolean_t ;
#endif

#ifndef TRUE
#  define TRUE  1
#endif

#ifndef FALSE
#  define FALSE 0
#endif


#define toggle(b)   if( (b) == FALSE ){ (b) = TRUE ; }else{ (b) = FALSE ; }

#define safe_free(ptr)  if( (ptr) != NULL ){ free(ptr) ; (ptr) = NULL ; }



/* Sometime we want to apply a macro to open and close braces
 * in statement blocks
 *
 * This mechanism is designed to do that.
 */
static char *open_brace_macro = NULL ;
static char *close_brace_macro = NULL ;

static int apply_brace_macros = FALSE ;

static char *return_macro = NULL ;

static int apply_return_macro = FALSE ;


/* File streaming macros used mostly for brevity and consistency
 */

static FILE *fin = NULL ;
static FILE *fout = NULL ;


#define FCLOSE(fs) \
                    if( (fs) != NULL ) \
                    { \
                        fclose( (fs) ) ; \
                        (fs) = NULL ; \
                    }

#define FPUT(c)     { if( (c) != -1 ){ fputc( (int)(c), fout ) ; } }

#define FPUTS(b)    { if( (b) != NULL ){ fputs( (b), fout ) ; } }


/* Track source line numbers and use #linenum inserted into the output to
 * enable subsequent passes by cpp or a compiler to report the correct
 * line numbers in our source files !
 */
static unsigned int linenum = 1 ;


static boolean_t skip_is_on = FALSE ;

static boolean_t changes_made = FALSE ;

#define DEFAULT_MACROCHAR '#'

static char initial_macrochar = DEFAULT_MACROCHAR ;

static char macrochar = DEFAULT_MACROCHAR ;



#define BUFFLEN 1023


/*
 */


#define BUFFER_DECL( _sym ) \
							static char _sym [ BUFFLEN + 1 ] ; \
							static int _sym ## _idx = 0 ;

#define BUFFER_INDEX( _sym )	( _sym ## _idx )



BUFFER_DECL( prebuff ) ;
BUFFER_DECL( buff ) ;
BUFFER_DECL( postbuff ) ;


#define BUFFER_INIT( _buff )	{ \
									_buff [ 0 ] = 0 ; \
									_buff [ BUFFLEN ] = 0 ; \
									_buff ## _idx = 0 ; \
								}


#define BUFFER_AVAILABLE( _buff )	( _buff ## _idx < BUFFLEN )


#define appendc( _c, _buff )	appendc_fn( (_c), (_buff), BUFFLEN, &_buff ## _idx )

#define appends( _s, _buff )	appends_fn( (_s), (_buff), BUFFLEN, &_buff ## _idx )


/* appendc_fn() will append a char at the index position.
 * 
 * when the index points to the last char in the buffer
 * we always write the new character.  This does mean
 * that the buffer is filled and loose characters, but
 * may mean that we eventually get to an EOL, EOS or EOF
 * character, which is important.
 */
static void appendc_fn( int c, char *buff, int buffsz, int *idxp )
{
	if( ( buff == NULL ) || ( idxp == NULL ) )
		return ;
	
	if( *idxp < 0 )
		return ;
	
	buff[*idxp] = (char)c ;
	
	if( *idxp < buffsz )
	{
		(*idxp)++ ;
	}
}

/* appends_fn() writes a string, terminated by a nul char,
 * to the buffer. Like appendc_fn() it always writes the
 * last character to the buffer, even if the buffer is filled.
 */
static void appends_fn( char *str, char *buff, int buffsz, int *idxp )
{
	if( ( str == NULL ) || ( buff == NULL ) || ( idxp == NULL ) )
		return ;
	
	if( *idxp < 0 )
		return ;
	
	char *p = str ;
	int k = *idxp ;
	
	while( *p != 0 )
	{
		buff[k] = *p ;
		
		if( k < buffsz )
		{
			k++ ;
		}
		
		p++ ;
	};
	
	/* make sure the buffer is nul terminated
	 */
	buff[k] = 0 ;
	
	/* update the index
	 */
	*idxp = k ;
}


#define READ_FROM_BUFFER( _buff )	read_from_buffer_fn( (_buff), BUFFLEN, &_buff ## _idx )


/* read one character from a buffer if possible.
 * 
 * return -1 is reading is not possible
 */

static int read_from_buffer_fn( char *buff, int buffsz, int *idxp )
{
	int retv = 0 ;
	
	if( ( buff == NULL ) || ( idxp == NULL ) )
	{
		return -1 ;
	}
	
	if( ( *idxp <= 0 ) || ( *idxp >= buffsz ) )
	{
		return -1 ;
	}
	
	(*idxp)-- ;
	
	retv = buff[ *idxp ] ;
	
	return retv ;
}



/***********************************************************************
 */


static int lastchar = -1 ;


#define OUTPUTBUFFS_GEN( p1, p2, p3, lc )   \
            { \
                if( *(p1) != '\0' ) \
                { \
                    FPUTS( (p1) ) ; \
                } \
                if( *(p2) != '\0' ) \
                { \
                    FPUTS( (p2) ) ; \
                } \
                if( *(p3) != '\0' ) \
                { \
                    FPUTS( (p3) ) ; \
                } \
                if( (lc) != -1 ) \
                { \
                    FPUT( (lc) ) ; \
                } \
            }

#define OUTPUTBUFFS()               OUTPUTBUFFS_GEN( prebuff, buff, postbuff, lastchar )

#define OUTPUTBUFFS_NOLASTCHAR()    OUTPUTBUFFS_GEN( prebuff, buff, postbuff, -1 )



#define iswhitespace(c)     ( ( (c) == ' ' ) || ( (c) == '\t' ) )

#define istrueeol()         ( ( currentchar_read == '\n' ) && ( lastchar_read != '\\' ) )

/*******************************************************
 */

static int iskeyword( char *str )
{
    int retv = 0 ;
    
    if( buff[0] != macrochar )
        return 0 ;
    
    /* need to avoid white spaces in comparisons
     * as we'd like spacing to be allowed
     *
     * We assume that "keyword" is space trimmed
     * which it should be.
     */
    int i = 1 ;
    int j = 0 ;
    
    while( iswhitespace( buff[i] ) )
        i++ ;
    
    while( buff[i] == str[j] )
    {
        if( str[j] == 0 )
            break ;
        
        i++ ;
        j++ ;
    };
    
    if( buff[i] == str[j] )
    {
        retv = 1 ;
    }
    else
    {
        retv = 0 ;
    }
    
    return retv ;
}


/*******************************************************
 */

struct wordstack_s {
    struct wordstack_s  *next ;
    char            *buff ;
    } ;

typedef struct wordstack_s  wordstack_t ;


/* wordstackp is a stack for storing copies of
 * previously read symbols.
 *
 * It is used in e.g. the "#def" directive.
 */
static wordstack_t *wordstackp = NULL ;


/*******************************************************
 */


/* copy the buffer str to the indicated buffer
 */


#define copybuff( _dest )	strncpy( (_dest), buff, sizeof(buff) )


/*******************************************************
 */


/* read a symbol from the input stream returning it's
 * delimiter and the buffer containing the word
 * terminated by a '\0'
 *
 * a symbol is anything like a variable or function name
 * it can start with and contain a digits or underscores
 *
 * So a -1 return means EOF
 *
 * and anything else is a valid termination character
 *
 * Note the need to defer the toggling of the inside_quotes
 * state until the next readsymbol operation starts.  This
 * is required or a stacked symbol could be read at the end
 * of a quotated section and incorrectly matched if we cleared
 * the inside_quotes flag early.  By using the delay
 * mechanism to toggle we can make the logic work simply for
 * calling code.
 */
static int inside_quotes = FALSE ;
static int quote_pending = FALSE ;

static int escape_pending = FALSE ;

/* the following is used to allow us to backtrack the last
 * characters we read
 *
 * if pendingchar is -1 then there is no character saved for
 * reading
 */

/*
BUFFER_DECL( pendingchar ) ;

#define pendcharbuffer( c )   appendc( c, pendingcharbuffer )
*/

static int pendingchar = 0 ;

#define pendcharbuffer( c )		{ pendingchar = (int)(c) ; }

#define pendchar( _ci )			{ pendingchar = (int)(_ci) ; }


/*******************************************************
 */

BUFFER_DECL( deferredbuffer ) ;

#define read_from_deferred_buffer()        READ_FROM_BUFFER( deferredbuffer )

#define append_to_deferredbuffer( _src )    append_to_deferredbuffer_fn( (_src) )

static void append_to_deferredbuffer_fn( char *src )
{
	/* We need to append this in reverse as we are going to read input from this
	 */
	if( ( src == NULL ) || ( deferredbuffer == NULL ) )
		return ;
	
	int len = 0 ;
	
	len = strlen( src ) ;
	
	if( len <= 0 )
		return ;
	
	int i = len - 1 ;
	
	int k = BUFFER_INDEX(deferredbuffer) ;
	
	while( i >= 0 )
	{
		deferredbuffer[k] = src[i] ;
		
		if( k < sizeof(deferredbuffer) )
		{
			k++ ;
		}
		
		i-- ;
	};
	
	/* make sure the buffer is nul terminated
	 */
	buff[k] = 0 ;
	
	/* update the index
	 */
    BUFFER_INDEX(deferredbuffer) = k ;
}

/*******************************************************
 */

static int in_comment = FALSE ;

static int lastchar_read = -1 ;

static int currentchar_read = -1 ;

static int in_quotes = FALSE ;

/* The rotatingbuffer simply stores the last BUFFLEN
 * characters read by rotating the index value.  Note
 * that cap can take input from stdin so we cannot
 * assume that the source can be preloaded to a
 * buffer or presume the ability to change file
 * position.
 *
 * At ALL times rotatingbuffer_idx points to the NEXT
 * character position to fill.
 *
 * It is initialized to ALL zeros.
 *
 * The extra character at the end of the buffer is NEVER
 * accessed by code but is there as a guard in case
 * the buffer is accessed by code expecting a nul terminator,
 * and so rotatingbuffe[BUFFLEN] == 0 at all times.
 */


BUFFER_DECL( rotatingbuffer ) ;


static char get_rotatingbuffer_char( int nidx )
{
    /* Takes a NEGATIVE index value and reads characters
     * going back in the rotating buffer
     */
    
    if( nidx > 0 )
        return 0 ;
    
    if( nidx < BUFFLEN )
        return 0 ;
    
    int j = rotatingbuffer_idx ;
    
    j += nidx ;
    
    if( j < 0 )
        j += BUFFLEN ;
    
    return rotatingbuffer[j] ;
}


/*******************************************************
 */

int nextchar()
{
    int retv = -1 ;
    
    int i = 0 ;
    
    if( pendingchar != -1 )
    {
        retv = pendingchar ;
        pendingchar = -1 ;
    }
    else
    {
        retv = read_from_deferred_buffer() ;
        
        if( retv != -1 )
            return retv ;

        retv = fgetc( fin ) ;
        
        /* check if we're need to replace braces
         */
        if( ( ! in_quotes ) && ( ! in_comment ) && apply_brace_macros )
        {
            if( retv == (int)'{' )
            {
                append_to_deferredbuffer( open_brace_macro ) ;
                
                retv = read_from_deferred_buffer() ;
            }
            else if( retv == (int)'}' )
            {
                append_to_deferredbuffer( close_brace_macro ) ;
                
                retv = read_from_deferred_buffer() ;
            }
        }
    }
    
    lastchar_read = currentchar_read ;
    currentchar_read = retv ;
    
    if( /* ( ! in_quotes ) && */ ( ! in_comment ) )
    {
        rotatingbuffer[rotatingbuffer_idx] = (char)retv ;
        rotatingbuffer_idx++ ;
        rotatingbuffer_idx %= BUFFLEN ;
        rotatingbuffer[rotatingbuffer_idx] = 0 ;
    }
    
    if( lastchar_read == (int)'\n' )
    {
        linenum++ ;
    }
    
    // if( retv != -1 ){ fputc( retv, stderr ) ; }
    
    return retv ;
}

/*******************************************************
 */

BUFFER_DECL( escaped_char_seq ) ;


int read_escaped_char( boolean_t writeout )
{
    int d = -1 ;
    
    BUFFER_INIT( escaped_char_seq ) ;

    d = nextchar() ;
    
    switch( d )
    {
        case (int)'0' :
        case (int)'1' :
        case (int)'2' :
        case (int)'3' :
        case (int)'4' :
        case (int)'5' :
        case (int)'6' :
        case (int)'7' :
            /* octal : at nost 3 octal digits
             */
            d = nextchar() ;

            if( ( d < (int)'0' ) || ( d > (int)'7' ) )
            {
                pendchar( d ) ;
                break ;
            }

            if( writeout ) { FPUT( d ) ; }

            d = nextchar() ;

            if( ( d < (int)'0' ) || ( d > (int)'7' ) )
            {
                pendchar( d ) ;
                break ;
            }
            
            if( writeout ) { FPUT( d ) ; }

            break ;
        
        case 'x' :
        case 'u' :
        case 'U' :
            /* all of these are arbitrarily long sequences of hex digits
             */
            while( ( !feof(fin) ) && ( d != '\n' ) && isxdigit( d ) )
            {
                if( writeout ) { FPUT( d ) ; }

                d = nextchar() ;
            };
            /* the last char read was a dud for some reason
             * put it in the pending character store
             */
            pendchar( d ) ;
            
            break ;
        
        default:
            /* The default handles both well defined single escaped chars
             * and undefined single escaped chars the same - it
             * assumes the char is valid and outputs it.
             *
             * This is typical behavior for C-like parsers.
             */
            if( writeout ) { FPUT( d ) ; }

            break ;
    }
    
    return d ;
}


/* Read characters inside quotations until we either run out
 * ( which is an error and returns -1 ) or we reach the end
 * quotation mark, when we can return 0
 *
 * The characters are passed directly to the output stream
 *
 * This routine must allow for escaped charaters.
 *
 * Assumes we have already read the opening quotation mark
 */
int pass_chars_in_quotes( int endquotechar )
{
    int retv = 0 ;

    int c = -1 ;
    int d = -1 ;

    c = nextchar() ;
    
    while( ( c != -1 ) && !feof(fin) )
    {
        FPUT( c ) ;
        
        if( c == endquotechar )
            /* quote ended properly
             */
            break ;
        
        if( c == '\n' ) 
        {
            return -1 ;
        }
        
        if( c == '\\' )
        {
            // an escaped character - read the sequence
            
            d = nextchar() ;
            
            switch( d )
            {
                case (int)'0' :
                case (int)'1' :
                case (int)'2' :
                case (int)'3' :
                case (int)'4' :
                case (int)'5' :
                case (int)'6' :
                case (int)'7' :
                    /* octal : at most 3 octal digits
                     */
                    FPUT( d ) ;
                    d = nextchar() ;
                    if( ( d < (int)'0' ) || ( d > (int)'7' ) )
                    {
                        pendchar( d ) ;
                        break ;
                    }
                    FPUT( d ) ;
                    d = nextchar() ;
                    if( ( d < (int)'0' ) || ( d > (int)'7' ) )
                    {
                        pendchar( d ) ;
                        break ;
                    }
                    FPUT( d ) ;
                    break ;
                
                case 'x' :
                case 'u' :
                case 'U' :
                    /* all of these are arbitrarily long sequences of hex digits
                     */
                    while( ( !feof(fin) ) && ( d != '\n' ) && isxdigit( d ) )
                    {
                        FPUT( d ) ;
                        d = nextchar() ;
                    };
                    /* the last char read was a dud for some reason
                     * put it in the pending character store
                     */
                    pendchar( d ) ;
                    break ;
                
                default:
                    /* The default handles both well defined single escaped chars
                     * and undefined single escaped chars the same - it
                     * assumes the char is valid and outputs it.
                     *
                     * This is typical behavior for C-like parsers.
                     */
                    FPUT( d ) ;
                    break ;
            }
        }
        
        c = nextchar() ;
    };
    
    return retv ;
}

/*******************************************************
 */


/* readchar(c) reads input characters until it finds a
 * match to the one requested.
 *
 * it ONLY ignores isspace() characters.
 * 
 * If you pass a whitespace character to it ( ie. try
 * and match to a whitespace ) it simply returns immediately.
 *
 * a return of -1 is an error
 * a return matching the requested char is valid
 */
int readchar( int cwanted )
{
    int retv = -1 ;
    int c = 0 ;
    
    if( iswhitespace(cwanted) )
    {
		return retv ;
	}

    c = nextchar() ;

    while( ( c != -1 ) && !feof(fin) )
    {
        if( !iswhitespace(c) )
        {
            if( c == cwanted )
            {
                retv = c ;
            }

            break ;
        }

        c = nextchar() ;
    };

    return retv ;
}

/*******************************************************
 */


#define issymbolchar(c)     ( ( (c) == '_' ) || isalnum((c)) )

/* reads the next symbol
 *
 * the default behavior is to ignore spaces and output
 * them to fout.
 *
 * symbols only contain alpha-numerics and underscore
 *
 * trailing and lead whitespace is stored in the
 * prebuff and postbuff buffers.
 */



int readsymbol()
{
    int retv = 0 ;

    int c = 0 ;
    
    BUFFER_INIT( prebuff ) ;
    BUFFER_INIT( buff ) ;
    BUFFER_INIT( postbuff ) ;

    if( quote_pending )
    {
        toggle(inside_quotes) ;
        toggle(quote_pending) ;
    }

    c = nextchar() ;

    while( BUFFER_AVAILABLE(buff) && BUFFER_AVAILABLE(prebuff) )
    {
        if( inside_quotes )
        {
            /* read chars until the buffer is full or we
             * find an non-escaped matching quote to end
             *
             * inside quotes we need to check for escaped
             * sequences.
             */

            if( escape_pending )
            {
                toggle(escape_pending) ;
                
                appendc( c, buff ) ;
            }
            else
            {
                if( c == '"' )
                {
                    toggle(quote_pending) ;

                    break ;
                }

                if( c == '\\' )
                {
                    toggle(escape_pending) ;
                }

				appendc( c, buff ) ;
            }

            /* go back to start of loop
             */

            c = nextchar() ;

            continue ;
        }


        /* note that this only happens if we are not inside_quotes
         */

        if( ( BUFFER_INDEX(buff) == 0 ) && iswhitespace(c) )
        {
			appendc( c, prebuff ) ;

            c = nextchar() ;

            continue ;
        }

        if( issymbolchar(c) )
        {
			appendc( c, buff ) ;
        }
        else
        {
            if( (char)c == '"' )
            {
                toggle(quote_pending) ;
            }

            break ;
        }

        c = nextchar() ;
    };

	appendc( 0, buff ) ;
	appendc( 0, prebuff ) ;
    
    // k = 0 ;

    while( BUFFER_AVAILABLE(postbuff) && iswhitespace(c) )
    {
		appendc( c, postbuff ) ;

        c = nextchar() ;
    };

    /* the last char read could be a valid char from the next symbol
     * so we have to check and allow it to be stored for the next character
     * reading.
     */

    if( issymbolchar(c) )
    {
        pendchar(c) ;
        c = (int)' ' ;
    }

	appendc( 0, postbuff ) ;

    retv = c ;
    
    lastchar = c ;
    
    /*
    if( c != (int)'\n' )
    {
        debugf( "readsymbol() ::     buff = [ %s ][ %s ][ %s ][ %c ]\n", prebuff, buff, postbuff, c ) ;
    }
    else
    {
        debugf( "readsymbol() ::     buff = [ %s ][ %s ][ %s ][ \\n ]\n", prebuff, buff, postbuff ) ;
    }
    */

    return retv ;
}


/*******************************************************
 */


/* read everything up to the EOL into the buffer
 */
int read_to_eol()
{
    int retv = 0 ;
    int c = 0 ;
    
    BUFFER_INIT( buff ) ;

    while( BUFFER_AVAILABLE(buff) && ( c != -1 ) )
    {
        c = nextchar() ;

        if( c == (int)'\n' )
            break ;

        if( c != -1 )
        {
			appendc( c, buff ) ;
        }
    };

    appendc( 0, buff ) ;

    return retv ;
}

/*******************************************************
 */

/* this function pushes a copy of a buffer onto the stack
 *
 * basically it's a simple list for later checking
 *
 * the data structure supports these lists
 *
 * if checklen is nonzero then only copy the buffer if length
 * is greater than zero.  There may be situations when we
 * want to copy a zero-length string to the stack, so we need
 * the option.
 */

void stackcopybuffer( char *buffer, int checklen )
{
    wordstack_t *node = NULL ;
    int len = 0 ;

    len = strlen(buffer) ;

    if( ( checklen != 0 ) && ( len == 0 ) )
        return ;

    len++ ;

    node = (wordstack_t *)malloc( sizeof(wordstack_t) ) ;

    if( node == NULL )
        return ;

    node->buff = NULL ;
    node->next = wordstackp ;
    wordstackp = node ;

    node->buff = (char *)malloc( len ) ;

    if( node->buff == NULL )
        return ;

    memcpy( node->buff, buffer, len ) ;
}

/*******************************************************
 */


#define stackcopy() stackcopybuffer(buff,1)

#define stackcopyall()  \
            { \
                stackcopybuffer(prebuff,0) ; \
                stackcopybuffer(buff,0) ; \
                stackcopybuffer(postbuff,0) ; \
            }

/*******************************************************
 */


/* get a pointer to the buffer stored in the node
 * at the given index.
 *
 * 0 is stop of stack
 *
 * return NULL on error
 */
char *stackbuffat( int index )
{
    char *retp = NULL ;
    wordstack_t *curr = wordstackp ;
    wordstack_t *next = NULL ;
    int i = 0 ;

    if( index < 0 )
        return NULL ;

    if( wordstackp == NULL )
        return NULL ;

    while( ( curr != NULL ) && ( i != index ) )
    {
        i++ ;

        curr = curr->next ;
    };

    if( ( curr != NULL ) && ( i == index ) )
    {
        retp = curr->buff ;

        /* debugf( "stackbuffat( %d ) = [ %s ]\n", index, retp ) ;
         */
    }

    return retp ;
}

/*******************************************************
 */


/* pop the tos, freeing all memory for that node
 */
void stackpop()
{
    wordstack_t *next = NULL ;

    if( wordstackp == NULL )
        return ;

    next = wordstackp->next ;

    free( wordstackp->buff ) ;
    free( wordstackp ) ;

    wordstackp = next ;
}

/*******************************************************
 */


/* release all memory used by the stack
 * and reset the stack pointer
 */
void stackfree()
{
    wordstack_t *curr = wordstackp ;
    wordstack_t *next = NULL ;

    while( curr != NULL )
    {
        free( curr->buff ) ;
        next = curr->next ;
        free( curr ) ;
        curr = next ;
    };

    wordstackp = NULL ;
}

/*******************************************************
 */


/* check if the stack contains the currently buffered symbol
 *
 * this function return TRUE if it is and FALSE if not
 * TRUE is normally 1 and FALSE should be 0, but use the
 * macros TRUE and FALSE and not explicit values.
 */
int symbolonstack()
{
    int retv = FALSE ;
    wordstack_t *curr = wordstackp ;

    while( curr != NULL )
    {
        if( curr->buff != NULL )
        {
            if( strcmp( buff, curr->buff ) == 0 )
            {
                return TRUE ;
            }
        }

        curr = curr->next ;
    };

    return retv ;
}

/*******************************************************
 */


int process_macrochar()
{
    int retv = 0 ;
    
    macrochar = nextchar() ;
    
    return retv ;
}

/*******************************************************
 */


int process_simple_macro_def( char **macro )
{
    int retv = 0 ;

    safe_free( *macro ) ;
    
    int i = 0 ;
    
    i = read_to_eol() ;
    
    i = strlen( buff ) ;
    
    *macro = (char *)malloc( i+1 ) ;
    
    if( *macro == NULL )
    {
        /* could not get memory
         */
    
        return -1 ;
    }
    
    memcpy( *macro, buff, i+1 ) ;
    
    return retv ;
}

/*******************************************************
 */


int process_def_open_brace()
{
    int retv = 0 ;
    
    retv = process_simple_macro_def( &open_brace_macro ) ;
    
    return retv ;
}

/*******************************************************
 */


int process_def_close_brace()
{
    int retv = 0 ;
    
    retv = process_simple_macro_def( &close_brace_macro ) ;
    
    return retv ;
}

/*******************************************************
 */


int process_def_return_macro()
{
    int retv = 0 ;
    
    retv = process_simple_macro_def( &return_macro ) ;
    
    return retv ;
}

/*******************************************************
 */


int process_quote()
{
    int retv = 0 ;
    int c = 0 ;

    /* take all input from now until either EOF or
     * '#' at the start of a line and treat it as being part
     * of one define
     *
     * basically pads ' \' unto the end of all lines except the
     * last non-empty one.
     */
    
    c = nextchar() ;

    while( ( c != -1 ) && !feof(fin) )
    {
        if( c == (int)'\n' )
        {
            /* read the next char and see if it's a macrochar ( normally a hash )
             *
             * if it is we know this is the last line of the
             * quoted section and we don't output the ' \'
             */

            /* output pending empty lines
             */
            
            c = nextchar() ;

            if( c == (int)macrochar )
            {
                c = nextchar() ;

                if( c == (int)'\n' )
                {
                    FPUT( '\n' ) ;
                    FPUT( '\n' ) ;

                    return 0 ;
                }
                else
                {
                    /* not a single # followed by newline
                     */

                    FPUT( macrochar ) ;

                    continue ;
                }

            }
            else
            {
                FPUT( ' ' ) ;
                FPUT( '\\' ) ;
                FPUT( '\n' ) ;
                
                continue ;
            }
        }
        else
        {
            /* not an EOL
             */
            
            FPUT( c ) ;
        }
            
        c = nextchar() ;
    };

    return retv ;
}

/*******************************************************
 */


/* treat everything until the next line starting with '#' as a
 * comment.
 *
 * wraps the comment in a common comment style.
 */
int process_comment()
{
    int retv = 0 ;
    int c = 0 ;

    fprintf( fout, "\n/*\n * " ) ;
    
    c = nextchar() ;

    while( ( c != -1 ) && !feof(fin) )
    {
        if( c == (int)macrochar )
        {
            c = nextchar() ;

            if( c == '\n' )
                break ;

            FPUT( macrochar ) ;

            continue ;
        }

        if( c == '\n' )
        {
            fprintf( fout, "\n *" ) ;

            /* if we don't check for the hash symbol coming next we
             * will add a space we don't want which sounds trivial
             * but will cause the comment never to be terminated
             * as the * and / will be separated by a space !
             */

            c = nextchar() ;
            pendchar(c) ;

            if( c != (int)macrochar )
                FPUT( ' ' ) ;
        }
        else
        {
            FPUT( c ) ;
        }

        c = nextchar() ;
    };

    fprintf( fout, "/\n" ) ;

    return retv ;
}

/*******************************************************
 */


static int ends_in_continuation()
{
    int len = 0 ;
    
    len = strlen( buff ) ;
    
    if( len < 1 )
        /* No continuation mark possible
         */
        return 0 ;
    
    if( buff[len-1] == '\\' )
        /* a continuation mark
         */
        return 1 ;
    
    return 0 ;
}

/*******************************************************
 */


/* process a redefine
 *
 * The C preprocessor requires that you first undefine
 * a macro before redefining, but has no direct support
 * for doing that automatically.
 */
int process_redefine()
{
    int retv = 0 ;
    int i = 0 ;
    int c = 0 ;
    int newc = 0 ;

    c = readsymbol() ;

    fprintf( fout, "#undef %s%s%s\n", prebuff, buff, postbuff ) ;
    fprintf( fout, "#define %s%s%s", prebuff, buff, postbuff ) ;
    
    /* Now read to first EOL with no continuation before the new line
     */
    
    i = read_to_eol() ;
    
    while( ends_in_continuation() )
    {
        FPUTS( buff ) ;
        FPUT( '\n' ) ;
    
        i = read_to_eol() ;
    };
    
    FPUTS( buff ) ;
    FPUT( '\n' ) ;
    
    return retv ;
}

/*******************************************************
 */


/* process a macro definition
 *
 * unlike quote creates the macro definition and substitutes all
 * the symbols used as paramater markers with '(<paramname>)'
 * which is the safe macro expansion version.
 *
 * Note that no attempt is made to parse the code so ANY token
 * matching the sequence will be converted.
 */
int process_def()
{
    int retv = 0 ;
    int i = 0 ;
    int c = 0 ;
    int newc = 0 ;

    boolean_t isbracketable = FALSE ;
    
    /* first we need to read the definition part
     * which should be of the form <macroname>([<parametername>{,<parametername>}])
     *
     */

    c = readsymbol() ;

    if( c != (int)'(' )
        /* this is a syntax error
         */
        return -1 ;

    fprintf( fout, "#define %s%s%s(", prebuff, buff, postbuff ) ;

    i = 0 ;

    c = readsymbol() ;

    while( c == (int)',' )
    {
        OUTPUTBUFFS() ;

        /* need to keep a copy of buff
         */

        stackcopy() ;

        c = readsymbol() ;
    };

    OUTPUTBUFFS() ;

    stackcopy() ;

    /* definition has been read and output
     *
     * now output the text replacing the paramameter values until
     * a macrochar ( normally a hash ) is read or EOF and adding
     * the required ' \' EOL sequences 
     */

    c = readsymbol() ;

    while( c != -1 )
    {
        if( !inside_quotes )
        {
            if( c == (int)macrochar )
            {
                c = nextchar() ;

                if( c == '\n' )
                {
                    FPUT( '\n' ) ;
                    break ;
                }

                pendchar(c) ;

                c = (int)macrochar ;
            }
            
            isbracketable = symbolonstack() ;
            
            if( isbracketable )
            {
                /* check following characters to see if the first non-whitespace
                 * sequence is a double hash char
                 */
            }
            
            if( isbracketable )
            {
                FPUTS( prebuff ) ;
                FPUT( '(' ) ;
                FPUTS( buff ) ;
                FPUT( ')' ) ;
                FPUTS( postbuff ) ;
            }
            else
            {
                OUTPUTBUFFS_NOLASTCHAR() ;
            }

            newc = readsymbol() ;

            if( ( c == (int)'\n' ) && ( newc != (int)macrochar ) )
            {
                FPUT( ' ' ) ;
                FPUT( '\\' ) ;
                FPUT( '\n' ) ;
            }
            else
            {
                FPUT( c ) ;
            }

            c = newc ;

            continue ;
        }

        /* the following only happens if we are inside quotes
         */

        OUTPUTBUFFS_NOLASTCHAR() ;

        if( c == (int)'\n' )
        {
            FPUT( ' ' ) ;
            FPUT( '\\' ) ;
        }

        FPUT( c ) ;

        c = readsymbol() ;
    };

    /* tidy up
     */

    stackfree() ;

    return retv ;
}

/*******************************************************
 */


/* output a set of constants with the given pre- and post- identifiers
 * on the given list of name.
 *
 * type 0 flags are from 0 incremented by 1 ( a sequence ) output as
 *        sequences of the last constant+1.
 *
 * type 1 flags are power of two staring from 1
 *
 * type 2 flags are from 0 incremented by 1, but output as exlicit values.
 *
 * type 3 flags are from 0 decremented by 1, as explicit values
 *
 * all the values are made relative to the base one so it is easy
 * to change later
 */
int process_constants( int type )
{
    int retv = 0 ;
    int i = 0 ;
    int c = 0 ;
    char *pre = NULL ;
    char *post = NULL ;
    char *base = NULL ;


    c = readsymbol() ;
    stackcopy() ;
    pre = wordstackp->buff ;

    c = readsymbol() ;
    stackcopy() ;
    post = wordstackp->buff ;

    c = readsymbol() ;
    stackcopy() ;
    base = wordstackp->buff ;

    if( ( type == 0 ) || ( type == 2 ) )
    {
        fprintf( fout, "#define %s_%s_%s\t\t0\n", pre, base, post ) ;

        i = 1 ;
    }

    if( type == 1 )
    {
        fprintf( fout, "#define %s_%s_%s\t\t0x01\n", pre, base, post ) ;

        i = 2 ;
    }

    if( type == 3 )
    {
        fprintf( fout, "#define %s_%s_%s\t\t0\n", pre, base, post ) ;

        i = -1 ;
    }

    while( ( c != -1 ) && ( (char)c != macrochar ) )
    {
        c = readsymbol() ;

        if( strlen(buff) > 0 )
        {
            if( type == 0 )
            {
                fprintf( fout, "#define %s_%s_%s\t\t%s_%s_%s + %d\n", pre, buff, post, pre, base, post, i ) ;

                i++ ;

                continue ;
            }

            if( type == 1 )
            {
                fprintf( fout, "#define %s_%s_%s\t\t0x0%X\n", pre, buff, post, i ) ;

                i *= 2 ;

                continue ;
            }

            if( type == 2 )
            {
                fprintf( fout, "#define %s_%s_%s\t\t%d\n", pre, buff, post, i ) ;

                i++ ;

                continue ;
            }

            if( type == 3 )
            {
                fprintf( fout, "#define %s_%s_%s\t\t%d\n", pre, buff, post, i ) ;

                i-- ;

                continue ;
            }
        }
    };

    return retv ;
}


/*******************************************************
 */


/* Send a command to the shell to process the following
 * block of text
 *
 * The shell command is everything up to EOL following the
 * #command directive
 *
 * Input to the command is send via a pipe.  Output from
 * the command is recieved via another pipe.
 * The command recieves input on it's stdin and sends
 * output to stdout.
 *
 * The parent will have to wait until the child dies (!)
 * before it can continue, so we have to watch for that.
 */

#define PARENT_READ readpipe[0]
#define CHILD_WRITE readpipe[1]
#define CHILD_READ  writepipe[0]
#define PARENT_WRITE    writepipe[1]

int process_command()
{
    int retv = 0 ;

    int writepipe[2] = { -1, -1 } ;
    int readpipe[2] = { -1, -1 } ;

    pid_t childpid ;

    int c = 0 ;

    FILE *fproc = NULL ;


    /* get the command
     */
    retv = read_to_eol() ;

    if( retv < 0 )
        return retv ;

    /* open the pipes
     */

    retv = pipe( writepipe ) ;
    if( retv < 0 )
    {
        return -1 ;
    }

    retv = pipe( readpipe ) ;
    if( retv < 0 )
    {
        close( writepipe[0] ) ;
        close( writepipe[1] ) ;
        return -1 ;
    }

    /* now fork a child
     */

    childpid = fork() ;

    if( childpid == 0 )
    {
        /* In child
         */

        close( PARENT_WRITE ) ;
        close( PARENT_READ ) ;

        dup2( CHILD_READ, 0 ) ;
        dup2( CHILD_WRITE, 1 ) ;

        close( CHILD_READ ) ;
        close( CHILD_WRITE ) ;

        /* now start a command
         */

        retv = execlp( buff, buff, NULL ) ;

        /* if we got here there was an error and we exit anyway
         */

        exit(-1) ;
    }
    else
    {
        /* In parent
         */

        close( CHILD_READ ) ;
        close( CHILD_WRITE ) ;

        /* send input to child
         */

        fproc = fdopen( PARENT_WRITE, "w" ) ;

        if( fproc == NULL )
            goto write_error ;

        c = nextchar() ;

        while( c != -1 )
        {
            if( c == (int)macrochar )
            {
                c = nextchar() ;

                if( c == (int)'\n' )
                {
                    break ;
                }

                fputc( (int)macrochar, fproc ) ;

                continue ;
            }

            fputc( c , fproc ) ;

            c = nextchar() ;
        };

        /* fputc( (int)macrochar, fproc ) ;
         */

        fclose( fproc ) ;

write_error:

        /* read output from command run by child
         */

        fproc = fdopen( PARENT_READ, "r" ) ;

        c = fgetc( fproc ) ;

        while( ( c != -1 ) && !feof(fproc) )
        {
            FPUT(c) ;

            c = fgetc( fproc ) ;
        };

        fclose( fproc ) ;

        /* wait for child to die
         */

        childpid = wait( &retv ) ;

        /* close pipes !
         */
        close( PARENT_READ ) ;
        close( PARENT_WRITE ) ;
    }
    

    return retv ;
}


/*******************************************************
 */


/* process checks the keyword we read in and if it finds a valid
 * word it does our extension processing
 *
 * This returns 0 if a keyword was found and processed and
 * -1 if processing failed or no keyword was found.
 */

#define process_keyword( _kw, _proc ) \
    \
    if( iskeyword( #_kw ) ) \
    { \
        changes_made = TRUE ; \
        \
        retv = process_ ## _proc ; \
        \
        debugf( "Accepted keyword :: " #_kw "\n" ) ; \
        \
        goto err_exit ; \
    }

#define flag_keyword( _kw, _flag, _value ) \
    \
    if( iskeyword( #_kw ) ) \
    { \
        (_flag) = (_value) ; \
        \
        changes_made = TRUE ; \
        \
        debugf( "Accepted flag:: " #_kw "\n" ) ; \
        \
        retv = 0 ; \
        \
        goto err_exit ; \
    }
    

int process()
{
    int retv = -1 ;

    /* for safety
     */
    buff[BUFFLEN] = '\0' ;
    
    // debugf( "prebuff  = [%s]\n", prebuff ) ;
    // debugf( "buff     = [%s]\n", buff ) ;
    // debugf( "postbuff = [%s]\n", postbuff ) ;
    
    flag_keyword( skipoff, skip_is_on, FALSE ) ;
    
    /* NOTE :
     *
     * The following check for skip_is_on must only be made
     * AFTER checking for a skipoff directive.
     *
     * If it's done before that then we never check for skipoff
     * and we could skip forever !
     */

    if( skip_is_on )
    {
        return -1 ;
    }

    flag_keyword( skipon, skip_is_on, TRUE ) ;
    
    process_keyword( macrochar, macrochar() ) ;
    
    if( iskeyword("debugon") )
    {
        /* turn on debug reporting from caps
         */
        debug_on() ;

        changes_made = TRUE ;

        retv = 0 ;
        
        goto err_exit ;
    }

    if( iskeyword("debugoff") )
    {
        /* turn off debug reporting from caps
         */
        debug_off() ;

        changes_made = TRUE ;

        retv = 0 ;
        
        goto err_exit ;
    }

    process_keyword( quote, quote() ) ;

    process_keyword( comment, comment() ) ;

    process_keyword( def, def() ) ;
    
    process_keyword( constants, constants(0) ) ;

    process_keyword( flags, constants(1) ) ;

    process_keyword( constants-values, constants(2) ) ;

    process_keyword( constants-negative, constants(3) ) ;

    process_keyword( command, command() ) ;
    
    process_keyword( redefine, redefine() ) ;

    flag_keyword( brace_macros_on, apply_brace_macros, TRUE ) ;    
    
    flag_keyword( brace_macros_off, apply_brace_macros, FALSE ) ;    
    
    process_keyword( def_open_brace, def_open_brace() ) ;
    
    process_keyword( def_close_brace, def_close_brace() ) ;
    
    flag_keyword( return_macro_on, apply_return_macro, TRUE ) ;
    
    flag_keyword( return_macro_off, apply_return_macro, FALSE ) ;
    
    process_keyword( def_return_macro, def_return_macro() ) ;
    
err_exit:
    
    // if( ! changes_made )
    // {
        fprintf( fout, "#line %d\n", linenum ) ;
    // }
    
    return retv ;
}


/*******************************************************************
 *
 * main_process() processes each individual file passed to cap
 *
 * There is no cross-file communication.  Each file starts with a
 * clean state in cap.
 *
 */
int main_process()
{
    if( fin == NULL )
    {
        return 0 ;
    }
    
    int retv = 0 ;
    int c = 0 ;
    int i = 0 ;
    int j = 0 ;
    
    int leadingspaces = 0 ;
    
    /* blank chars is needed because a blank might be a character
     * other than a space ( e.g. a tab ) and we want to output that
     * character, not just a space.  So we have to record blank chars
     */
    char blankchars[BUFFLEN] ;
    
    /* Initialize the state variables for a new file
     */
    
    apply_brace_macros = FALSE ;
    
    BUFFER_INIT( deferredbuffer ) ;
    
    escape_pending = FALSE ;
    
    in_comment = FALSE ;
    in_quotes = FALSE ;
    
    lastchar_read = -1 ;
    currentchar_read = -1 ;
    
    BUFFER_INIT( prebuff ) ;
    BUFFER_INIT( buff ) ;
    BUFFER_INIT( postbuff ) ;

    
    BUFFER_INIT( rotatingbuffer ) ;
    
    macrochar = initial_macrochar ;
    
    pendingchar = -1 ;
    
    quote_pending = FALSE ;
    
    skip_is_on = FALSE ;
    
    linenum = 1 ;

    /* Now process the file ... 
     */

    while( ( c != -1 ) && ( !feof(fin) ) )
    {
        // DBGLINE() ;
        
        c = nextchar() ;
        
        if( c == -1 )
            break ;
        
        if( c != (int)macrochar )
        {
            /* not a macrochar ( normally hash ) as first char on line
             * then output everything until we
             * we reach EOL or EOF with special handling.
             */
            
            // DBGLINE() ;
            
            FPUT(c) ;

            while( ( c != '\n' ) && ( c != -1 ) && ( !feof(fin) ) )
            {
                c = nextchar() ;

                if( c == -1 )
                    break ;
                
                if( ( c == '\'' ) && ( lastchar_read != '\\' ) )
                {
                    /* a single char in quotes - could be escaped
                     * treat like a quoted string
                     */
                    
                    FPUT( c ) ;
                    
                    pass_chars_in_quotes( '\'' ) ;
                    
                    c = currentchar_read ;
                }
                else if( ( c == '/' ) && ( lastchar_read == '/' ) )
                {
                    // Single line comment - read and output to EOL
                    
                    read_to_eol() ;
                    
                    FPUT( c ) ;
                    FPUTS( buff ) ;
                    FPUT( currentchar_read ) ;
                    
                    c = currentchar_read ;
                    
                    // fprintf( stderr, "single line comment at %d : [%c%s]\n", linenum, (char)c, buff ) ;
                }
                else if( ( c == '*' ) && ( lastchar_read == '/' ) )
                {
                    DBGLINE() ;
                
                    /* a C comment
                     * read everything and output it unchanged until EOF
                     * or we detect the end of comment pair of chars
                     */
                    
                    in_comment = TRUE ;
                    
                    FPUT(c) ;
                    
                    while( ( c != -1 ) && ( !feof(fin) ) )
                    {
                        if( ( c == '/' ) && ( lastchar_read == '*' ) )
                        {
                            /* end of comment
                             */
                            
                            break ;
                        }
                        
                        c = nextchar() ;
                        
                        FPUT(c) ;
                    };
                    
                    in_comment = FALSE ;
                }
                else if( ( ( c == '"' ) && ( lastchar_read != '\\' ) /* && ( lastchar_read != '\'' ) */ ) && ! in_quotes )
                {
                    DBGLINE() ;
                    
                    /* a double quotes character starting something in quotes
                     */
                    
                    FPUT( c ) ;
                    
                    in_quotes = TRUE ;
                    
                    pass_chars_in_quotes( (int)'\"' ) ;
                    
                    c = currentchar_read ;
                    
                    in_quotes = FALSE ;
                    
                    if( c == -1 )
                    {
                        debugf( "c was -1\n" ) ;
                    }
                    
                    DBGLINE() ;
                }
                else if( apply_return_macro && ( c == 'r' ) && ( ( lastchar_read == '\n' ) || iswhitespace(lastchar_read) ) )
                {
                    DBGLINE() ;
                
                    /* check for possible return statement
                     *
                     * we don't do this if we're not applting brace macros
                     */
                    
                    char tempbuff[BUFFLEN] ;
                    
                    char *returnstr = "return" ;
                    
                    memset( tempbuff, 0, 8 ) ;
                    
                    int k = 0 ;
                    
                    while( ( c == returnstr[k] ) && ( k < 6 ) )
                    {
                        tempbuff[k] = returnstr[k] ;
                        k++ ;
                        
                        c = nextchar() ;
                    };
                    
                    tempbuff[k] = c ;
                    tempbuff[k+1] = 0 ;
                    
                    if( ( k == 6 ) && ( iswhitespace(c) || ( c == ';' ) || ( c == '(' ) ) )
                    {
                        /* a match to a the C return keyword !
                         *
                         * with brace macros on we need to ensure that the
                         * closing brace macro is placed before the return
                         *
                         * There are three forms :
                         *    return ;
                         *    return(...) ;
                         *    return x ;
                         */
                        
                        FPUT( '{' ) ;
                        
                        FPUTS( return_macro ) ;
                        
                        if( c == ';' )
                        {
                            FPUTS( tempbuff ) ;
                        }
                        else
                        {
                            FPUTS( tempbuff ) ;
                            
                            c = nextchar() ;
                            
                            while( ( c != -1 ) && ( c != ';' ) && ( !feof(fin) ) )
                            {
                                FPUT( c ) ;
                                c = nextchar() ;
                            };
                            
                            FPUT( c ) ;
                        }
                        
                        FPUT( '}' ) ;
                    }
                    else
                    {
                        /* Not a match - just output what we have
                         */
                        
                        FPUTS( tempbuff ) ;
                    }
                }
                else
                {
                    FPUT(c) ;
                }
            };

            if( c == -1 )
                break ;
        }
        else
        {
            /* a possible preprocessor directive
             * check if it is one of our extension keywords
             *
             * If it is pass processing to the extension module
             * and if not then output the directive
             */

            /* read characters into a buffer until EOL, EOF or a space
             * check this string againsts the key word lists
             *
             * Note that isspace() also checks for EOL
             *
             * As we permit leading spaces after the hash and before the
             * directive we need to first check for this.
             */

            *buff = macrochar ;
            
            i = 1 ;
            
            c = nextchar() ;
            
            leadingspaces = 0 ;
            
            while( iswhitespace((char)c) )
            {
                blankchars[ leadingspaces++ ] = (char)c ;
                
                c = nextchar() ;
            };
            
            blankchars[ leadingspaces ] = 0 ;

            while( ( i < BUFFLEN ) && ( c != -1 ) && ( !isspace((char)c) ) )
            {
                buff[i] = (char)c ;
                i++ ;

                c = nextchar() ;
            };

            buff[i] = '\0' ;
            
            debugf( "buff = %s\n", buff ) ;
            
            if( ( i == BUFFLEN ) || ( c == -1 ) )
            {
                /* ran out of room in buffer or EOF
                 * so we can treat that as not being a keyword
                 */
                
                DBGLINE() ;
                
                FPUT( macrochar ) ;
                
                FPUTS( blankchars ) ;
                leadingspaces = 0 ;

                j = 1 ;

                while( j < i )
                {
                    FPUT( buff[j] ) ;
                    j++ ;
                };

                if( c != -1 )
                {
                    FPUT( c ) ;
                }
                
                DBGLINE() ;
            }
            else
            {
                /* a space or EOL terminated the sequence
                 *
                 * in either case we check for a keyword and we output it as given
                 * if no keyword is found.
                 */
                
                DBGLINE() ;
                
                retv = process() ;

                if( retv != 0 )
                {
                    DBGLINE() ;
                
                    /* ouput the buffer if we did not recognize the word
                     */

                    FPUT( macrochar ) ;
                    
                    FPUTS( blankchars ) ;
                    leadingspaces = 0 ;

                    j = 1 ;
                    
                    while( j < i )
                    {
                        FPUT( buff[j] ) ;
                        j++ ;
                    };
                    
                    /* .. and finally the last character read !
                     */
                    
                    FPUT( c ) ;
                    
                    /* Now write out everything until EOL without continuation mark
                     *
                     * but check if the last char was a newline, in which case we had
                     * some like '  #  else\n' as a line and reading another char will
                     * put us on the next line !
                     */
                    
                    if( c != (int)'\n' )
                    {
                        c = nextchar() ;
                        
                        while( ( c != -1 ) && ( !feof(fin) ) && ! istrueeol() )
                        {
                            FPUT( c ) ;
                            
                            c = nextchar() ;
                        };
                        
                        FPUT( c ) ;
                    }
                }

                if( isspace(c) )
                {
                    /* if not EOF then we still have a character we read ahead
                     * that must be output
                     */
                    FPUT( c ) ;
                }
            }
        }
    };
    
err_exit:
    
    /* DEBUG Stuff
     */
     
    /*
    {
        int ii = 0 ;
    
        ii = rotatingbuffer_idx - 10 ;
    
        if( ii < 0 ){ ii = 0  ;}
    
        fprintf( stderr, "END buff = [%s] indicies ( 0 to %d->%d )\n", rotatingbuffer+ii, rotatingbuffer_idx, ii ) ;
    }
    */
    
    return retv ;
}

/*******************************************************
 */


static void version()
{
    char ver[128] = "$Revision: 1.138 $" ;
    
    /* Skip the RCS string preceeding the version number
     */
    
    int i = 11 ;
    
    while( isdigit( ver[i] ) || ( ver[i] == '.' ) )
        i++ ;
    
    ver[i] = 0 ;
    
    printf( "CAP - C Auxilary Preprocessor - version %s\n", ver+11 ) ;
}

/*******************************************************
 */


static int init_main( int argc, char **argv )
{
    int retv = 0 ;
    int i ;

    fin     = NULL ;
    fout    = stdout ;

    int input_files = 0 ;


    inside_quotes = FALSE ;
    quote_pending = FALSE ;

    escape_pending = FALSE ;

    pendingchar = -1 ;


    i = 1 ;

    while( i < argc )
    {
        if( ( strcmp(argv[i],"-V") == 0 ) || ( strcmp(argv[i],"--version") == 0 ) )
        {
            i++ ;
            
            version() ;
            
            continue ;
        }
        
        if( strcmp(argv[i],"-m") == 0 )
        {
            /* Set the character used to denote a macro
             * default char is a hash ( # ) and this lets
             * you use something else.
             */
            
            i++ ;
            
            if( argc <= i )
            {
                // not enough arguments
                
                return -1 ;
            }
            
            initial_macrochar = *( argv[i] ) ;
            
            i++ ;
            
            continue ;
        }
    
        if( strcmp(argv[i],"-o") == 0 )
        {
            i++ ;

            if( i > argc )
                return -1 ;

            if( fout != stdout )
            {
                FCLOSE( fout ) ;
                fout = NULL ;
            }

            if( strcmp( argv[i], "-" ) == 0 )
            {
                fout = stdout ;
            }
            else
            {
                fout = fopen( argv[i], "w" ) ;

                if( fout == NULL )
                    return -1 ;
            }

            if( fout == NULL )
                return -1 ;
            
            i++ ;
            
            continue ;
        }

        /* This has to be a filename ( or a mistake )
         */

        if( fin != stdin )
        {
            FCLOSE( fin ) ;
            fin = NULL ;
        }

        if( strcmp( argv[i], "-" ) == 0 )
        {
            fin = stdin ;
        }
        else
        {
            fin = fopen( argv[i] , "r" ) ;
        }

        if( fin == NULL )
            return -1 ;

        input_files++ ;

        retv = main_process() ;

        if( retv != 0 )
            return -1 ;

        i++ ;
    };

    if( input_files == 0 )
    {
        retv = main_process() ;
    }

    return retv ;
}

/*******************************************************
 */

static int deinit_main()
{
    int retv = 0 ;

    /* close file channels
     */

    fflush( fout ) ;

    if( fin != stdin )
    {
        FCLOSE( fin ) ;
    }

    if( fout != stdout )
    {
        FCLOSE( fout ) ;
    }
    
    safe_free( open_brace_macro ) ;
    safe_free( close_brace_macro ) ;

    return retv ;
}


/*******************************************************
 */

int main( int argc, char **argv )
{
    int retv = 0 ;

    debug_on() ;
    
    retv = init_main( argc, argv ) ;
    
    debug_off() ;

    if( retv != 0 )
        goto fini_error ;

fini_error:

    deinit_main() ;

    return retv ;
}


/*******************************************************
 */


