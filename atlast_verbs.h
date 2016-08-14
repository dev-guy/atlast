#ifndef __ATLAST_VERBS_H
#define __ATLAST_VERBS_H

// Token types

typedef enum {
    T_null,     		  /* Nothing scanned */
    T_word,     		  /* Word stored in token name buffer */
    T_int,    		      /* Integer scanned */
    T_real,     	      /* Real number scanned */
    // T_String  		  /* String scanned */
} Token;

// Functions 
// Never reorder these!

typedef enum {
    W_noop, // This must be first
    W_lit,
	W_print,
	W_plus,

	W__last // This must be the last item
} Word;

#endif
