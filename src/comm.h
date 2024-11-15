#ifndef GS_COMM_H
#define GS_COMM_H

/* requires:
     <stddef.h>            for size_t
     <stdlib.h>            for exit
     "fail.h", "types.h"
     "gs_defs.h"           for comm_allreduce, comm_scan, comm_reduce_T
*/

#include <assert.h>
#include <string.h>
#if !defined(GS_FAIL_H) || !defined(GS_TYPES_H)
#warning "comm.h" requires "fail.h" and "types.h"
#endif

/*
  When the preprocessor macro GSLIB_USE_MPI is defined, defines (very) thin wrappers
  for the handful of used MPI routines. Alternatively, when GSLIB_USE_MPI is not defined,
  these wrappers become dummy routines suitable for a single process run.

  Basic usage:
  
    struct comm c;
  
    comm_init(&c, MPI_COMM_WORLD);  // initializes c using MPI_Comm_dup

    comm_free(&c);
  
  Very thin MPI wrappers: (see below for implementation)

    comm_send,_recv,_isend,_irecv,_time,_barrier
    
  Additionally, some reduction and scan routines are provided making use
    of the definitions in "gs_defs.h" (provided this has been included first).

  Example comm_allreduce usage:
    
    double v[5], buf[5];
    comm_allreduce(&c, gs_double,gs_add, v,5,buf);
      // Computes the vector sum of v across all procs, using
      // buf as a scratch area. Delegates to MPI_Allreduce if possible.
    
  Example comm_scan usage:
    
    long in[5], out[2][5], buf[2][5];
    comm_scan(out, &c,gs_long,gs_add, in,5,buf);
      // out[0] will be the vector sum of "in" across procs with ids
           *strictly* less than this one (exclusive behavior),
         and out[1] will be the vector sum across all procs, as would
           be computed with comm_allreduce.
         Note: differs from MPI_Scan which has inclusive behavior
  
  Example comm_reduce_double, etc. usage:
  
    T out, in[10];
    out = comm_reduce_T(&c, gs_max, in, 10);
      // out will equal the largest element of "in",
         across all processors
      // T can be "double", "float", "int", "long", "slong", "sint", etc.
         as defined in "gs_defs.h"
         
*/

#ifdef GSLIB_USE_MPI
#include <mpi.h>
typedef MPI_Comm comm_ext;
typedef MPI_Request comm_req;
#else
typedef int comm_ext;
typedef int comm_req;
typedef int MPI_Fint;
#endif

#define comm_allreduce  GS_PREFIXED_NAME(comm_allreduce )
#define comm_iallreduce GS_PREFIXED_NAME(comm_iallreduce)
#define comm_scan       GS_PREFIXED_NAME(comm_scan      )
#define comm_dot        GS_PREFIXED_NAME(comm_dot       )

/* global id, np vars strictly for diagnostic messages (fail.c) */
#ifndef comm_gbl_id
#define comm_gbl_id GS_PREFIXED_NAME(comm_gbl_id)
#define comm_gbl_np GS_PREFIXED_NAME(comm_gbl_np)
extern uint comm_gbl_id, comm_gbl_np;
#endif

struct comm {
  uint id, np;
  comm_ext c;
};

static void comm_init(struct comm *c, comm_ext ce);
/* (macro) static void comm_init_check(struct comm *c, MPI_Fint ce, uint np); */
/* (macro) static void comm_dup(struct comm *d, const struct comm *s); */
/* (macro) static void comm_split(const struct comm *s, int bin, int key, struct comm *d); */
static void comm_free(struct comm *c);
static double comm_time(void);
static void comm_barrier(const struct comm *c);
static void comm_recv(const struct comm *c, void *p, size_t n,
                      uint src, int tag);
static void comm_send(const struct comm *c, void *p, size_t n,
                      uint dst, int tag);
static void comm_irecv(comm_req *req, const struct comm *c,
                       void *p, size_t n, uint src, int tag);
static void comm_isend(comm_req *req, const struct comm *c,
                       void *p, size_t n, uint dst, int tag);
static void comm_wait(comm_req *req, int n);
static void comm_bcast(const struct comm *c, void *p, size_t n,
		       uint root);
double comm_dot(const struct comm *comm, double *v, double *w,
		uint n);
static void comm_gather(const struct comm *c, void *out, size_t out_n,
		        void *in, size_t in_n, uint root);

#ifdef GS_DEFS_H
void comm_allreduce(const struct comm *com, gs_dom dom, gs_op op,
                          void *v, uint vn, void *buf);
void comm_iallreduce(comm_req *req, const struct comm *com, gs_dom dom, gs_op op,
                          void *v, uint vn, void *buf);
void comm_scan(void *scan, const struct comm *com, gs_dom dom, gs_op op,
               const void *v, uint vn, void *buffer);

#define DEFINE_REDUCE(T) \
T GS_PREFIXED_NAME(comm_reduce__##T)( \
    const struct comm *comm, gs_op op, const T *in, uint n); \
static T comm_reduce_##T(const struct comm *c, gs_op op, const T *v, uint vn) \
{ return GS_PREFIXED_NAME(comm_reduce__##T)(c,op,v,vn); }
GS_FOR_EACH_DOMAIN(DEFINE_REDUCE)
#undef DEFINE_REDUCE

#define comm_reduce_sint \
    GS_TYPE_LOCAL(comm_reduce_int,comm_reduce_long,comm_reduce_long_long)
#define comm_reduce_slong \
    GS_TYPE_GLOBAL(comm_reduce_int,comm_reduce_long,comm_reduce_long_long)

#endif

/*----------------------------------------------------------------------------
  Code for static (inline) functions
  ----------------------------------------------------------------------------*/

static void comm_init(struct comm *c, comm_ext ce)
{
#ifdef GSLIB_USE_MPI
  int i;
  MPI_Comm_dup(ce, &c->c);
  MPI_Comm_rank(c->c,&i), comm_gbl_id=c->id=i;
  MPI_Comm_size(c->c,&i), comm_gbl_np=c->np=i;
#else
  c->id = 0, c->np = 1;
#endif
}

static void comm_init_check_(struct comm *c, MPI_Fint ce, uint np,
                             const char *file, unsigned line)
{
#ifdef GSLIB_USE_MPI
  comm_init(c,MPI_Comm_f2c(ce));
  if(c->np != np)
    fail(1,file,line,"comm_init_check: passed P=%u, "
                     "but MPI_Comm_size gives P=%u",
                     (unsigned)np,(unsigned)c->np);
#else
  comm_init(c,0);
  if(np != 1)
    fail(1,file,line,"comm_init_check: passed P=%u, "
                     "but not compiled with -DMPI",(unsigned)np);
#endif
}
#define comm_init_check(c,ce,np) comm_init_check_(c,ce,np,__FILE__,__LINE__)


static void comm_dup_(struct comm *d, const struct comm *s,
                      const char *file, unsigned line)
{
  d->id = s->id, d->np = s->np;
#ifdef GSLIB_USE_MPI
  MPI_Comm_dup(s->c,&d->c);
#else
  if(s->np!=1) fail(1,file,line,"%s not compiled with -DMPI\n",file);
#endif
}
#define comm_dup(d,s) comm_dup_(d,s,__FILE__,__LINE__)

static void comm_split_(const struct comm *s, int bin, int key, struct comm *d,
                        const char *file, unsigned line) {
#if defined(GSLIB_USE_MPI)
  MPI_Comm nc;
  MPI_Comm_split(s->c, bin, key, &nc);
  comm_init(d, nc);
  MPI_Comm_free(&nc);
#else
  if(s->np!=1) fail(1,file,line,"%s not compiled with -DMPI\n",file);
#endif
}
#define comm_split(s, bin, key, d) comm_split_(s, bin, key, d, __FILE__, __LINE__)

static void comm_free(struct comm *c)
{
#ifdef GSLIB_USE_MPI
  MPI_Comm_free(&c->c);
#endif
}

static double comm_time(void)
{
#ifdef GSLIB_USE_MPI
  return MPI_Wtime();
#else
  return 0;
#endif
}

static void comm_barrier(const struct comm *c)
{
#ifdef GSLIB_USE_MPI
  MPI_Barrier(c->c);
#endif
}

static void comm_recv(const struct comm *c, void *p, size_t n,
                      uint src, int tag)
{
#ifdef GSLIB_USE_MPI
# ifndef MPI_STATUS_IGNORE
  MPI_Status stat;
  MPI_Recv(p,n,MPI_UNSIGNED_CHAR,src,tag,c->c,&stat);
# else  
  MPI_Recv(p,n,MPI_UNSIGNED_CHAR,src,tag,c->c,MPI_STATUS_IGNORE);
# endif
#endif
}

static void comm_send(const struct comm *c, void *p, size_t n,
                      uint dst, int tag)
{
#ifdef GSLIB_USE_MPI
  MPI_Send(p,n,MPI_UNSIGNED_CHAR,dst,tag,c->c);
#endif
}

static void comm_irecv(comm_req *req, const struct comm *c,
                       void *p, size_t n, uint src, int tag)
{
#ifdef GSLIB_USE_MPI
  MPI_Irecv(p,n,MPI_UNSIGNED_CHAR,src,tag,c->c,req);
#endif
}

static void comm_isend(comm_req *req, const struct comm *c,
                       void *p, size_t n, uint dst, int tag)
{
#ifdef GSLIB_USE_MPI
  MPI_Isend(p,n,MPI_UNSIGNED_CHAR,dst,tag,c->c,req);
#endif
}

static void comm_wait(comm_req *req, int n)
{
#ifdef GSLIB_USE_MPI
# ifndef MPI_STATUSES_IGNORE
  MPI_Status status[8];
  while(n>=8) MPI_Waitall(8,req,status), req+=8, n-=8;
  if(n>0) MPI_Waitall(n,req,status);
# else
  MPI_Waitall(n,req,MPI_STATUSES_IGNORE);
# endif  
#endif
}

static void comm_bcast(const struct comm *c, void *p, size_t n, uint root)
{
#ifdef GSLIB_USE_MPI
  MPI_Bcast(p,n,MPI_UNSIGNED_CHAR,root,c->c);
#endif
}

static void comm_gather(const struct comm *c, void *out, size_t out_n,
		void *in, size_t in_n, uint root)
{
#ifdef GSLIB_USE_MPI
  MPI_Gather(out,out_n,MPI_UNSIGNED_CHAR,in,in_n,MPI_UNSIGNED_CHAR,root,c->c);
#else
  assert(out_n == in_n);
  memcpy(in,out,out_n);
#endif
}
#endif
