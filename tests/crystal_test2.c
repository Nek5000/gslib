/*----------------------------------------------------------------------------

  crystal_test2 - crystal-router capability, traffic, and all-to-all modes

  A small, low-level crystal-router test that:
    1. demonstrates the extended (>INT_MAX-byte) capability - a payload/pattern
       that corrupts under the pre-fix router transfers correctly now;
    2. shows network traffic flow - how data concentrates onto one rank
       (all-to-one), inspectable via verbose mode (-v) and tests/viz_traffic.py;
    3. exercises common all-to-all modes: chain (reverse perm), half-swap,
       and all-to-one.

  Every element has a global identity 'eg' and a deterministic payload
  f(eg,j) = (double)((ulong)eg*lxyz + j). After crystal_router, we verify
  DIRECTLY from cr.data (read eg from each message header, recompute f, compare)
  and count received elements. No receive-side arrays => all-to-one is feasible
  and there is no per-rank divide-by-zero. Pass/fail is global:
  total received == nelgt AND global L2 error ~ 0.

  Wire layout per message (uints): [target, source, m, eg, payload...]
    m = 1 + lxyz*sizeof(double)/sizeof(uint)   (identity + payload)

  To force the batched (chunked) path cheaply, rebuild libgs with a small cap,
  e.g. CFLAGS="-O2 -DCR_MAX_MSG=4096UL", then run at small -E.

  ----------------------------------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "gslib.h"

#define lx1 8
#define lxyz (lx1*lx1*lx1)

/* uint counts. Message body = identity (eg, a ulong) + payload (lxyz doubles). */
#define IDENT_N   ((ulong)sizeof(ulong)/sizeof(uint))    /* eg, in uints        */
#define PAYLOAD_N ((ulong)lxyz*sizeof(double)/sizeof(uint)) /* doubles, in uints*/
#define ROW_N     (IDENT_N + PAYLOAD_N)    /* msg body length (== length field) */
#define MSG_N     (3 + ROW_N)              /* header (targ,src,m) + body        */

static uint verbose = 0;
static int rank, np;

enum { NET_CHAIN=0, NET_HALF=1, NET_ALL2ONE=2 };

typedef struct {
  int verbose;
  int nelt;
  int network;
  long maxmsg; /* runtime CR_MAX_MSG cap in bytes; 0 = library default */
} options_t;

static const char *net_name(int n)
{
  switch(n) {
    case NET_CHAIN:   return "chain (reverse permutation)";
    case NET_HALF:    return "half-swap";
    case NET_ALL2ONE: return "all-to-one (rank 0)";
    default:          return "unknown";
  }
}

static void usage(const char *prog)
{
  fprintf(stderr,
    "Usage: %s [-E N] [-n N] [-M N] [-v] [-h]\n"
    "  -E, --nelt N    : elements per rank (default: 1024)\n"
    "  -n, --network N : all-to-all mode (0=chain, 1=half, 2=all2one)\n"
    "  -M, --maxmsg N  : runtime per-MPI-call cap in bytes (0=default;\n"
    "                    small values force the batched path, e.g. 4096)\n"
    "  -v, --verbose   : enable verbose output (traffic inspection)\n"
    "  -h, --help      : show this help\n",
    prog);
  fflush(stderr);
}

static uint parse_args(int argc, char **argv, options_t *opt)
{
  /* defaults */
  opt->verbose = 0;
  opt->nelt = 1024;
  opt->network = NET_CHAIN;
  opt->maxmsg = 0;

  for(int i = 1; i < argc; ++i) {

    if(strcmp(argv[i], "--verbose") == 0 || strcmp(argv[i], "-v") == 0) {
      opt->verbose = 1;

    } else if(strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
      usage(argv[0]);
      return 1;

    } else if(strcmp(argv[i], "--nelt") == 0 || strcmp(argv[i], "-E") == 0) {
      if(i + 1 >= argc) {
        fprintf(stderr, "Missing value for --nelt\n");
        usage(argv[0]);
        return 1;
      }
      opt->nelt = atoi(argv[++i]);

    } else if(strcmp(argv[i], "--network") == 0 || strcmp(argv[i], "-n") == 0) {
      if(i + 1 >= argc) {
        fprintf(stderr, "Missing value for --network\n");
        usage(argv[0]);
        return 1;
      }
      opt->network = atoi(argv[++i]);

    } else if(strcmp(argv[i], "--maxmsg") == 0 || strcmp(argv[i], "-M") == 0) {
      if(i + 1 >= argc) {
        fprintf(stderr, "Missing value for --maxmsg\n");
        usage(argv[0]);
        return 1;
      }
      opt->maxmsg = atol(argv[++i]);

    } else {
      fprintf(stderr, "Unknown option: %s\n", argv[i]);
      usage(argv[0]);
      return 1;
    }
  }

  if(opt->nelt <= 0) {
    fprintf(stderr, "nelt must be > 0 (got %d)\n", opt->nelt);
    return 1;
  }
  if(opt->network < 0 || opt->network > 2) {
    fprintf(stderr, "network must be 0, 1, or 2 (got %d)\n", opt->network);
    return 1;
  }
  return 0;
}

/* deterministic payload: distinct per (eg,j) so a wrong chunk offset shows up */
static double payload_val(const ulong eg, const uint j)
{
  return (double)((ulong)eg*lxyz + j);
}

/* target rank for global element eg under the chosen network mode */
static uint target_rank(const ulong eg, const uint nelt, const int imode)
{
  const ulong nelgt = (ulong)nelt*np;
  switch(imode) {
    case NET_CHAIN:   return (uint)((nelgt-1-eg)/nelt); /* reverse permutation */
    case NET_HALF: {                                    /* swap rank halves    */
      const uint half = np/2;
      const uint r = (uint)(eg/nelt);
      return (r < half) ? r+half : (np>1 ? r-half : 0);
    }
    case NET_ALL2ONE: return 0;                         /* concentrate on rank0*/
    default:          return 0;
  }
}

int main(int argc, char *argv[])
{
  comm_ext world;
  struct comm comm;
  struct crystal cr;
  uint nelt, e;
  ulong nelgt, *heg=NULL; /* heg[e] = global id of local element e */
  uint ierr=0, *data, *end;

#ifndef GSLIB_USE_MPI
  fail(1,__FILE__,__LINE__,"MPI is required\n");
#endif

  MPI_Init(&argc,&argv);
  world = MPI_COMM_WORLD;
  MPI_Comm_size(world,&np);
  MPI_Comm_rank(world,&rank);

  comm_init(&comm,world);

  options_t opt;
  if(comm.id==0) ierr = parse_args(argc, argv, &opt);
  comm_bcast(&comm, &ierr, sizeof(uint), 0);
  if(ierr) { comm_free(&comm); MPI_Finalize(); return ierr==1?0:EXIT_FAILURE; }

  comm_bcast(&comm, &opt, sizeof(options_t), 0);
  verbose = opt.verbose;

  nelt  = (uint)opt.nelt;
  nelgt = (ulong)np*nelt;

  if(comm.id == 0) {
    printf("crystal_test2: np=%d nelt=%u nelgt=%llu network=%d [%s] verbose=%d\n",
           np, nelt, (unsigned long long)nelgt, opt.network,
           net_name(opt.network), opt.verbose);
    printf("  payload=%d doubles/elem, msg=%llu uints/elem, "
           "send buf=%llu uints/rank  maxmsg=%ld bytes (0=default)\n", lxyz,
           (unsigned long long)MSG_N, (unsigned long long)((ulong)nelt*MSG_N),
           opt.maxmsg);
    fflush(stdout);
  }

  crystal_init(&cr,&comm);
  if(verbose) crystal_set_verbose(&cr, verbose+1); /* level 2: chunk traces */
  if(opt.maxmsg > 0) crystal_set_max_msg(&cr, (ulong)opt.maxmsg); /* runtime cap */

  /* local element identities */
  heg = tmalloc(ulong, nelt);
  for(e=0; e<nelt; e++) heg[e] = (ulong)rank*nelt + e;

  /* pack: [target, source, m, eg, payload...] per element */
  comm_barrier(&comm);
  double t_pack = comm_time();
  {
    const ulong data_n = (ulong)nelt*MSG_N;

    /* informational overflow notice (large-but-valid is fine; do not die) */
    if(verbose && (ulong)data_n*sizeof(uint) >= (ulong)INT_MAX)
      fprintf(stdout, "  note: rank %u send buf = %llu bytes (>= INT_MAX)\n",
              comm.id, (unsigned long long)((ulong)data_n*sizeof(uint)));

    cr.data.n = data_n;
    buffer_reserve(&cr.data, cr.data.n*sizeof(uint));

    data = cr.data.ptr;
    for(e=0; e<nelt; ++e, data+=MSG_N) {
      const ulong eg = heg[e];
      double pl[lxyz];
      data[0] = target_rank(eg, nelt, opt.network); /* target rank */
      data[1] = comm.id;                            /* source rank */
      data[2] = (uint)ROW_N;                        /* msg body length */
      memcpy(&data[3], &eg, sizeof(ulong));         /* global identity */
      for(uint j=0;j<lxyz;j++) pl[j] = payload_val(eg,j);
      /* payload sits at a 4-byte-aligned uint offset; memcpy avoids UB */
      memcpy(&data[3+IDENT_N], pl, lxyz*sizeof(double));
    }
  }
  comm_barrier(&comm);
  t_pack = comm_time() - t_pack;

  /* transfer */
  double t_xfer = comm_time();
  crystal_router(&cr);
  t_xfer = comm_time() - t_xfer;

  /* verify directly from cr.data: read eg, recompute payload, compare + count */
  comm_barrier(&comm);
  double t_chk = comm_time();
  double verr2 = 0.0, vref2 = 0.0;
  ulong nrecv = 0;
  data = cr.data.ptr; end = data + cr.data.n;
  for(; data!=end; data+=3+data[2]) {
    ulong eg; double pl[lxyz];
    memcpy(&eg, &data[3], sizeof(ulong));
    memcpy(pl, &data[3+IDENT_N], lxyz*sizeof(double));
    for(uint j=0;j<lxyz;j++) {
      const double ex = payload_val(eg,j), df = pl[j]-ex;
      verr2 += df*df; vref2 += ex*ex;
    }
    /* sanity: after routing, target field must equal this rank */
    if(data[0] != comm.id) verr2 += 1.0; /* mis-delivery -> force failure */
    nrecv++;
  }
  comm_barrier(&comm);
  t_chk = comm_time() - t_chk;

  /* global reductions: correctness is a global property */
  double gnorm[2] = {verr2, vref2}, gbuf[2];
  comm_allreduce(&comm,gs_double,gs_add,gnorm,2,gbuf);
  slong gcount = (slong)nrecv, cbuf;
  comm_allreduce(&comm,gs_long,gs_add,&gcount,1,&cbuf);

  const double rel_l2 = (gnorm[1]>0.0) ? sqrt(gnorm[0]/gnorm[1]) : sqrt(gnorm[0]);

  if(comm.id == 0) {
    printf("Time:  pack/xfer/chk  %.4e %.4e %.4e  total=%.4e sec\n",
           t_pack, t_xfer, t_chk, t_pack+t_xfer+t_chk);
    printf("Recv:  total=%lld  expected=%llu  %s\n",
           (long long)gcount, (unsigned long long)nelgt,
           ((ulong)gcount==nelgt)?"OK":"MISMATCH");
    printf("Error: rel_l2=%e\n", rel_l2);
    fflush(stdout);
  }

  comm_barrier(&comm);
  ierr = ((ulong)gcount==nelgt && rel_l2<1e-12) ? 0 : 1;
  if(!ierr) {
    if(comm.id==0)
      diagnostic("",__FILE__,__LINE__,
        "test successful  np=%d nelt=%u network=%d (rel_l2=%.2e)",
        np, nelt, opt.network, rel_l2);
  } else {
    fail(1,__FILE__,__LINE__,
      "failure: recv=%lld/%llu rel_l2=%.4e",
      (long long)gcount,(unsigned long long)nelgt, rel_l2);
  }

  free(heg);
  crystal_free(&cr);
  comm_free(&comm);
  MPI_Finalize();
  return 0;
}
