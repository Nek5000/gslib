/*------------------------------------------------------------------------------

  Crystal Router

  Accomplishes all-to-all communication in log P msgs per proc
  The routine is low-level; the format of the input/output is an
  array of integers, consisting of a sequence of messages with format:

      target proc
      source proc
      m
      integer
      integer
      ...
      integer  (m integers in total)

  Before crystal_router is called, the source of each message should be
  set to this proc id; upon return from crystal_router, the target of each
  message will be this proc id.

  Example Usage:

    struct crystal cr;

    crystal_init(&cr, &comm);    // makes an internal copy of comm

    crystal_set_verbose(&cr, 1); // (optional) set verbose=1

    crystal.data.n = ... ;  // total number of integers (not bytes!)
    buffer_reserve(&cr.data, crystal.n * sizeof(uint));
    ... // fill cr.data.ptr with messages
    crystal_router(&cr);

    crystal_free(&cr);

  ----------------------------------------------------------------------------*/

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "c99.h"
#include "name.h"
#include "fail.h"
#include "types.h"
#include "gs_defs.h"
#include "comm.h"
#include "mem.h"
#include <stdio.h>

#define crystal_init   GS_PREFIXED_NAME(crystal_init  )
#define crystal_set_verbose GS_PREFIXED_NAME(crystal_set_verbose)
#define crystal_set_max_msg GS_PREFIXED_NAME(crystal_set_max_msg)
#define crystal_free   GS_PREFIXED_NAME(crystal_free  )
#define crystal_router GS_PREFIXED_NAME(crystal_router)

#define CR_ALIGN 64UL      // cache size 64 bytes
#define CR_INT_MARGIN 4096UL // safety margin kept below INT_MAX for the byte cap
// CR_MAX_MSG: per-MPI-call byte ceiling (< INT_MAX, aligned). Overridable via
// -DCR_MAX_MSG=<bytes> to force the batched path at small sizes for testing.
#ifndef CR_MAX_MSG
#define CR_MAX_MSG (((ulong)INT_MAX - CR_INT_MARGIN) & ~(CR_ALIGN-1)) // align down
#endif
#ifndef CR_MAX_N
#define CR_MAX_N CR_MAX_MSG/sizeof(uint)
#endif

/* NOTE: this struct is duplicated in crystal.h; keep the two copies in sync. */
struct crystal {
  struct comm comm;
  buffer data, work;
  sint verbose;
  ulong max_msg; /* runtime per-MPI-call byte cap; 0 = compile-time CR_MAX_MSG */
};

/* effective per-call caps for this crystal (bytes / uints): field or default */
static ulong cr_max_msg(const struct crystal *p)
{ return p->max_msg ? p->max_msg : CR_MAX_MSG; }
static ulong cr_max_n(const struct crystal *p)
{ return cr_max_msg(p)/sizeof(uint); }

void crystal_init(struct crystal *p, const struct comm *comm)
{
  comm_dup(&p->comm, comm);
  buffer_init(&p->data,1000);
  buffer_init(&p->work,1000);
  p->verbose = 0;
  p->max_msg = 0;
}

void crystal_set_verbose(struct crystal *p, const sint verbose)
{
  p->verbose = verbose;
  if(verbose && p->comm.id==0)
    fprintf(stdout, "crystal_set_verbose: verbose=%d\n", p->verbose);
}

/* Set the per-MPI-call byte cap at runtime. 0 restores the compile-time
   default (CR_MAX_MSG). Values are clamped to (0, INT_MAX) and rounded down to
   CR_ALIGN; a positive value below one uint is a usage error (fail). This
   validation is what preserves the Plan A invariant: MPI byte counts must stay
   representable in an int. */
void crystal_set_max_msg(struct crystal *p, const ulong max_msg_bytes)
{
  ulong v = max_msg_bytes;
  if(v==0) {                     /* sentinel: use compile-time default */
    p->max_msg = 0;
  } else {
    if(v < CR_ALIGN)             /* must be at least one aligned block */
      fail(1,__FILE__,__LINE__,
           "crystal_set_max_msg: cap %llu bytes too small (< CR_ALIGN=%llu)",
           (unsigned long long)max_msg_bytes, (unsigned long long)CR_ALIGN);
    if(v >= (ulong)INT_MAX) v = (ulong)INT_MAX - CR_INT_MARGIN; /* clamp < int */
    v &= ~(CR_ALIGN-1);                                         /* align down  */
    p->max_msg = v;
  }
  if(p->verbose && p->comm.id==0)
    fprintf(stdout, "crystal_set_max_msg: max_msg=%llu bytes (0=default -> %llu)\n",
            (unsigned long long)p->max_msg, (unsigned long long)cr_max_msg(p));
}

void crystal_free(struct crystal *p)
{
  comm_free(&p->comm);
  buffer_free(&p->data);
  buffer_free(&p->work);
}

static void uintcpy(uint *dst, const uint *src, ulong n)
{
  if(dst+n<=src)    memcpy (dst,src,n*sizeof(uint));
  else if(dst!=src) memmove(dst,src,n*sizeof(uint));
}

static ulong crystal_move(struct crystal *p, uint cutoff, int send_hi)
{
  uint *src, *end;
  uint *keep = p->data.ptr, *send;
  ulong n = p->data.n, len;
  send = buffer_reserve(&p->work,n*sizeof(uint));
  /* len is ulong, but src[2] (per-message length field) is a uint on the wire,
     so 3+src[2] is a uint add: a single message is capped at UINT_MAX uints.
     Lifting this needs a wire-format change (F5/D2 in LOGBOOK_A.md). */
  if(send_hi) { /* send hi, keep lo */
    for(src=keep,end=keep+n; src<end; src+=len) {
      len = 3 + (ulong)src[2];
      if(src[0]>=cutoff) memcpy (send,src,len*sizeof(uint)), send+=len;
      else               uintcpy(keep,src,len),              keep+=len;
    }
  } else      { /* send lo, keep hi */
    for(src=keep,end=keep+n; src<end; src+=len) {
      len = 3 + (ulong)src[2];
      if(src[0]< cutoff) memcpy (send,src,len*sizeof(uint)), send+=len;
      else               uintcpy(keep,src,len),              keep+=len;
    }
  }
  p->data.n = keep - (uint*)p->data.ptr;
  return (ulong)(send - (uint*)p->work.ptr);
}

static ulong crystal_exchange(struct crystal *p, ulong send_n_long, uint targ,
                             int recvn, int tag)
{

  uint *recv[2];
  ulong count_long[2] = {0,0}, sum_long;
  enum { nr_max = 3*4 }; /* fixed request-pool size (not a VLA) */
  comm_req req[nr_max];

  if(recvn) // 1 or 2=recv
    comm_irecv(&req[1],&p->comm, &count_long[0],sizeof(ulong), targ        ,tag);
  if(recvn==2) // 2=recv more
    comm_irecv(&req[2],&p->comm, &count_long[1],sizeof(ulong), p->comm.id-1,tag);
  comm_isend(&req[0],&p->comm, &send_n_long,sizeof(ulong), targ,tag);
  comm_wait(req,recvn+1);

  sum_long = p->data.n + count_long[0] + count_long[1];
  if(p->verbose>1) {
    fprintf(stdout, "crystal_exchange: rank = %d  buf sizes"
          "  %llu  %llu  %llu  %llu\n", p->comm.id,
          (unsigned long long)p->data.n, (unsigned long long)count_long[0],
          (unsigned long long)count_long[1], (unsigned long long) sum_long);
    /* per-round data-flow record (for tests/viz_flow.py). keep = kept after
       crystal_move, send = shipped to targ, recv = received, hold = held after */
    fprintf(stdout, "CRFLOW round=%d rank=%d targ=%u recvn=%d"
          " send=%llu keep=%llu recv=%llu hold=%llu\n",
          tag/2, p->comm.id, targ, recvn,
          (unsigned long long)send_n_long, (unsigned long long)p->data.n,
          (unsigned long long)(count_long[0]+count_long[1]),
          (unsigned long long)sum_long);
        fflush(stdout);
  }

  buffer_reserve(&p->data,sum_long*sizeof(uint));
  recv[0] = (uint*)p->data.ptr + p->data.n, recv[1] = recv[0] + count_long[0];
  p->data.n = sum_long;

  const ulong maxn = cr_max_n(p); /* effective per-call uint cap (hoisted) */
  if(send_n_long > maxn ||
      count_long[0] > maxn ||
      count_long[1] > maxn) {

    ulong soff=0, sleft=send_n_long;
    ulong r1off=0, r1left=count_long[0];
    ulong r2off=0, r2left=count_long[1];

    sint nr = 0;
    while(sleft || r1left || r2left) {
      ulong sn=0, r1n=0, r2n=0;

      if(nr+3>nr_max){
        comm_wait(req,nr);
        nr = 0;
      }

      if(recvn && r1left) {
        r1n = (r1left > maxn) ? maxn : r1left;
        if(p->verbose>1) {
          fprintf(stdout,
            "CR %d tag=%d %s peer=%u size=%llu off=%llu left=%llu\n",
            p->comm.id, tag+1,
            "R1", targ,
            (unsigned long long)(r1n*sizeof(uint)),
            (unsigned long long)r1off,
            (unsigned long long)r1left);
          fflush(stdout);
        }
        comm_irecv(&req[nr++],&p->comm,
                   recv[0]+r1off,r1n*sizeof(uint),targ,tag+1);
      }

      if(recvn==2 && r2left) {
        r2n = (r2left > maxn) ? maxn : r2left;
        if(p->verbose>1) {
          fprintf(stdout,
            "CR %d tag=%d %s peer=%u size=%llu off=%llu left=%llu\n",
            p->comm.id, tag+1,
            "R2", p->comm.id-1,
            (unsigned long long)(r2n*sizeof(uint)),
            (unsigned long long)r2off,
            (unsigned long long)r2left);
          fflush(stdout);
        }
        comm_irecv(&req[nr++],&p->comm,
                   recv[1]+r2off,r2n*sizeof(uint),p->comm.id-1,tag+1);
      }

      if(sleft) {
        sn = (sleft > maxn) ? maxn : sleft;
        if(p->verbose>1) {
          fprintf(stdout,
            "CR %d tag=%d %s peer=%u size=%llu off=%llu left=%llu\n",
            p->comm.id, tag+1,
            "S ", targ,
            (unsigned long long)(sn*sizeof(uint)),
            (unsigned long long)soff,
            (unsigned long long)sleft);
          fflush(stdout);
        }
        comm_isend(&req[nr++],&p->comm,
                   (uint*)p->work.ptr+soff,sn*sizeof(uint),targ,tag+1);
      }

      r1off += r1n; r1left -= r1n;
      r2off += r2n; r2left -= r2n;
      soff  += sn;  sleft  -= sn;
    }
    if(nr>0) comm_wait(req,nr);

  } else {
    if(recvn)    comm_irecv(&req[1],&p->comm,
                            recv[0],count_long[0]*sizeof(uint), targ        ,tag+1);
    if(recvn==2) comm_irecv(&req[2],&p->comm,
                            recv[1],count_long[1]*sizeof(uint), p->comm.id-1,tag+1);
    comm_isend(&req[0],&p->comm, p->work.ptr,send_n_long*sizeof(uint), targ,tag+1);
    comm_wait(req,recvn+1);
  }

  return sum_long;
}

void crystal_router(struct crystal *p)
{
  uint bl=0, bh, nl;
  uint id = p->comm.id, n=p->comm.np;
  uint targ;
  int tag = 0; /* passed to comm_isend/irecv as int (matches MPI tag type) */
  ulong send_n_long, send_n_long_b;
  sint overflow;
  int send_hi, recvn;

  /* The batched protocol pairs ranks; both peers must chunk into the same
     number/sizes of messages, so the effective cap must be identical on every
     rank. crystal_set_max_msg is per-rank, so verify collectively (once). */
  if(n>1) {
    slong cap[2], wk[2];
    cap[0] = (slong)cr_max_n(p); cap[1] = -cap[0];
    comm_allreduce(&p->comm, gs_slong, gs_min, cap, 2, wk);
    if(cap[0] != -cap[1])
      fail(1,__FILE__,__LINE__,
           "crystal_router: max_msg differs across ranks (min cap %lld != max "
           "%lld uints); crystal_set_max_msg must be called collectively with "
           "the same value on every rank", (long long)cap[0], (long long)(-cap[1]));
  }

  while(n>1) {
    nl = (n+1)/2, bh = bl+nl; // if uneven, low has more
    send_hi = id<bh;
    send_n_long = crystal_move(p,bh,send_hi);

    if(p->verbose) {
      send_n_long_b = send_n_long * sizeof(uint);
      overflow = (send_n_long_b >= cr_max_msg(p));
      if (overflow) {
        fprintf(stdout, "crystal_router: rank %d pre-xchg batching send=%llu B "
          "(> cap=%llu B)\n", p->comm.id,
          (unsigned long long)send_n_long_b, (unsigned long long)cr_max_msg(p));
        fflush(stdout);
      }
    }

    recvn = 1, targ = n-1-(id-bl)+bl; // ideal: low - high pariwise
    if(id==targ) targ=bh, recvn=0; // recv nothing
    if(n&1 && id==bh) recvn=2; // recv from 2 partners
    send_n_long = crystal_exchange(p,send_n_long,targ,recvn,tag);
    if(id<bh) n=nl; else n-=nl,bl=bh;
    tag += 2;

    if(p->verbose) {
      send_n_long_b = send_n_long * sizeof(uint);
      overflow = (send_n_long_b >= cr_max_msg(p));
      if (overflow) {
        fprintf(stdout, "crystal_router: rank %d post-xchg buf=%llu B "
          "(> cap=%llu B)\n", p->comm.id,
          (unsigned long long)send_n_long_b, (unsigned long long)cr_max_msg(p));
        fflush(stdout);
      }
    }
  }
}
