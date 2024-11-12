#ifndef GS_C99_H
#define GS_C99_H

#ifndef __STDC_VERSION__
#  define GS_NO_C99
#elif __STDC_VERSION__ < 199901L
#  define GS_NO_C99
#endif

#ifdef GS_NO_C99
#  define restrict
#  define inline
#  undef GS_NO_C99
#endif

#endif
