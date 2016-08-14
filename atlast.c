/*

			      A T L A S T

	 Autodesk Threaded Language Application System Toolkit

		    Main Interpreter and Compiler


     Designed and implemented in January of 1990 by John Walker.

		This program is in the public domain.

*/
/*  Subpackage configuration.  If INDIVIDUALLY is defined, the inclusion

#include "atlast.h"

    of subpackages is based on whether their compile-time tags are
    defined.  Otherwise, we automatically enable all the subpackages.  */

//#define ARRAY			      /* Array subscripting words */
//#define BREAK			      /* Asynchronous break facility */
//#define COMPILERW		      /* Compiler-writing words */
//#define DEFFIELDS		      /* Definition field access for words */
//#define DOUBLE			      /* Double word primitives (2DUP) */
//#define MATH			      /* Math functions */
//#define MEMMESSAGE		      /* Print message for stack/heap errors */
//#define REAL			      /* Floating point numbers */
//#define SHORTCUTA		      /* Shortcut integer arithmetic words */
//#define SHORTCUTC		      /* Shortcut integer comparison */
//#define STRING			      /* String functions */
//#define SYSTEM			      /* System command function */
//#define SLEEP
//#ifndef NOMEMCHECK
#define TRACE			      /* Execution tracing */
//#define WALKBACK		      /* Walkback trace */
//#define WORDSUSED		      /* Logging of words used and unused */
//#endif /* NOMEMCHECK */

#ifdef SLEEP
#include <unistd.h>
#endif

#define EXPORT

#include <mem.h>
#include <fm_debug.h>

#include "atldef.h"
#include "atlast_verbs.h"

/*  Data types	*/

typedef enum {False = 0, True = 1} Boolean;

#define EOS     '\0'                  /* End of string characters */

#define V	(void)		      /* Force result to void */

#define Truth	-1L		      /* Stack value for truth */
#define Falsity 0L		      /* Stack value for falsity */

/* Utility definition to get an  array's  element  count  (at  compile
   time).   For  example:

       int  arr[] = {1,2,3,4,5};
       ...
       printf("%d", ELEMENTS(arr));

   would print a five.  ELEMENTS("abc") can also be used to  tell  how
   many  bytes are in a string constant INCLUDING THE TRAILING NULL. */

#define ELEMENTS(array) (sizeof(array)/sizeof((array)[0]))

/*  Globals visible to calling programs  */

atl_int atl_stklen = ATL_STKLEN;	      /* Evaluation stack length */
atl_int atl_rstklen = ATL_RSTKLEN;	      /* Return stack length */
atl_int atl_heaplen = ATL_HEAPLEN;	      /* Heap length */

//atl_int atl_ltempstr = 256;	      /* Temporary string buffer length */
//atl_int atl_ntempstr = 10;	      /* Number of temporary string buffers */

atl_int atl_trace = Falsity;	      /* Tracing if true */
atl_int atl_walkback = Truth;	      /* Walkback enabled if true */
atl_int atl_redef = Truth;	      /* Allow redefinition without issuing
                                       the "not unique" message. */

    /* The evaluation stack */

Exported stackitem stack[ATL_STKLEN];
Exported stackitem *stk;	      /* Stack pointer */
Exported stackitem *stackbot;	      /* Stack bottom */
Exported stackitem *stacktop;	      /* Stack top */

    /* The return stack */

Exported dictword **rstack[ATL_RSTKLEN];   /* Return stack */
Exported dictword ***rstk;	      /* Return stack pointer */
Exported dictword ***rstackbot;       /* Return stack bottom */
Exported dictword ***rstacktop;       /* Return stack top */

    /* The heap */

Exported stackitem heap[ATL_HEAPLEN];      /* Allocation heap */
Exported stackitem *hptr;	      /* Heap allocation pointer */
Exported stackitem *heapbot;	      /* Bottom of heap (temp string buffer) */
Exported stackitem *heaptop;	      /* Top of heap */

    /* The temporary string buffers */

//Exported char **strbuf = NULL;	      /* Table of pointers to temp strings */
//Exported int cstrbuf = 0;	      /* Current temp string */

    /* The walkback trace stack */

#ifdef WALKBACK
static dictword *wback[ATL_RSTKLEN];       /* Walkback trace buffer */
static dictword **wbptr;	      /* Walkback trace pointer */
#endif /* WALKBACK */

#ifdef MEMSTAT
Exported stackitem *stackmax;	      /* Stack maximum excursion */
Exported dictword ***rstackmax;       /* Return stack maximum excursion */
Exported stackitem *heapmax;	      /* Heap maximum excursion */
#endif

static const int *instream;	      /* Current input stream line */
static const int *stream_end;

#ifdef REAL
static atl_real tokreal;	      /* Scanned real number */
#ifdef ALIGNMENT
Exported atl_real rbuf0, rbuf1, rbuf2; /* Real temporary buffers */
#endif
#endif

Exported dictword **ip = NULL;	      /* Instruction pointer */
Exported dictword *curword = NULL;    /* Current word being executed */
static int evalstat = ATL_SNORM;      /* Evaluator status */
static Boolean tickpend = False;      /* Take address of next word */
static Boolean ctickpend = False;     /* Compile-time tick ['] pending */
static Boolean cbrackpend = False;    /* [COMPILE] pending */
static Boolean stringlit = False;     /* String literal anticipated */
#ifdef BREAK
static Boolean broken = False;	      /* Asynchronous break received */
#endif

/*  Forward functions  */

static void exword(dictword *);
static	void	trouble(const char *);

#ifndef NOMEMCHECK
//static void notcomp(void);
//static void divzero(void);
#endif
#ifdef WALKBACK
static void pwalkback(void);
#endif

#define stackitem_to_ptr(item, datatype) ( (datatype*) ((size_t) item) )

#define stackitem_to_stackitem_ptr(x) stackitem_to_ptr(x, stackitem)

#if 0
static stackitem * ptr_to_stackitem_ptr(void * x) {
    return (stackitem*) x;
}

static rstackitem stackitem_to_rstackitem(stackitem x) {
    return ((rstackitem)NULL)+x;
}
#endif

static stackitem ptr_to_stackitem(const void *x) {
    uint16 bytes[2];

    MemCopy(bytes, x, 4);
    // xap is big endian
    return( ((int32) bytes[1])<<16) + bytes[0];
}

/*  LOOKUP  --	Look up token in the dictionary.  */

static dictword *lookup(int word)
{
    dictword *dw = dict;

    if (word < 0 || word >= W__last) {
    	DEBUG_E("Unknown word: ");
    	DEBUG_U16(word);
    	DEBUG_ENDL(NULL);

    	return NULL;
    }

#ifdef WORDSUSED
	dw->flags |= WORDUSED; /* Mark this word used */
#endif

    return dw;
}

/*  ATL_MEMSTAT  --  Print memory usage summary.  */

#ifdef MEMSTAT
void atl_memstat(void)
{
    static char fmt[] = "   %-12s %6ld    %6ld    %6ld       %3ld\n";

    V printf("\n             Memory Usage Summary\n\n");
    V printf("                 Current   Maximum    Items     Percent\n");
    V printf("  Memory Area     usage     used    allocated   in use \n");

    V printf(fmt, "Stack",
	((long) (stk - stack)),
	((long) (stackmax - stack)),
	atl_stklen,
	(100L * (stk - stack)) / atl_stklen);
    V printf(fmt, "Return stack",
	((long) (rstk - rstack)),
	((long) (rstackmax - rstack)),
	atl_rstklen,
	(100L * (rstk - rstack)) / atl_rstklen);
    V printf(fmt, "Heap",
	((long) (hptr - heap)),
	((long) (heapmax - heap)),
	atl_heaplen,
	(100L * (hptr - heap)) / atl_heaplen);
}
#endif /* MEMSTAT */

/*  Primitive implementing functions.  */

#ifdef NOMEMCHECK
#define Compiling
#else
#define Compiling if (state == Falsity) {notcomp(); return;}
#endif
#define Compconst(x) Ho(1); Hstore = (stackitem) (x)
#define Skipstring ip += *((char *) ip)

prim P_plus(void)			      /* Add two numbers */
{
    Sl(2);
/* printf("PLUS %lx + %lx = %lx\n", S1, S0, (S1 + S0)); */
    S1 += S0;
    Pop;
}

prim P_print(void) {
	Sl(1);
	DEBUG_I("U32 on top of stack: ");
    DEBUG_U32(S0);
	DEBUG_ENDL(NULL);
}

// These functions are documented here:
// https://www.fourmilab.ch/atlast/atlast.html
//
// To enable:
// 1. Surround with #endif and #if 0
// 2. Add to atlast_verbs.h
// 3. Add to atl_init(void)

#if 0
prim P_minus(void)			      /* Subtract two numbers */
{
    Sl(2);
    S1 -= S0;
    Pop;
}

prim P_times(void)			      /* Multiply two numbers */
{
    Sl(2);
    S1 *= S0;
    Pop;
}

prim P_div(void)			      /* Divide two numbers */
{
    Sl(2);
#ifndef NOMEMCHECK
    if (S0 == 0) {
	divzero();
	return;
    }
#endif /* NOMEMCHECK */
    S1 /= S0;
    Pop;
}

prim P_mod(void)			      /* Take remainder */
{
    Sl(2);
#ifndef NOMEMCHECK
    if (S0 == 0) {
	divzero();
	return;
    }
#endif /* NOMEMCHECK */
    S1 %= S0;
    Pop;
}

prim P_divmod(void) 		      /* Compute quotient and remainder */
{
    stackitem quot;

    Sl(2);
#ifndef NOMEMCHECK
    if (S0 == 0) {
	divzero();
	return;
    }
#endif /* NOMEMCHECK */
    quot = S1 / S0;
    S1 %= S0;
    S0 = quot;
}

prim P_min(void)			      /* Take minimum of stack top */
{
    Sl(2);
    S1 = min(S1, S0);
    Pop;
}

prim P_max(void)			      /* Take maximum of stack top */
{
    Sl(2);
    S1 = max(S1, S0);
    Pop;
}

prim P_neg(void)			      /* Negate top of stack */
{
    Sl(1);
    S0 = - S0;
}

prim P_abs(void)			      /* Take absolute value of top of stack */
{
    Sl(1);
    S0 = abs(S0);
}

prim P_equal(void)			      /* Test equality */
{
    Sl(2);
    S1 = (S1 == S0) ? Truth : Falsity;
    Pop;
}

prim P_unequal(void)		      /* Test inequality */
{
    Sl(2);
    S1 = (S1 != S0) ? Truth : Falsity;
    Pop;
}

prim P_gtr(void)			      /* Test greater than */
{
    Sl(2);
    S1 = (S1 > S0) ? Truth : Falsity;
    Pop;
}

prim P_lss(void)			      /* Test less than */
{
    Sl(2);
    S1 = (S1 < S0) ? Truth : Falsity;
    Pop;
}

prim P_geq(void)			      /* Test greater than or equal */
{
    Sl(2);
    S1 = (S1 >= S0) ? Truth : Falsity;
    Pop;
}

prim P_leq(void)			      /* Test less than or equal */
{
    Sl(2);
    S1 = (S1 <= S0) ? Truth : Falsity;
    Pop;
}

prim P_and(void)			      /* Logical and */
{
    Sl(2);
/* printf("AND %lx & %lx = %lx\n", S1, S0, (S1 & S0)); */
    S1 &= S0;
    Pop;
}

prim P_or(void)			      /* Logical or */
{
    Sl(2);
    S1 |= S0;
    Pop;
}

prim P_xor(void)			      /* Logical xor */
{
    Sl(2);
    S1 ^= S0;
    Pop;
}

prim P_not(void)			      /* Logical negation */
{
    Sl(1);
    S0 = ~S0;
}

prim P_shift(void)			      /* Shift:  value nbits -- value */
{
    Sl(1);
    S1 = (S0 < 0) ? (((unsigned long) S1) >> (-S0)) :
		    (((unsigned long) S1) <<   S0);
    Pop;
}

#ifdef SHORTCUTA

prim P_1plus(void)			      /* Add one */
{
    Sl(1);
    S0++;
}

prim P_2plus(void)			      /* Add two */
{
    Sl(1);
    S0 += 2;
}

prim P_1minus(void) 		      /* Subtract one */
{
    Sl(1);
    S0--;
}

prim P_2minus(void) 		      /* Subtract two */
{
    Sl(1);
    S0 -= 2;
}

prim P_2times(void) 		      /* Multiply by two */
{
    Sl(1);
    S0 *= 2;
}

prim P_2div(void)			      /* Divide by two */
{
    Sl(1);
    S0 /= 2;
}

#endif /* SHORTCUTA */

#ifdef SHORTCUTC

prim P_0equal(void) 		      /* Equal to zero ? */
{
    Sl(1);
    S0 = (S0 == 0) ? Truth : Falsity;
}

prim P_0notequal(void)		      /* Not equal to zero ? */
{
    Sl(1);
    S0 = (S0 != 0) ? Truth : Falsity;
}

prim P_0gtr(void)			      /* Greater than zero ? */
{
    Sl(1);
    S0 = (S0 > 0) ? Truth : Falsity;
}

prim P_0lss(void)			      /* Less than zero ? */
{
    Sl(1);
    S0 = (S0 < 0) ? Truth : Falsity;
}

#endif /* SHORTCUTC */

/*  Storage allocation (heap) primitives  */

prim P_here(void)			      /* Push current heap address */
{
    So(1);
    Push = ptr_to_stackitem(hptr);
}

prim P_bang(void)			      /* Store value into address */
{
    Sl(2);
    Hpc(ptr_to_stackitem(S0));
    *(stackitem_to_stackitem_ptr(S0)) = S1;
    Pop2;
}

prim P_at(void)			      /* Fetch value from address */
{
    Sl(1);
    Hpc(S0);
    S0 = *(stackitem_to_stackitem_ptr(S0));
}

prim P_plusbang(void)		      /* Add value at specified address */
{
    Sl(2);
    Hpc(S0);
    *(stackitem_to_stackitem_ptr(S0)) += S1;
    Pop2;
}

prim P_allot(void)			      /* Allocate heap bytes */
{
    stackitem n;

    Sl(1);
    n = (S0 + (sizeof(stackitem) - 1)) / sizeof(stackitem);
    Pop;
    Ho(n);
    hptr += n;
}

prim P_comma(void)			      /* Store one item on heap */
{
    Sl(1);
    Ho(1);
    Hstore = S0;
    Pop;
}

prim P_cbang(void)			      /* Store byte value into address */
{
    Sl(2);
    Hpc(S0);
    *(stackitem_to_stackitem_ptr(S0)) = S1;
    Pop2;
}

prim P_cat(void)			      /* Fetch byte value from address */
{
    Sl(1);
    Hpc(S0);
    S0 = *(stackitem_to_stackitem_ptr(S0));
}

prim P_ccomma(void) 		      /* Store one byte on heap */
{
    unsigned char *chp;

    Sl(1);
    Ho(1);
    chp = ((unsigned char *) hptr);
    *chp++ = S0;
    hptr = (stackitem *) chp;
    Pop;
}

prim P_cequal(void) 		      /* Align heap pointer after storing */
{				      /* a series of bytes. */
    stackitem n = (ptr_to_stackitem(hptr)) - (ptr_to_stackitem(heap)) %
			(sizeof(stackitem));

    if (n != 0) {
	char *chp = ((char *) hptr);

	chp += sizeof(stackitem) - n;
	hptr = ((stackitem *) chp);
    }
}

/*  Variable and constant primitives  */

prim P_var(void)			      /* Push body address of current word */
{
    So(1);
    Push = ptr_to_stackitem(curword) + Dictwordl;
}

Exported void P_create(void)	      /* Create new word */
{
    defpend = True;		      /* Set definition pending */
    Ho(Dictwordl);
    createword = (dictword *) hptr;   /* Develop address of word */
    createword->name = NULL;	      /* Clear pointer to name string */
    createword->code = P_var;	      /* Store default code */
    hptr += Dictwordl;		      /* Allocate heap space for word */
}

prim P_variable(void)		      /* Declare variable */
{
    P_create(); 		      /* Create dictionary item */
    Ho(1);
    Hstore = 0; 		      /* Initial value = 0 */
}

prim P_con(void)			      /* Push value in body */
{
    So(1);
    Push = *((ptr_to_stackitem_ptr(curword)) + Dictwordl);
}

prim P_constant(void)		      /* Declare constant */
{
    Sl(1);
    P_create(); 		      /* Create dictionary item */
    createword->code = P_con;	      /* Set code to constant push */
    Ho(1);
    Hstore = S0;		      /* Store constant value in body */
    Pop;
}

/*  Array primitives  */

#ifdef ARRAY
prim P_arraysub(void)		      /* Array subscript calculation */
{				      /* sub1 sub2 ... subn -- addr */
    int i, offset, esize, nsubs;
    stackitem *array;
    stackitem *isp;

    Sl(1);
    array = ((ptr_to_stackitem_ptr(curword)) + Dictwordl);
    Hpc(array);
    nsubs = *array++;		      /* Load number of subscripts */
    esize = *array++;		      /* Load element size */
#ifndef NOMEMCHECK
    isp = &S0;
    for (i = 0; i < nsubs; i++) {
	stackitem subn = *isp--;

	if (subn < 0 || subn >= array[i])
            trouble("Subscript out of range");
    }
#endif /* NOMEMCHECK */
    isp = &S0;
    offset = *isp;		      /* Load initial offset */
    for (i = 1; i < nsubs; i++)
	offset = (offset * (*(++array))) + *(--isp);
    Npop(nsubs - 1);
    /* Calculate subscripted address.  We start at the current word,
       advance to the body, skip two more words for the subscript count
       and the fundamental element size, then skip the subscript bounds
       words (as many as there are subscripts).  Then, finally, we
       can add the calculated offset into the array. */
    S0 = ptr_to_stackitem ( ptr_to_stackitem_ptr(curword) +
	    Dictwordl + 2 + nsubs + esize * offset);
}

prim P_array(void)			      /* Declare array */
{				      /* sub1 sub2 ... subn n esize -- array */
    int i, nsubs, asize = 1;
    stackitem *isp;

    Sl(2);
#ifndef NOMEMCHECK
    if (S0 <= 0)
        trouble("Bad array element size");
    if (S1 <= 0)
        trouble("Bad array subscript count");
#endif /* NOMEMCHECK */

    nsubs = S1; 		      /* Number of subscripts */
    Sl(nsubs + 2);		      /* Verify that dimensions are present */

    /* Calculate size of array as the product of the subscripts */

    asize = S0; 		      /* Fundamental element size */
    isp = &S2;
    for (i = 0; i < nsubs; i++) {
#ifndef NOMEMCHECK
	if (*isp <= 0)
            trouble("Bad array dimension");
#endif /* NOMEMCHECK */
	asize *= *isp--;
    }

    asize = (asize + (sizeof(stackitem) - 1)) / sizeof(stackitem);
    Ho(asize + nsubs + 2);	      /* Reserve space for array and header */
    P_create(); 		      /* Create variable */
    createword->code = P_arraysub;   /* Set method to subscript calculate */
    Hstore = nsubs;		      /* Header <- Number of subscripts */
    Hstore = S0;		      /* Header <- Fundamental element size */
    isp = &S2;
    for (i = 0; i < nsubs; i++) {     /* Header <- Store subscripts */
	Hstore = *isp--;
    }
    while (asize-- > 0) 	      /* Clear the array to zero */
	Hstore = 0;
    Npop(nsubs + 2);
}
#endif /* ARRAY */

/*  Floating point primitives  */

#ifdef REAL

prim P_flit(void)			      /* Push floating point literal */
{
    int i;

    So(Realsize);
#ifdef TRACE
    if (atl_trace) {
	atl_real tr;

	V memcpy((char *) &tr, (char *) ip, sizeof(atl_real));
        V printf("%g ", tr);
    }
#endif /* TRACE */
    for (i = 0; i < Realsize; i++) {
	Push = ptr_to_stackitem(*ip)++;
    }
}

prim P_fplus(void)			      /* Add floating point numbers */
{
    Sl(2 * Realsize);
    SREAL1(REAL1 + REAL0);
    Realpop;
}

prim P_fminus(void) 		      /* Subtract floating point numbers */
{
    Sl(2 * Realsize);
    SREAL1(REAL1 - REAL0);
    Realpop;
}

prim P_ftimes(void) 		      /* Multiply floating point numbers */
{
    Sl(2 * Realsize);
    SREAL1(REAL1 * REAL0);
    Realpop;
}

prim P_fdiv(void)			      /* Divide floating point numbers */
{
    Sl(2 * Realsize);
#ifndef NOMEMCHECK
    if (REAL0 == 0.0) {
	divzero();
	return;
    }
#endif /* NOMEMCHECK */
    SREAL1(REAL1 / REAL0);
    Realpop;
}

prim P_fmin(void)			      /* Minimum of top two floats */
{
    Sl(2 * Realsize);
    SREAL1(min(REAL1, REAL0));
    Realpop;
}

prim P_fmax(void)			      /* Maximum of top two floats */
{
    Sl(2 * Realsize);
    SREAL1(max(REAL1, REAL0));
    Realpop;
}

prim P_fneg(void)			      /* Negate top of stack */
{
    Sl(Realsize);
    SREAL0(- REAL0);
}

prim P_fabs(void)			      /* Absolute value of top of stack */
{
    Sl(Realsize);
    SREAL0(abs(REAL0));
}

prim P_fequal(void) 		      /* Test equality of top of stack */
{
    stackitem t;

    Sl(2 * Realsize);
    t = (REAL1 == REAL0) ? Truth : Falsity;
    Realpop2;
    Push = t;
}

prim P_funequal(void)		      /* Test inequality of top of stack */
{
    stackitem t;

    Sl(2 * Realsize);
    t = (REAL1 != REAL0) ? Truth : Falsity;
    Realpop2;
    Push = t;
}

prim P_fgtr(void)			      /* Test greater than */
{
    stackitem t;

    Sl(2 * Realsize);
    t = (REAL1 > REAL0) ? Truth : Falsity;
    Realpop2;
    Push = t;
}

prim P_flss(void)			      /* Test less than */
{
    stackitem t;

    Sl(2 * Realsize);
    t = (REAL1 < REAL0) ? Truth : Falsity;
    Realpop2;
    Push = t;
}

prim P_fgeq(void)			      /* Test greater than or equal */
{
    stackitem t;

    Sl(2 * Realsize);
    t = (REAL1 >= REAL0) ? Truth : Falsity;
    Realpop2;
    Push = t;
}

prim P_fleq(void)			      /* Test less than or equal */
{
    stackitem t;

    Sl(2 * Realsize);
    t = (REAL1 <= REAL0) ? Truth : Falsity;
    Realpop2;
    Push = t;
}

prim P_fdot(void)			      /* Print floating point top of stack */
{
    Sl(Realsize);
    // V printf("%g ", REAL0);
    Realpop;
}

prim P_float(void)			      /* Convert integer to floating */
{
    atl_real r;

    Sl(1)
    So(Realsize - 1);
    r = S0;
    stk += Realsize - 1;
    SREAL0(r);
}

prim P_fix(void)			      /* Convert floating to integer */
{
    stackitem i;

    Sl(Realsize);
    i = (int) REAL0;
    Realpop;
    Push = i;
}

#ifdef MATH

#define Mathfunc(x) Sl(Realsize); SREAL0(x(REAL0))

prim P_acos(void)			      /* Arc cosine */
{
    Mathfunc(acos);
}

prim P_asin(void)			      /* Arc sine */
{
    Mathfunc(asin);
}

prim P_atan(void)			      /* Arc tangent */
{
    Mathfunc(atan);
}

prim P_atan2(void)			      /* Arc tangent:  y x -- atan */
{
    Sl(2 * Realsize);
    SREAL1(atan2(REAL1, REAL0));
    Realpop;
}

prim P_cos(void)			      /* Cosine */
{
    Mathfunc(cos);
}

prim P_exp(void)			      /* E ^ x */
{
    Mathfunc(exp);
}

prim P_log(void)			      /* Natural log */
{
    Mathfunc(log);
}

prim P_pow(void)			      /* X ^ Y */
{
    Sl(2 * Realsize);
    SREAL1(pow(REAL1, REAL0));
    Realpop;
}

prim P_sin(void)			      /* Sine */
{
    Mathfunc(sin);
}

prim P_sqrt(void)			      /* Square root */
{
    Mathfunc(sqrt);
}

prim P_tan(void)			      /* Tangent */
{
    Mathfunc(tan);
}
#undef Mathfunc
#endif /* MATH */
#endif /* REAL */

/*  Stack mechanics  */

prim P_depth(void)			      /* Push stack depth */
{
    stackitem s = stk - stack;

    So(1);
    Push = s;
}

#endif
prim P_clear(void)			      /* Clear stack */
{
    stk = stack;
}
#if 0

prim P_dup(void)			      /* Duplicate top of stack */
{
    stackitem s;

    Sl(1);
    So(1);
    s = S0;
    Push = s;
}

prim P_drop(void)			      /* Drop top item on stack */
{
    Sl(1);
    Pop;
}

prim P_swap(void)			      /* Exchange two top items on stack */
{
    stackitem t;

    Sl(2);
    t = S1;
    S1 = S0;
    S0 = t;
}

prim P_over(void)			      /* Push copy of next to top of stack */
{
    stackitem s;

    Sl(2);
    So(1);
    s = S1;
    Push = s;
}

prim P_pick(void)			      /* Copy indexed item from stack */
{
    Sl(2);
    S0 = stk[-(2 + S0)];
}

prim P_rot(void)			      /* Rotate 3 top stack items */
{
    stackitem t;

    Sl(3);
    t = S0;
    S0 = S2;
    S2 = S1;
    S1 = t;
}

prim P_minusrot(void)		      /* Reverse rotate 3 top stack items */
{
    stackitem t;

    Sl(3);
    t = S0;
    S0 = S1;
    S1 = S2;
    S2 = t;
}

prim P_roll(void)			      /* Rotate N top stack items */
{
    stackitem i, j, t;

    Sl(1);
    i = S0;
    Pop;
    Sl(i + 1);
    t = stk[-(i + 1)];
    for (j = -(i + 1); j < -1; j++)
	stk[j] = stk[j + 1];
    S0 = t;
}

prim P_tor(void)			      /* Transfer stack top to return stack */
{
    Rso(1);
    Sl(1);
    Rpush = stackitem_to_rstackitem(S0);
    Pop;
}

prim P_rfrom(void)			      /* Transfer return stack top to stack */
{
    Rsl(1);
    So(1);
    Push = ptr_to_stackitem(R0);
    Rpop;
}

prim P_rfetch(void) 		      /* Fetch top item from return stack */
{
    Rsl(1);
    So(1);
    Push = ptr_to_stackitem(R0);
}

#ifdef Macintosh
/* This file creates more than 32K of object code on the Mac, which causes
   MPW to barf.  So, we split it up into two code segments of <32K at this
   point. */
#pragma segment TOOLONG
#endif /* Macintosh */

/*  Double stack manipulation items  */

#ifdef DOUBLE

prim P_2dup(void)			      /* Duplicate stack top doubleword */
{
    stackitem s;

    Sl(2);
    So(2);
    s = S1;
    Push = s;
    s = S1;
    Push = s;
}

prim P_2drop(void)			      /* Drop top two items from stack */
{
    Sl(2);
    stk -= 2;
}

prim P_2swap(void)			      /* Swap top two double items on stack */
{
    stackitem t;

    Sl(4);
    t = S2;
    S2 = S0;
    S0 = t;
    t = S3;
    S3 = S1;
    S1 = t;
}

prim P_2over(void)			      /* Extract second pair from stack */
{
    stackitem s;

    Sl(4);
    So(2);
    s = S3;
    Push = s;
    s = S3;
    Push = s;
}

prim P_2rot(void)			      /* Move third pair to top of stack */
{
    stackitem t1, t2;

    Sl(6);
    t2 = S5;
    t1 = S4;
    S5 = S3;
    S4 = S2;
    S3 = S1;
    S2 = S0;
    S1 = t2;
    S0 = t1;
}

prim P_2variable(void)		      /* Declare double variable */
{
    P_create(); 		      /* Create dictionary item */
    Ho(2);
    Hstore = 0; 		      /* Initial value = 0... */
    Hstore = 0; 		      /* ...in both words */
}

prim P_2con(void)			      /* Push double value in body */
{
    So(2);
    Push = *((ptr_to_stackitem_ptr(curword)) + Dictwordl);
    Push = *((ptr_to_stackitem_ptr(curword)) + Dictwordl + 1);
}

prim P_2constant(void)		      /* Declare double word constant */
{
    Sl(1);
    P_create(); 		      /* Create dictionary item */
    createword->code = P_2con;       /* Set code to constant push */
    Ho(2);
    Hstore = S1;		      /* Store double word constant value */
    Hstore = S0;		      /* in the two words of body */
    Pop2;
}

prim P_2bang(void)			      /* Store double value into address */
{
    stackitem *sp;

    Sl(2);
    Hpc(S0);
    sp = stackitem_to_stackitem_ptr(S0);
    *sp++ = S2;
    *sp = S1;
    Npop(3);
}

prim P_2at(void)			      /* Fetch double value from address */
{
    stackitem *sp;

    Sl(1);
    So(1);
    Hpc(S0);
    sp = stackitem_to_stackitem_ptr(S0);
    S0 = *sp++;
    Push = *sp;
}
#endif /* DOUBLE */

/*  Data transfer primitives  */

#endif

prim P_noop(void){
}

prim P_dolit(void)			      /* Push instruction stream literal */
{
    So(1);
#ifdef TRACE
    if (atl_trace) {
        DEBUG_I("lit ");
        DEBUG_U32(*ip);
        DEBUG_ENDL(NULL);
    }
#endif
    Push = ptr_to_stackitem(*ip++);	      /* Push the next datum from the
					 instruction stream. */
}

#if 0

/*  Control flow primitives  */

prim P_nest(void)			      /* Invoke compiled word */
{
    Rso(1);
#ifdef WALKBACK
    *wbptr++ = curword; 	      /* Place word on walkback stack */
#endif
    Rpush = ip; 		      /* Push instruction pointer */
    ip = (((dictword **) curword) + Dictwordl);
}

prim P_exit(void)			      /* Return to top of return stack */
{
    Rsl(1);
#ifdef WALKBACK
    wbptr = (wbptr > wback) ? wbptr - 1 : wback;
#endif
    ip = R0;			      /* Set IP to top of return stack */
    Rpop;
}

prim P_branch(void) 		      /* Jump to in-line address */
{
    ip += ptr_to_stackitem(*ip);	      /* Jump addresses are IP-relative */
}

prim P_qbranch(void)		      /* Conditional branch to in-line addr */
{
    Sl(1);
    if (S0 == 0)		      /* If flag is false */
	ip += ptr_to_stackitem(*ip);	      /* then branch. */
    else			      /* Otherwise */
	ip++;			      /* skip the in-line address. */
    Pop;
}

prim P_if(void)			      /* Compile IF word */
{
    Compiling;
    Compconst(s_qbranch);	      /* Compile question branch */
    So(1);
    Push = ptr_to_stackitem(hptr);	      /* Save backpatch address on stack */
    Compconst(0);		      /* Compile place-holder address cell */
}

prim P_else(void)			      /* Compile ELSE word */
{
    stackitem *bp;

    Compiling;
    Sl(1);
    Compconst(s_branch);	      /* Compile branch around other clause */
    Compconst(0);		      /* Compile place-holder address cell */
    Hpc(S0);
    bp = stackitem_to_stackitem_ptr(S0);	      /* Get IF backpatch address */
    *bp = hptr - bp;
    S0 = ptr_to_stackitem(hptr - 1);      /* Update backpatch for THEN */
}

prim P_then(void)			      /* Compile THEN word */
{
    stackitem *bp;

    Compiling;
    Sl(1);
    Hpc(S0);
    bp = stackitem_to_stackitem_ptr(S0);	      /* Get IF/ELSE backpatch address */
    *bp = hptr - bp;
    Pop;
}

prim P_qdup(void)			      /* Duplicate if nonzero */
{
    Sl(1);
    if (S0 != 0) {
	stackitem s = S0;
	So(1);
	Push = s;
    }
}

prim P_begin(void)			      /* Compile BEGIN */
{
    Compiling;
    So(1);
    Push = ptr_to_stackitem(hptr);	      /* Save jump back address on stack */
}

prim P_until(void)			      /* Compile UNTIL */
{
    stackitem off;
    stackitem *bp;

    Compiling;
    Sl(1);
    Compconst(s_qbranch);	      /* Compile question branch */
    Hpc(S0);
    bp = stackitem_to_stackitem_ptr(S0);	      /* Get BEGIN address */
    off = -(hptr - bp);
    Compconst(off);		      /* Compile negative jumpback address */
    Pop;
}

prim P_again(void)			      /* Compile AGAIN */
{
    stackitem off;
    stackitem *bp;

    Compiling;
    Compconst(s_branch);	      /* Compile unconditional branch */
    Hpc(S0);
    bp = stackitem_to_stackitem_ptr(S0);	      /* Get BEGIN address */
    off = -(hptr - bp);
    Compconst(off);		      /* Compile negative jumpback address */
    Pop;
}

prim P_while(void)			      /* Compile WHILE */
{
    Compiling;
    So(1);
    Compconst(s_qbranch);	      /* Compile question branch */
    Compconst(0);		      /* Compile place-holder address cell */
    Push = ptr_to_stackitem(hptr - 1);    /* Queue backpatch for REPEAT */
}

prim P_repeat(void) 		      /* Compile REPEAT */
{
    stackitem off;
    stackitem *bp1, *bp;

    Compiling;
    Sl(2);
    Hpc(S0);
    bp1 = stackitem_to_stackitem_ptr(S0);	      /* Get WHILE backpatch address */
    Pop;
    Compconst(s_branch);	      /* Compile unconditional branch */
    Hpc(S0);
    bp = stackitem_to_stackitem_ptr(S0);	      /* Get BEGIN address */
    off = -(hptr - bp);
    Compconst(off);		      /* Compile negative jumpback address */
    *bp1 = hptr - bp1;                /* Backpatch REPEAT's jump out of loop */
    Pop;
}

prim P_do(void)			      /* Compile DO */
{
    Compiling;
    Compconst(s_xdo);		      /* Compile runtime DO word */
    So(1);
    Compconst(0);		      /* Reserve cell for LEAVE-taking */
    Push = ptr_to_stackitem(hptr);	      /* Save jump back address on stack */
}

prim P_xdo(void)			      /* Execute DO */
{
    Sl(2);
    Rso(3);
    Rpush = ip + ptr_to_stackitem(*ip);   /* Push exit address from loop */
    ip++;			      /* Increment past exit address word */
    Rpush = stackitem_to_rstackitem(S1);	      /* Push loop limit on return stack */
    Rpush = stackitem_to_rstackitem(S0);	      /* Iteration variable initial value to
					 return stack */
    stk -= 2;
}

prim P_qdo(void)			      /* Compile ?DO */
{
    Compiling;
    Compconst(s_xqdo);		      /* Compile runtime ?DO word */
    So(1);
    Compconst(0);		      /* Reserve cell for LEAVE-taking */
    Push = ptr_to_stackitem(hptr);	      /* Save jump back address on stack */
}

prim P_xqdo(void)			      /* Execute ?DO */
{
    Sl(2);
    if (S0 == S1) {
	ip += ptr_to_stackitem(*ip);
    } else {
	Rso(3);
	Rpush = ip + (ptr_to_stackitem(*ip));/* Push exit address from loop */
	ip++;			      /* Increment past exit address word */
	Rpush = stackitem_to_rstackitem(S1);      /* Push loop limit on return stack */
	Rpush = stackitem_to_rstackitem(S0);      /* Iteration variable initial value to
					 return stack */
    }
    stk -= 2;
}

prim P_loop(void)			      /* Compile LOOP */
{
    stackitem off;
    stackitem *bp;

    Compiling;
    Sl(1);
    Compconst(s_xloop); 	      /* Compile runtime loop */
    Hpc(S0);
    bp = stackitem_to_stackitem_ptr(S0);	      /* Get DO address */
    off = -(hptr - bp);
    Compconst(off);		      /* Compile negative jumpback address */
    *(bp - 1) = (hptr - bp) + 1;      /* Backpatch exit address offset */
    Pop;
}

prim P_ploop(void)			      /* Compile +LOOP */
{
    stackitem off;
    stackitem *bp;

    Compiling;
    Sl(1);
    Compconst(s_pxloop);	      /* Compile runtime +loop */
    Hpc(S0);
    bp = stackitem_to_stackitem_ptr(S0);	      /* Get DO address */
    off = -(hptr - bp);
    Compconst(off);		      /* Compile negative jumpback address */
    *(bp - 1) = (hptr - bp) + 1;      /* Backpatch exit address offset */
    Pop;
}

prim P_xloop(void)			      /* Execute LOOP */
{
    Rsl(3);
    R0 = stackitem_to_rstackitem((ptr_to_stackitem(R0)) + 1);
    if ((ptr_to_stackitem(R0)) == (ptr_to_stackitem(R1))) {
	rstk -= 3;		      /* Pop iteration variable and limit */
	ip++;			      /* Skip the jump address */
    } else {
	ip += ptr_to_stackitem(*ip);
    }
}

prim P_xploop(void) 		      /* Execute +LOOP */
{
    stackitem niter;

    Sl(1);
    Rsl(3);

    niter = (ptr_to_stackitem(R0)) + S0;
    Pop;
    if ((niter >= (ptr_to_stackitem(R1))) &&
	((ptr_to_stackitem(R0)) < (ptr_to_stackitem(R1)))) {
	rstk -= 3;		      /* Pop iteration variable and limit */
	ip++;			      /* Skip the jump address */
    } else {
	ip += ptr_to_stackitem(*ip);
	R0 = stackitem_to_rstackitem(niter);
    }
}

prim P_leave(void)			      /* Compile LEAVE */
{
    Rsl(3);
    ip = R2;
    rstk -= 3;
}

prim P_i(void)			      /* Obtain innermost loop index */
{
    Rsl(3);
    So(1);
    Push = ptr_to_stackitem(R0);            /* It's the top item on return stack */
}

prim P_j(void)			      /* Obtain next-innermost loop index */
{
    Rsl(6);
    So(1);
    Push = ptr_to_stackitem(rstk[-4]);      /* It's the 4th item on return stack */
}

#endif
prim P_quit(void)			      /* Terminate execution */
{
    rstk = rstack;		      /* Clear return stack */
#ifdef WALKBACK
    wbptr = wback;
#endif
    ip = NULL;			      /* Stop execution of current word */
}
#if 0

#ifdef SLEEP
prim P_sleep() // microsec ---
{ 
  Sl(1);
  usleep(S0);
  Pop;
}

prim P_longsleep() // sec ---
{ 
  Sl(1);
  sleep(S0);
  Pop;
}
#endif

#endif
prim P_abort(void)			      /* Abort, clearing data stack */
{
    P_clear();			      /* Clear the data stack */
    P_quit();			      /* Shut down execution */
}
#if 0

prim P_abortq(void) 		      /* Abort, printing message */
{
    if (state) {
	stringlit = True;	      /* Set string literal expected */
	Compconst(s_abortq);	      /* Compile ourselves */
    } else {
        // V printf("%s", (char *) ip); 
        /* Otherwise, print string literal
					 in in-line code. */
#ifdef WALKBACK
	pwalkback();
#endif /* WALKBACK */
	P_abort();		      /* Abort */
	atl_comment = state = Falsity;/* Reset all interpretation state */
	stringlit = tickpend = ctickpend = False;
    }
}

/*  Compilation primitives  */

prim P_immediate(void)		      /* Mark most recent word immediate */
{
    dict->name[0] |= IMMEDIATE;
}

prim P_lbrack(void) 		      /* Set interpret state */
{
    Compiling;
    state = Falsity;
}

prim P_rbrack(void) 		      /* Restore compile state */
{
    state = Truth;
}

Exported void P_dodoes(void)	      /* Execute indirect call on method */
{
    Rso(1);
    So(1);
    Rpush = ip; 		      /* Push instruction pointer */
#ifdef WALKBACK
    *wbptr++ = curword; 	      /* Place word on walkback stack */
#endif
    /* The compiler having craftily squirreled away the DOES> clause
       address before the word definition on the heap, we back up to
       the heap cell before the current word and load the pointer from
       there.  This is an ABSOLUTE heap address, not a relative offset. */
    ip = *((dictword ***) ((ptr_to_stackitem_ptr(curword)) - 1));

    /* Push the address of this word's body as the argument to the
       DOES> clause. */
    Push = ptr_to_stackitem((ptr_to_stackitem_ptr(curword)) + Dictwordl);
}

prim P_does(void)			      /* Specify method for word */
{

    /* O.K., we were compiling our way through this definition and we've
       encountered the Dreaded and Dastardly Does.  Here's what we do
       about it.  The problem is that when we execute the word, we
       want to push its address on the stack and call the code for the
       DOES> clause by diverting the IP to that address.  But...how
       are we to know where the DOES> clause goes without adding a
       field to every word in the system just to remember it.  Recall
       that since this system is portable we can't cop-out through
       machine code.  Further, we can't compile something into the
       word because the defining code may have already allocated heap
       for the word's body.  Yukkkk.  Oh well, how about this?  Let's
       copy any and all heap allocated for the word down one stackitem
       and then jam the DOES> code address BEFORE the link field in
       the word we're defining.

       Then, when (DOES>) (P_dodoes) is called to execute the word, it
       will fetch that code address by backing up past the start of
       the word and seting IP to it.  Note that FORGET must recognise
       such words (by the presence of the pointer to P_dodoes() in
       their code field, in case you're wondering), and make sure to
       deallocate the heap word containing the link when a
       DOES>-defined word is deleted.  */

    if (createword != NULL) {
	stackitem *sp = ((stackitem *) createword), *hp;

	Rsl(1);
	Ho(1);

	/* Copy the word definition one word down in the heap to
	   permit us to prefix it with the DOES clause address. */

	for (hp = hptr - 1; hp >= sp; hp--)
	    *(hp + 1) = *hp;
	hptr++; 		      /* Expand allocated length of word */
	*sp++ = ptr_to_stackitem(ip);       /* Store DOES> clause address before
                                         word's definition structure. */
	createword = (dictword *) sp; /* Move word definition down 1 item */
	createword->code = P_dodoes; /* Set code field to indirect jump */

	/* Now simulate an EXIT to bail out of the definition without
	   executing the DOES> clause at definition time. */

	ip = R0;		      /* Set IP to top of return stack */
#ifdef WALKBACK
	wbptr = (wbptr > wback) ? wbptr - 1 : wback;
#endif
	Rpop;			      /* Pop the return stack */
    }
}

prim P_colon(void)			      /* Begin compilation */
{
    state = Truth;		      /* Set compilation underway */
    P_create(); 		      /* Create conventional word */
}

prim P_semicolon(void)		      /* End compilation */
{
    Compiling;
    Ho(1);
    Hstore = s_exit;
    state = Falsity;		      /* No longer compiling */
    /* We wait until now to plug the P_nest code so that it will be
       present only in completed definitions. */
    if (createword != NULL)
	createword->code = P_nest;   /* Use P_nest for code */
    createword = NULL;		      /* Flag no word being created */
}

prim P_tick(void)			      /* Take address of next word */
{
    int i;

    /* Try to get next symbol from the input stream.  If
       we can't, and we're executing a compiled word,
       report an error.  Since we can't call back to the
       calling program for more input, we're stuck. */

    i = next_token();	      /* Scan for next token */
    if (i != T_null) {
	if (i == T_word) {
	    dictword *di;

	    if ((di = lookup(next_arg())) != NULL) {
		So(1);
		Push = ptr_to_stackitem(di); /* Push word compile address */
	    } else {
//                V printf(" '%s' undefined ", tokbuf);
	    }
	} else {
  //          V printf("\nWord not specified when expected.\n");
	    P_abort();
	}
    } else {
	/* O.K., there was nothing in the input stream.  Set the
	   tickpend flag to cause the compilation address of the next
           token to be pushed when it's supplied on a subsequent input
	   line. */
	if (ip == NULL) {
	    tickpend = True;	      /* Set tick pending */
	} else {
//            V printf("\nWord requested by ` not on same input line.\n");
	    P_abort();
	}
    }
}

prim P_bracktick(void)		      /* Compile in-line code address */
{
    Compiling;
    ctickpend = True;		      /* Force literal treatment of next
					 word in compile stream */
}

prim P_execute(void)		      /* Execute word pointed to by stack */
{
    dictword *wp;

    Sl(1);
    wp = stackitem_to_ptr(S0, dictword);	      /* Load word address from stack */
    Pop;			      /* Pop data stack before execution */
    exword(wp); 		      /* Recursively call exword() to run
					 the word. */
}

prim P_body(void)			      /* Get body address for word */
{
    Sl(1);
    S0 += Dictwordl * sizeof(stackitem);
}

prim P_state(void)			      /* Get state of system */
{
    So(1);
    Push = ptr_to_stackitem(&state);
}

/*  Definition field access primitives	*/

#ifdef SYSTEM
prim P_system(void)
{				      /* string -- status */
    Sl(1);
    Hpc(S0);
    S0 = system((char *) S0);
}
#endif /* SYSTEM */

#ifdef TRACE
prim P_trace(void)			      /* Set or clear tracing of execution */
{
    Sl(1);
    atl_trace = (S0 == 0) ? Falsity : Truth;
    Pop;
}
#endif /* TRACE */

#ifdef WALKBACK
prim P_walkback(void)		      /* Set or clear error walkback */
{
    Sl(1);
    atl_walkback = (S0 == 0) ? Falsity : Truth;
    Pop;
}
#endif /* WALKBACK */

#ifdef WORDSUSED

prim P_wordsused(void)		      /* List words used by program */
{
    dictword *dw = dict;

    while (dw != NULL) {
	if (*(dw->name) & WORDUSED) {
//           V printf("\n%s", dw->name + 1);
	}
#ifdef Keyhit
	if (kbquit()) {
	    break;
	}
#endif
	dw = dw->wnext;
    }
  //  V printf("\n");
}

prim P_wordsunused(void)		      /* List words not used by program */
{
    dictword *dw = dict;

    while (dw != NULL) {
	if (!(*(dw->name) & WORDUSED)) {
//           V printf("\n%s", dw->name + 1);
	}
#ifdef Keyhit
	if (kbquit()) {
	    break;
	}
#endif
	dw = dw->wnext;
    }
//    V printf("\n");
}
#endif /* WORDSUSED */

#ifdef COMPILERW

prim P_brackcompile(void)		      /* Force compilation of immediate word */
{
    Compiling;
    cbrackpend = True;		      /* Set [COMPILE] pending */
}

prim P_literal(void)		      /* Compile top of stack as literal */
{
    Compiling;
    Sl(1);
    Ho(2);
    Hstore = s_lit;		      /* Compile load literal word */
    Hstore = S0;		      /* Compile top of stack in line */
    Pop;
}

prim P_compile(void)		      /* Compile address of next inline word */
{
    Compiling;
    Ho(1);
    Hstore = ptr_to_stackitem(*ip)++;       /* Compile the next datum from the
					 instruction stream. */
}

prim P_backmark(void)		      /* Mark backward backpatch address */
{
    Compiling;
    So(1);
    Push = ptr_to_stackitem(hptr);	      /* Push heap address onto stack */
}

prim P_backresolve(void)		      /* Emit backward jump offset */
{
    stackitem offset;

    Compiling;
    Sl(1);
    Ho(1);
    Hpc(S0);
    offset = -(hptr - stackitem_to_stackitem_ptr(S0));
    Hstore = offset;
    Pop;
}

prim P_fwdmark(void)		      /* Mark forward backpatch address */
{
    Compiling;
    Ho(1);
    Push = ptr_to_stackitem(hptr);	      /* Push heap address onto stack */
    Hstore = 0;
}

prim P_fwdresolve(void)		      /* Emit forward jump offset */
{
    stackitem offset;

    Compiling;
    Sl(1);
    Hpc(S0);
    offset = (hptr - stackitem_to_stackitem_ptr(S0));
    *(stackitem_to_stackitem_ptr(S0)) = offset;
    Pop;
}

#endif /* COMPILERW */
#endif

/* Dictionary */

Exported dictword dict[W__last];

#if 0
static struct primfcn primt[] = {
    {"0+", P_plus},
    {"0-", P_minus},
    {"0*", P_times},
    {"0/", P_div},
    {"0MOD", P_mod},
    {"0/MOD", P_divmod},
    {"0MIN", P_min},
    {"0MAX", P_max},
    {"0NEGATE", P_neg},
    {"0ABS", P_abs},
    {"0=", P_equal},
    {"0<>", P_unequal},
    {"0>", P_gtr},
    {"0<", P_lss},
    {"0>=", P_geq},
    {"0<=", P_leq},

    {"0AND", P_and},
    {"0OR", P_or},
    {"0XOR", P_xor},
    {"0NOT", P_not},
    {"0SHIFT", P_shift},

    {"0DEPTH", P_depth},
    {"0CLEAR", P_clear},
    {"0DUP", P_dup},
    {"0DROP", P_drop},
    {"0SWAP", P_swap},
    {"0OVER", P_over},
    {"0PICK", P_pick},
    {"0ROT", P_rot},
    {"0-ROT", P_minusrot},
    {"0ROLL", P_roll},
    {"0>R", P_tor},
    {"0R>", P_rfrom},
    {"0R@", P_rfetch},

#ifdef SHORTCUTA
    {"01+", P_1plus},
    {"02+", P_2plus},
    {"01-", P_1minus},
    {"02-", P_2minus},
    {"02*", P_2times},
    {"02/", P_2div},
#endif /* SHORTCUTA */

#ifdef SHORTCUTC
    {"00=", P_0equal},
    {"00<>", P_0notequal},
    {"00>", P_0gtr},
    {"00<", P_0lss},
#endif /* SHORTCUTC */

#ifdef DOUBLE
    {"02DUP", P_2dup},
    {"02DROP", P_2drop},
    {"02SWAP", P_2swap},
    {"02OVER", P_2over},
    {"02ROT", P_2rot},
    {"02VARIABLE", P_2variable},
    {"02CONSTANT", P_2constant},
    {"02!", P_2bang},
    {"02@", P_2at},
#endif /* DOUBLE */

    {"0VARIABLE", P_variable},
    {"0CONSTANT", P_constant},
    {"0!", P_bang},
    {"0@", P_at},
    {"0+!", P_plusbang},
    {"0ALLOT", P_allot},
    {"0,", P_comma},
    {"0C!", P_cbang},
    {"0C@", P_cat},
    {"0C,", P_ccomma},
    {"0C=", P_cequal},
    {"0HERE", P_here},

#ifdef ARRAY
    {"0ARRAY", P_array},
#endif

#ifdef REAL
    {"0(FLIT)", P_flit},
    {"0F+", P_fplus},
    {"0F-", P_fminus},
    {"0F*", P_ftimes},
    {"0F/", P_fdiv},
    {"0FMIN", P_fmin},
    {"0FMAX", P_fmax},
    {"0FNEGATE", P_fneg},
    {"0FABS", P_fabs},
    {"0F=", P_fequal},
    {"0F<>", P_funequal},
    {"0F>", P_fgtr},
    {"0F<", P_flss},
    {"0F>=", P_fgeq},
    {"0F<=", P_fleq},
    {"0F.", P_fdot},
    {"0FLOAT", P_float},
    {"0FIX", P_fix},
#ifdef MATH
    {"0ACOS", P_acos},
    {"0ASIN", P_asin},
    {"0ATAN", P_atan},
    {"0ATAN2", P_atan2},
    {"0COS", P_cos},
    {"0EXP", P_exp},
    {"0LOG", P_log},
    {"0POW", P_pow},
    {"0SIN", P_sin},
    {"0SQRT", P_sqrt},
    {"0TAN", P_tan},
#endif /* MATH */
#endif /* REAL */

    {"0(NEST)", P_nest},
    {"0EXIT", P_exit},
    {"0(LIT)", P_dolit},
    {"0BRANCH", P_branch},
    {"0?BRANCH", P_qbranch},
    {"1IF", P_if},
    {"1ELSE", P_else},
    {"1THEN", P_then},
    {"0?DUP", P_qdup},
    {"1BEGIN", P_begin},
    {"1UNTIL", P_until},
    {"1AGAIN", P_again},
    {"1WHILE", P_while},
    {"1REPEAT", P_repeat},
    {"1DO", P_do},
    {"1?DO", P_qdo},
    {"1LOOP", P_loop},
    {"1+LOOP", P_ploop},
    {"0(XDO)", P_xdo},
    {"0(X?DO)", P_xqdo},
    {"0(XLOOP)", P_xloop},
    {"0(+XLOOP)", P_xploop},
    {"0LEAVE", P_leave},
    {"0I", P_i},
    {"0J", P_j},
    {"0QUIT", P_quit},
    {"0ABORT", P_abort},
    {"1ABORT\"", P_abortq},
#ifdef SLEEP
    {"0SLEEP", P_sleep},
    {"0LONGSLEEP", P_longsleep},
#endif

#ifdef SYSTEM
    {"0SYSTEM", P_system},
#endif
#ifdef TRACE
    {"0TRACE", P_trace},
#endif
#ifdef WALKBACK
    {"0WALKBACK", P_walkback},
#endif

#ifdef WORDSUSED
    {"0WORDSUSED", P_wordsused},
    {"0WORDSUNUSED", P_wordsunused},
#endif

#ifdef MEMSTAT
    {"0MEMSTAT", atl_memstat},
#endif

    {"0:", P_colon},
    {"1;", P_semicolon},
    {"0IMMEDIATE", P_immediate},
    {"1[", P_lbrack},
    {"0]", P_rbrack},
    {"0CREATE", P_create},
    {"0FORGET", P_forget},
    {"0DOES>", P_does},
    {"0'", P_tick},
    {"1[']", P_bracktick},
    {"0EXECUTE", P_execute},
    {"0>BODY", P_body},
    {"0STATE", P_state},

#ifdef DEFFIELDS
    {"0FIND", P_find},
    {"0>NAME", P_toname},
    {"0>LINK", P_tolink},
    {"0BODY>", P_frombody},
    {"0NAME>", P_fromname},
    {"0LINK>", P_fromlink},
    {"0N>LINK", P_nametolink},
    {"0L>NAME", P_linktoname},
    {"0NAME>S!", P_fetchname},
    {"0S>NAME!", P_storename},
#endif /* DEFFIELDS */

#ifdef COMPILERW
    {"1[COMPILE]", P_brackcompile},
    {"1LITERAL", P_literal},
    {"0COMPILE", P_compile},
    {"0<MARK", P_backmark},
    {"0<RESOLVE", P_backresolve},
    {"0>MARK", P_fwdmark},
    {"0>RESOLVE", P_fwdresolve},
#endif /* COMPILERW */

#ifdef CONIO
    {"0.", P_dot},
    {"0?", P_question},
    {"0CR", P_cr},
    {"0.S", P_dots},
    {"1.\"", P_dotquote},
    {"1.(", P_dotparen},
    {"0TYPE", P_type},
    {"0WORDS", P_words},
#endif /* CONIO */

#ifdef FILEIO
    {"0FILE", P_file},
    {"0FOPEN", P_fopen},
    {"0FCLOSE", P_fclose},
    {"0FDELETE", P_fdelete},
    {"0FGETS", P_fgetline},
    {"0FPUTS", P_fputline},
    {"0FREAD", P_fread},
    {"0FWRITE", P_fwrite},
    {"0FGETC", P_fgetc},
    {"0FPUTC", P_fputc},
    {"0FTELL", P_ftell},
    {"0FSEEK", P_fseek},
    {"0FLOAD", P_fload},
#endif /* FILEIO */

#ifdef EVALUATE
    {"0EVALUATE", P_evaluate},
#endif /* EVALUATE */
	
    {NULL, (codeptr) 0}
};
#endif

#ifdef WALKBACK

/*  PWALKBACK  --  Print walkback trace.  */

static void pwalkback(void)
{
    if (atl_walkback && ((curword != NULL) || (wbptr > wback))) {
        V printf("Walkback:\n");
	if (curword != NULL) {
            V printf("   %s\n", curword->name + 1);
	}
	while (wbptr > wback) {
	    dictword *wb = *(--wbptr);
            V printf("   %s\n", wb->name + 1);
	}
    }
}
#endif /* WALKBACK */

/*  TROUBLE  --  Common handler for serious errors.  */

static void trouble(const char *kind)
{
	DEBUG_E(kind);
	DEBUG_ENDL(NULL);
#ifdef WALKBACK
    pwalkback();
#endif /* WALKBACK */
    P_abort();			      /* Abort */
    state = Falsity;    /* Reset all interpretation state */
    stringlit =
	tickpend = ctickpend = False;
}

/*  ATL_ERROR  --  Handle error detected by user-defined primitive.  */

void atl_error(const char *kind)
{
    trouble(kind);
    evalstat = ATL_APPLICATION;       /* Signify application-detected error */
}

#ifndef NOMEMCHECK

/*  STAKOVER  --  Recover from stack overflow.	*/

Exported void stakover(void)
{
    trouble("Stack overflow");
    evalstat = ATL_STACKOVER;
}

/*  STAKUNDER  --  Recover from stack underflow.  */

Exported void stakunder(void)
{
    trouble("Stack underflow");
    evalstat = ATL_STACKUNDER;
}

/*  RSTAKOVER  --  Recover from return stack overflow.	*/

Exported void rstakover(void)
{
    trouble("Return stack overflow");
    evalstat = ATL_RSTACKOVER;
}

/*  RSTAKUNDER	--  Recover from return stack underflow.  */

Exported void rstakunder(void)
{
    trouble("Return stack underflow");
    evalstat = ATL_RSTACKUNDER;
}

/*  HEAPOVER  --  Recover from heap overflow.  Note that a heap
                  overflow does NOT wipe the heap; it's up to
		  the user to do this manually with FORGET or
		  some such. */

Exported void heapover(void)
{
    trouble("Heap overflow");
    evalstat = ATL_HEAPOVER;
}

/*  BADPOINTER	--  Abort if bad pointer reference detected.  */

Exported void badpointer(void)
{
    trouble("Bad pointer");
    evalstat = ATL_BADPOINTER;
}

/*  NOTCOMP  --  Compiler word used outside definition.  */

#if 0
static void notcomp(void)
{
    trouble("Compiler word outside definition");
    evalstat = ATL_NOTINDEF;
}

/*  DIVZERO  --  Attempt to divide by zero.  */

static void divzero(void)
{
    trouble("Divide by zero");
    evalstat = ATL_DIVZERO;
}
#endif

#endif /* !NOMEMCHECK */

/*  EXWORD  --	Execute a word (and any sub-words it may invoke). */

static void exword(dictword *wp)
{
    curword = wp;
#ifdef TRACE
    if (atl_trace) {
    	DEBUG_I("Trace: ");
    	DEBUG_STR(wp->name);
    	DEBUG_ENDL(NULL);
    }
#endif /* TRACE */
    (*curword->code)();	      /* Execute the first word */
    while (ip != NULL) {
#ifdef BREAK
	if (broken) {		      /* Did we receive a break signal */
        trouble("Break signal");
	    evalstat = ATL_BREAK;
	    break;
	}
#endif /* BREAK */
	curword = *ip++;
#ifdef TRACE
	if (atl_trace) {
    	DEBUG_I("Trace: ");
    	DEBUG_STR(curword->name);
    	DEBUG_ENDL(NULL);
	}
#endif /* TRACE */
	(*curword->code)();	      /* Execute the next word */
    }
    curword = NULL;
}

#ifdef TRACE
#define add_dict(word, wcode, wname) dict[word].code=wcode; dict[word].name = wname;
#else
#define add_dict(word, code, name) dict[word].code=code;
#endif

void atl_init(void)
{
	MemSet(dict, 0, sizeof(dict));

	// Recognized words
	add_dict(W_noop, P_noop, "noop")
	add_dict(W_lit, P_dolit, "lit")
	add_dict(W_plus, P_plus, "+")
	add_dict(W_print, P_print, "print")

	stk = stackbot = stack;
#ifdef MEMSTAT
	stackmax = stack;
#endif
	stacktop = stack + atl_stklen;
	rstk = rstackbot = rstack;
#ifdef MEMSTAT
	rstackmax = rstack;
#endif
	rstacktop = rstack + atl_rstklen;
#ifdef WALKBACK
	wbptr = wback;
#endif

	heapbot = heap;

	/* The system state word is kept in the first word of the heap
           so that pointer checking doesn't bounce references to it.
	   When creating the heap, we preallocate this word and initialise
	   the state to the interpretive state. */
	hptr = heap + 1;
	state = Falsity;
#ifdef MEMSTAT
	heapmax = hptr;
#endif
	heaptop = heap + atl_heaplen;
}

/*  ATL_LOOKUP	--  Look up a word in the dictionary.  Returns its
                    word item if found or NULL if the word isn't
		    in the dictionary. */

dictword *atl_lookup(int word)
{
    return lookup(word);
}

/*  ATL_BODY  --  Returns the address of the body of a word, given
		  its dictionary entry. */

stackitem *atl_body(dictword *dw)
{
    return ((stackitem *) dw) + Dictwordl;
}

/*  ATL_EXEC  --  Execute a word, given its dictionary address.  The
                  evaluation status for that word's execution is
		  returned.  The in-progress evaluation status is
		  preserved. */

int atl_exec(dictword *dw)
{
    int sestat = evalstat, restat;

    evalstat = ATL_SNORM;
#ifdef BREAK
    broken = False;		      /* Reset break received */
#endif
#undef Memerrs
#define Memerrs evalstat
    Rso(1);
    Rpush = ip; 		      /* Push instruction pointer */
    ip = NULL;			      /* Keep exword from running away */
    exword(dw);
    if (evalstat == ATL_SNORM) {      /* If word ran to completion */
	Rsl(1);
	ip = R0;		      /* Pop the return stack */
	Rpop;
    }
#undef Memerrs
#define Memerrs
    restat = evalstat;
    evalstat = sestat;
    return restat;
}

/*  ATL_MARK  --  Mark current state of the system.  */

void atl_mark(atl_statemark *mp)
{
    mp->mstack = stk;		      /* Save stack position */
    mp->mheap = hptr;		      /* Save heap allocation marker */
    mp->mrstack = rstk; 	      /* Set return stack pointer */
}

/*  ATL_UNWIND	--  Restore system state to previously saved state.  */

void atl_unwind(atl_statemark *mp)
{
    stk = mp->mstack;		      /* Roll back stack allocation */
    hptr = mp->mheap;		      /* Reset heap state */
    rstk = mp->mrstack; 	      /* Reset the return stack */
}

#ifdef BREAK

/*  ATL_BREAK  --  Asynchronously interrupt execution.	Note that this
		   function only sets a flag, broken, that causes
		   exword() to halt after the current word.  Since
                   this can be called at any time, it daren't touch the
		   system state directly, as it may be in an unstable
		   condition. */

void atl_break(void)
{
    broken = True;		      /* Set break request */
}
#endif /* BREAK */

static uint16 next_arg(void) {
	if (instream >= stream_end) {
		return 0;
	}
	return *instream++;
}

//static Token next_token(void) {
//	return next_arg();
//}
#define next_token() next_arg()

/*  ATL_EVAL  --  Evaluate a string containing ATLAST words.  */

int atl_eval(const int *stream, int stream_length)
{
    Token i;

#undef Memerrs
#define Memerrs evalstat
    instream = stream;
    stream_end = instream+stream_length;
    evalstat = ATL_SNORM;	      /* Set normal evaluation status */
#ifdef BREAK
    broken = False;		      /* Reset asynchronous break */
#endif
    
    while ((evalstat == ATL_SNORM) && (i = next_token()) != T_null) {

	switch (i) {
	    case T_word:
		if (tickpend) {
		    tickpend = False;
		    dictword * di = lookup(next_arg());
		    if (di != NULL) {
		    	So(1);
			   	Push = ptr_to_stackitem(di); /* Push word compile address */
		    } else {
		    	evalstat = ATL_UNDEFINED;
		    }
		}
		else {
		    dictword * di = lookup(next_arg());
		    if (di != NULL) {
                        /* Test the state.  If we're interpreting, execute
                           the word in all cases.  If we're compiling,
			   compile the word unless it is a compiler word
			   flagged for immediate execution by the
			   presence of a space as the first character of
			   its name in the dictionary entry. */
			if (state &&
			    (cbrackpend || ctickpend ||
			     !(di->flags & IMMEDIATE))) {
			    if (ctickpend) {
				/* If a compile-time tick preceded this
				   word, compile a (lit) word to cause its
				   address to be pushed at execution time. */
				Ho(1);
				Hstore = ptr_to_stackitem(lookup(W_lit));   /* Push (lit) */
				ctickpend = False;
			    }
			    cbrackpend = False;
			    Ho(1);	  /* Reserve stack space */
			    Hstore = ptr_to_stackitem(di);/* Compile word address */
			} else {
			    exword(di);   /* Execute word */
			}
		    } else {
			evalstat = ATL_UNDEFINED;
			state = Falsity;
		    }
		}
		break;

	    case T_int:
		if (state) {
		    Ho(2);
		    Hstore = ptr_to_stackitem(lookup(W_lit));   /* Push (lit) */
		    Hstore = next_arg();  /* Compile actual literal */
		} else {
		    So(1);
		    Push = next_arg();
		}
		break;

#ifdef REAL
	    case T_real:
		if (state) {
		    int i;
    	    	    union {
		    	atl_real r;
			stackitem s[Realsize];
		    } tru;

		    Ho(Realsize + 1);
		    Hstore = s_flit;  /* Push (flit) */
    	    	    tru.r = tokreal;
		    for (i = 0; i < Realsize; i++) {
			Hstore = tru.s[i];
		    }
		} else {
		    int i;
    	    	    union {
		    	atl_real r;
			stackitem s[Realsize];
		    } tru;

		    So(Realsize);
    	    	    tru.r = tokreal;
		    for (i = 0; i < Realsize; i++) {
			Push = tru.s[i];
		    }
		}
		break;
#endif /* REAL */

	    default:
            DEBUG_E("Unknown return type from next_token()");
            DEBUG_ENDL(NULL);
		break;
	}
    }
    return evalstat;
}
