#ifndef __ATLAST_VERBS_H
#define __ATLAST_VERBS_H

// Token types

typedef enum {
    TokNull,     		  /* Nothing scanned */
    TokWord,     		  /* Word stored in token name buffer */
    TokInt,    		      /* Integer scanned */
    TokReal,     	      /* Real number scanned */
    TokString  		      /* String scanned */
} TokenType;

// Functions 

typedef enum {
    F_lit,
	F_plus,
	F_xxx // This is the last item
} FunctionType;

#endif
