#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <float.h>
#include <math.h>
#include <time.h>
#include "gslib.h"
#include "rand_elt_test.h"

#define D 3

#if D==3
#define INITD(a,b,c) {a,b,c}
#define MULD(a,b,c) ((a)*(b)*(c))
#define INDEXD(a,na, b,nb, c) (((c)*(nb)+(b))*(na)+(a))
#define findpts_data             findpts_data_3
#define findpts_setup            findpts_setup_3
#define findpts_free             findpts_free_3
#define findpts                  findpts_3
#define findpts_eval             findpts_eval_3
#define findpts_fast_eval        findpts_fast_eval_3
#define findpts_fast_eval_data   findpts_fast_eval_data_3
#define findpts_fast_eval_free   findpts_fast_eval_free_3
#define findpts_fast_eval_setup  findpts_fast_eval_setup_3
#elif D==2
#define INITD(a,b,c) {a,b}
#define MULD(a,b,c) ((a)*(b))
#define INDEXD(a,na, b,nb, c) ((b)*(na)+(a))
#define findpts_data             findpts_data_2
#define findpts_setup            findpts_setup_2
#define findpts_free             findpts_free_2
#define findpts                  findpts_2
#define findpts_fast_eval        findpts_fast_eval_2
#define findpts_fast_eval_data   findpts_fast_eval_data_2
#define findpts_fast_eval_free   findpts_fast_eval_free_2
#define findpts_fast_eval_setup  findpts_fast_eval_setup_2
#endif

#define NR 5
#define NS 7
#define NT 6
#define K 4
#define NEL MULD(K,K,K)
#define TN 4

#define NPT_MAX 256
#define BBOX_TOL 0.01
#define NEWT_TOL 1024*DBL_EPSILON
#define LOC_HASH_SIZE NEL*MULD(NR,NS,NT)
#define GBL_HASH_SIZE NEL*MULD(NR,NS,NT)

/*
#define NPT_MAX 256
#define BBOX_TOL 1.00
#define NEWT_TOL 1024*DBL_EPSILON
#define LOC_HASH_SIZE NEL*3
#define GBL_HASH_SIZE NEL*3
*/

static uint np, id;

static const unsigned nr[D] = INITD(NR,NS,NT);
static const unsigned mr[D] = INITD(2*NR,2*NS,2*NT);
static double zr[NR], zs[NS], zt[NT];
static double x3[D][MULD(3,3,3)];
static double mesh[D][NEL*MULD(NR,NS,NT)];
static const double *const elx[D] = INITD(mesh[0],mesh[1],mesh[2]);

struct pt_data { double x[D], r[D], dist2, ex[D], fx[D]; uint code, proc, el; };
static struct array testp;

static struct crystal cr;

static double quad_eval(const double coef[MULD(3,3,3)], const double r[D])
{
  double lr0[D], lr1[D], lr2[D];
  unsigned d;
  for(d=0;d<D;++d) lr0[d]=r[d]*(r[d]-1)/2,
                   lr1[d]=(1+r[d])*(1-r[d]),
                   lr2[d]=r[d]*(r[d]+1)/2;
  #define EVALR(base) ( coef [base   ]*lr0[0] \
                       +coef [base+ 1]*lr1[0] \
                       +coef [base+ 2]*lr2[0] )
  #define EVALS(base) ( EVALR(base   )*lr0[1] \
                       +EVALR(base+ 3)*lr1[1] \
                       +EVALR(base+ 6)*lr2[1] )
  #define EVALT(base) ( EVALS(base   )*lr0[2] \
                       +EVALS(base+ 9)*lr1[2] \
                       +EVALS(base+18)*lr2[2] )
  #if D==2
  #  define EVAL() EVALS(0)
  #elif D==3
  #  define EVAL() EVALT(0)
  #endif
  
  return EVAL();
  
  #undef EVAL
  #undef EVALT
  #undef EVALS
  #undef EVALR
}

static void rand_mesh(void)
{
  const uint pn = ceil(pow(np,1.0/D));
  const uint pi=id%pn, pj=(id/pn)%pn;
  #if D==3
  const uint pk=(id/pn)/pn;
  #endif
  const double pfac = 1.0/pn;
  const double pbase[D] = INITD(-1+2*pfac*pi, -1+2*pfac*pj, -1+2*pfac*pk);
  const double fac = 1.0/K;
  const double z3[3] = {-1,0,1};
  unsigned ki,kj;
  #if D==3
  unsigned kk;
  rand_elt_3(x3[0],x3[1],x3[2], z3,3,z3,3,z3,3);
  #elif D==2
  rand_elt_2(x3[0],x3[1],       z3,3,z3,3);
  #endif
  if(id==0) printf("Global division: %u^%d\n",(unsigned)pn,D);
  #if D==3
  for(kk=0;kk<K;++kk)
  #endif
  for(kj=0;kj<K;++kj) for(ki=0;ki<K;++ki) {
    unsigned off = INDEXD(ki,K, kj,K, kk)*MULD(NR,NS,NT);
    unsigned i,j;
    double r[D], base[D] = INITD(-1+2*fac*ki,-1+2*fac*kj,-1+2*fac*kk);
    #if D==3
    unsigned k;
    for(k=0;k<NT;++k) { r[2]=pbase[2]+pfac*(1+base[2]+fac*(1+zt[k]));
    #endif
    for(j=0;j<NS;++j) { r[1]=pbase[1]+pfac*(1+base[1]+fac*(1+zs[j]));
    for(i=0;i<NR;++i) { r[0]=pbase[0]+pfac*(1+base[0]+fac*(1+zr[i]));
      mesh[0][off+INDEXD(i,NR, j,NS, k)] = quad_eval(x3[0],r);
      mesh[1][off+INDEXD(i,NR, j,NS, k)] = quad_eval(x3[1],r);
      #if D==3
      mesh[2][off+INDEXD(i,NR, j,NS, k)] = quad_eval(x3[2],r);
    }
    #endif
    }}
  }
}

static void test_mesh(void)
{
  const uint pn = ceil(pow(np,1.0/D));
  const uint pi=id%pn, pj=(id/pn)%pn;
  #if D==3
  const uint pk=(id/pn)/pn;
  #endif
  const double pfac = 1.0/pn;
  const double pbase[D] = INITD(-1+2*pfac*pi, -1+2*pfac*pj, -1+2*pfac*pk);
  const double fac = 1.0/K, step = 2.0/(K*(TN-1));
  unsigned ki,kj;
  #if D==3
  unsigned kk;
  #endif
  struct pt_data *out = testp.ptr;
  testp.n = NEL*MULD(TN,TN,TN);
  memset(testp.ptr,0,testp.n*sizeof(struct pt_data));
  #if D==3
  for(kk=0;kk<K;++kk)
  #endif
  for(kj=0;kj<K;++kj) for(ki=0;ki<K;++ki) {
    unsigned i,j;
    double r[D], base[D] = INITD(-1+2*fac*ki,-1+2*fac*kj,-1+2*fac*kk);
    #if D==3
    unsigned k;
    for(k=0;k<TN;++k) { r[2] = pbase[2]+pfac*(1+base[2]+step*k);
    #endif
    for(j=0;j<TN;++j) { r[1] = pbase[1]+pfac*(1+base[1]+step*j);
    for(i=0;i<TN;++i) { r[0] = pbase[0]+pfac*(1+base[0]+step*i);
      out->proc = rand()%np;
      out->x[0] = quad_eval(x3[0],r);
      out->x[1] = quad_eval(x3[1],r);
      #if D==3
      out->x[2] = quad_eval(x3[2],r);
      #endif
      ++out;
    }}
    #if D==3
    }
    #endif
  }
  sarray_transfer(struct pt_data,&testp,proc,1,&cr);
  if(0)
    printf("%u: %u shuffled points\n",id,(unsigned)testp.n);
}

static void print_ptdata(const struct comm *const comm)
{
  uint notfound=0;
  double dist2max=0, ed2max=0,edm2max=0,edf2max=0;
  const struct pt_data *pt = testp.ptr, *const end = pt+testp.n;
  for(;pt!=end;++pt) {
    if(0&&id==0)
    printf("code=%u, proc=%u, el=%u, dist2=%g, r=(%.17g,%.17g"
           #if D==3
           ",%.17g"
           #endif
           "), "
           "x=(%.17g,%.17g"
           #if D==3
           ",%.17g"
           #endif
           "), ex=(%.17g,%.17g"
           #if D==3
           ",%.17g"
           #endif
           ")\n",
      pt->code,pt->proc,pt->el,pt->dist2,
      pt->r[0],pt->r[1],
      #if D==3
      pt->r[2],
      #endif
      pt->x[0],pt->x[1],
      #if D==3
      pt->x[2],
      #endif
      pt->ex[0],pt->ex[1]
      #if D==3
      ,pt->ex[2]
      #endif
      );
    if(pt->code==2) ++notfound;
    else {
      double ed2=0, edm2=0,edf2=0, dx,dfx;
      unsigned d; for(d=0;d<D;++d) dx=pt->x[d]-pt->ex[d], ed2+=dx*dx;
                  for(d=0;d<D;++d) dfx=pt->x[d]-pt->fx[d], edf2+=dfx*dfx;
      dist2max=pt->dist2>dist2max?pt->dist2:dist2max;
      ed2max=ed2>ed2max?ed2:ed2max;
      edf2max=edf2>edf2max?edf2:edf2max;
    }
  }
  {
    double distmax=sqrt(dist2max), edmax=sqrt(ed2max), edfmax=sqrt(edf2max);
    slong total=testp.n;
    if(0)
    printf("%u: maximum distance = %g (adv), %g (eval);"
           " %u/%u points not found\n",
         (unsigned)id, distmax, edmax,
         (unsigned)notfound, (unsigned)testp.n);
    distmax = comm_reduce_double(comm,gs_max,&distmax,1);
    edmax   = comm_reduce_double(comm,gs_max,&edmax  ,1);
    edfmax   = comm_reduce_double(comm,gs_max,&edfmax  ,1);
    notfound = comm_reduce_sint(comm,gs_add,(sint*)&notfound,1);
    total    = comm_reduce_slong(comm,gs_add,&total,1);
    if(id==0)
      printf("maximum distance = %g (adv), \n %g (eval), \n %g (fast-eval), \n"
           " %u/%lu points not found\n",
           distmax, edmax,edfmax,
           (unsigned)notfound, (unsigned long)total);
  }
}

static void test(const struct comm *const comm)
{
  const double *x_base[D];
  const unsigned x_stride[D] = INITD(sizeof(struct pt_data),
                                     sizeof(struct pt_data),
                                     sizeof(struct pt_data));
  struct findpts_data *fd;
  struct findpts_fast_eval_data *fevd;
  struct pt_data *pt;
  unsigned d;
  if(id==0) printf("Initializing mesh\n");
  rand_mesh();
  test_mesh();
  pt = testp.ptr;
  if(id==0) printf("calling findpts_setup\n");
  fd=findpts_setup(comm,elx,nr,NEL,mr,BBOX_TOL,
                   LOC_HASH_SIZE,GBL_HASH_SIZE,
                   NPT_MAX,NEWT_TOL);
  if(id==0) printf("calling findpts\n");
  x_base[0]=pt->x, x_base[1]=pt->x+1;
  #if D==3
  x_base[2]=pt->x+2;
  #endif
  findpts(&pt->code , sizeof(struct pt_data),
          &pt->proc , sizeof(struct pt_data),
          &pt->el   , sizeof(struct pt_data),
           pt->r    , sizeof(struct pt_data),
          &pt->dist2, sizeof(struct pt_data),
           x_base   , x_stride, testp.n, fd);
clock_t begin = clock();
    for(d=0;d<D;++d) {
      if(id==0) printf("calling findpts_eval (%u)\n",d);
      findpts_eval(&pt->ex[d], sizeof(struct pt_data),
                   &pt->code , sizeof(struct pt_data),
                   &pt->proc , sizeof(struct pt_data),
                   &pt->el   , sizeof(struct pt_data),
                    pt->r    , sizeof(struct pt_data),
                    testp.n, mesh[d], fd);
    }
clock_t end = clock();
double time_spent = (double)(end - begin) / CLOCKS_PER_SEC;
double time_spent_tot;
time_spent_tot= comm_reduce_double(comm,gs_add,&time_spent,1);
if (id==0) {printf(" time spent approach old %f \n",time_spent_tot/np);}

begin = clock();
   fevd = findpts_fast_eval_setup(
                   &pt->code , sizeof(struct pt_data),
                   &pt->proc , sizeof(struct pt_data),
                   &pt->el   , sizeof(struct pt_data),
                    pt->r    , sizeof(struct pt_data),
                    testp.n, fd);
end = clock();
time_spent = (double)(end - begin) / CLOCKS_PER_SEC;
time_spent_tot= comm_reduce_double(comm,gs_add,&time_spent,1);
if (id==0) {printf(" time spent approach fast setup  %f \n",time_spent_tot/np);}

begin = clock();
    for(d=0;d<D;++d) {
      if(id==0) printf("calling findpts_fast_eval (%u)\n",d);
      findpts_fast_eval(&pt->fx[d], sizeof(struct pt_data),
                    mesh[d], fd, fevd);
    }
end = clock();
time_spent = (double)(end - begin) / CLOCKS_PER_SEC;
time_spent_tot= comm_reduce_double(comm,gs_add,&time_spent,1);
if (id==0) {printf(" time spent approach fast  %f \n",time_spent_tot/np);}


  findpts_free(fd);
  findpts_fast_eval_free(fevd);
  print_ptdata(comm);
}

int main(int narg, char *arg[])
{
  comm_ext world;
  struct comm comm;
  
#ifdef MPI
  MPI_Init(&narg,&arg);
  world = MPI_COMM_WORLD;
#else
  world=0;
#endif

  comm_init(&comm,world);
  id=comm.id, np=comm.np;

  lobatto_nodes(zr,NR),lobatto_nodes(zs,NS),lobatto_nodes(zt,NT);
  array_init(struct pt_data,&testp,NEL*MULD(TN,TN,TN));
  crystal_init(&cr,&comm);
  test(&comm);
  crystal_free(&cr);
  array_free(&testp);
  
  comm_free(&comm);

#ifdef MPI
  MPI_Finalize();
#endif

  return 0;
}