#ifndef GS_SORT_H
#define GS_SORT_H

#if !defined(GS_TYPES_H) || !defined(GS_MEM_H)
#warning "sort.h" requires "types.h" and "mem.h"
/* types.h defines uint, ulong
   mem.h   defines buffer */
#endif

/*------------------------------------------------------------------------------
  
  Sort
  
  O(n) stable sort with good performance for all n

  sortv     (uint  *out,  const uint  *A, uint n, uint stride,  buffer *buf)
  sortv_long(ulong *out,  const ulong *A, uint n, uint stride,  buffer *buf)

  sortp     (buffer *buf, int perm_start,  const uint  *A, uint n, uint stride)
  sortp_long(buffer *buf, int perm_start,  const ulong *A, uint n, uint stride)

  A, n, stride : specifices the input (stride is in bytes!)
  out : the sorted values on output

  For the value sort, (sortv*)
    A and out may alias (A == out) exactly when stride == sizeof(T)

  For the permutation sort, (sortp*)
    The permutation can be both input (when start_perm!=0) and output,
    following the convention that it is always at the start of the buffer buf:
      uint *perm = buf->ptr;
    The permutation denotes the ordering
      A[perm[0]], A[perm[1]], ..., A[perm[n-1]]
    (assuming stride == sizeof(uint) or sizeof(ulong) as appropriate)
    and is re-arranged stably to give a sorted ordering.
    Specifying start_perm==0 is equivalent to specifying
      perm[i] = i,   i=0,...,n-1
    for an initial permutation (but may be faster).
    The buffer will be expanded as necessary to accomodate the permutation
    and the required scratch space.
  
  Most code calls these routines indirectly via the higher-level routine
    sarray_sort for sorting arrays of structs (see "sarray_sort.h").

  ----------------------------------------------------------------------------*/

#define sortv_ui     GS_PREFIXED_NAME(sortv_ui)
#define sortv_ul     GS_PREFIXED_NAME(sortv_ul)
#define sortv_ull    GS_PREFIXED_NAME(sortv_ull)
#define sortv_double GS_PREFIXED_NAME(sortv_double)
#define sortv_float GS_PREFIXED_NAME(sortv_float)
#define sortp_ui     GS_PREFIXED_NAME(sortp_ui)
#define sortp_ul     GS_PREFIXED_NAME(sortp_ul)
#define sortp_ull    GS_PREFIXED_NAME(sortp_ull)
#define sortp_double GS_PREFIXED_NAME(sortp_double)
#define sortp_float GS_PREFIXED_NAME(sortp_float)

#define sortv GS_TYPE_LOCAL(sortv_ui,sortv_ul,sortv_ull)
#define sortp GS_TYPE_LOCAL(sortp_ui,sortp_ul,sortp_ull)
#define sortv_long GS_TYPE_GLOBAL(sortv_ui,sortv_ul,sortv_ull)
#define sortp_long GS_TYPE_GLOBAL(sortp_ui,sortp_ul,sortp_ull)

void sortv_ui(unsigned *out, const unsigned *A, uint n, unsigned stride,
              buffer *restrict buf);
void sortv_ul(unsigned long *out,
              const unsigned long *A, uint n, unsigned stride,
              buffer *restrict buf);
void sortv_double(double *out,
              const double *A, uint n, unsigned stride,
              buffer *restrict buf);
void sortv_float(float *out,
              const float *A, uint n, unsigned stride,
              buffer *restrict buf);
uint *sortp_ui(buffer *restrict buf, int start_perm,
               const unsigned *restrict A, uint n, unsigned stride);
uint *sortp_ul(buffer *restrict buf, int start_perm,
               const unsigned long *restrict A, uint n, unsigned stride);
uint *sortp_double(buffer *restrict buf, int start_perm,
               const double *restrict A, uint n, unsigned stride);
uint *sortp_float(buffer *restrict buf, int start_perm,
               const float *restrict A, uint n, unsigned stride);
#if defined(GS_USE_LONG_LONG) || defined(GS_GLOBAL_LONG_LONG)
void sortv_ull(unsigned long long *out,
               const unsigned long long *A, uint n, unsigned stride,
               buffer *restrict buf);
uint *sortp_ull(buffer *restrict buf, int start_perm,
                const unsigned long long *restrict A, uint n, unsigned stride);
#endif

#endif
