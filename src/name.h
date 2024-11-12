#ifndef GS_NAME_H
#define GS_NAME_H

/* establishes some macros to establish
   * the FORTRAN naming convention
     default            gs_setup, etc.
     -DGSLIB_UPCASE     GS_SETUP, etc.
     -DGSLIB_UNDERSCORE gs_setup_, etc.
   * a prefix for all external (non-FORTRAN) function names
     for example, -DGSLIB_PREFIX=jl_   transforms fail -> jl_fail
   * a prefix for all external FORTRAN function names     
     for example, -DGSLIB_FPREFIX=jlf_ transforms gs_setup_ -> jlf_gs_setup_
*/

/* the following macro functions like a##b,
   but will expand a and/or b if they are themselves macros */
#define GS_TOKEN_PASTE_(a,b) a##b
#define GS_TOKEN_PASTE(a,b) GS_TOKEN_PASTE_(a,b)

#ifdef GSLIB_PREFIX
#  define GS_PREFIXED_NAME(x) GS_TOKEN_PASTE(GSLIB_PREFIX,x)
#else
#  define GS_PREFIXED_NAME(x) x
#endif

#ifdef GSLIB_FPREFIX
#  define GS_FPREFIXED_NAME(x) GS_TOKEN_PASTE(GSLIB_FPREFIX,x)
#else
#  define GS_FPREFIXED_NAME(x) x
#endif

#if defined(GSLIB_UPCASE)
#  define GS_FORTRAN_NAME(low,up) GS_FPREFIXED_NAME(up)
#  define GS_FORTRAN_UNPREFIXED(low,up) up
#elif defined(GSLIB_UNDERSCORE)
#  define GS_FORTRAN_NAME(low,up) GS_FPREFIXED_NAME(GS_TOKEN_PASTE(low,_))
#  define GS_FORTRAN_UNPREFIXED(low,up) GS_TOKEN_PASTE(low,_)
#else
#  define GS_FORTRAN_NAME(low,up) GS_FPREFIXED_NAME(low)
#  define GS_FORTRAN_UNPREFIXED(low,up) low
#endif

#endif

