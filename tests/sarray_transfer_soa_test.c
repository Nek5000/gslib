#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <mpi.h>
#include "gslib.h"

/* Helper to check values relative to epsilon */
#define ABS(x) ((x)<0 ? -(x) : (x))
#define TEQ(a,b) (ABS((a)-(b)) < 1e-9)

int main(int narg, char *arg[])
{
  int i;
  comm_ext world; int np;
  struct comm comm;
  struct crystal cr;
  int id = 0;
#ifdef GSLIB_USE_MPI
  MPI_Init(&narg,&arg);
  world = MPI_COMM_WORLD;
  MPI_Comm_size(world,&np);
  MPI_Comm_rank(world,&id);
#else
  world=0, np=1;
#endif

  comm_init(&comm, world);
  crystal_init(&cr, &comm);

  /* Fields:
    0: int (local index)
    1: uint[2] (e.g., color, type)
    2: double[3] (e.g., coords)
  */
  int n_fields = 3;
  size_t sizes[] = {sizeof(int), sizeof(uint)*2, 3*sizeof(double)};

  /* Each proc (except rank 0) sends 1 point to every proc (including self) */
  uint n_in = (uint)np;
  if (id == 0) { n_in = 0;}

  /* Allocate inputs */
  uint *dest = tmalloc(uint, n_in);
  void **data_send = tmalloc(void*, n_fields);
  int *in_0 = tmalloc(int, n_in);
  uint *in_1 = tmalloc(uint, n_in * 2); /* 2 uints per entry */
  double *in_2 = tmalloc(double, n_in * 3); /* 3D coords per entry */

  data_send[0] = in_0;
  data_send[1] = in_1;
  data_send[2] = in_2;
  /* Fill Data */
  for(i=0; i<n_in; ++i) {
    dest[i] = i;

    in_0[i] = i; /* local index */
    in_1[2*i] = (uint)(id * 10000 + i);
    in_1[2*i+1] = (uint)(id * 20000 + i);
    in_2[3*i + 0] = id + 0.1;
    in_2[3*i + 1] = id + 0.2;
    in_2[3*i + 2] = id + 0.3;
  }

  printf("Proc %d sending %d items (All-to-All)\n", id, n_in);

  /* Packs data and performs transfer */
  uint n_out = sarray_transfer_soa_to_buffer(&cr, n_in, dest, n_fields, sizes, data_send);

  printf("Proc %d received %d items\n", id, n_out);
  comm_barrier(&comm);

  /* Allocate memory to receive data */
  uint *rank_recv = NULL;
  void **data_recv = tmalloc(void*, n_fields);

  if(n_out > 0) {
    rank_recv = tmalloc(uint, n_out);
    data_recv[0] = tmalloc(int, n_out);
    data_recv[1] = tmalloc(uint, n_out * 2);
    data_recv[2] = tmalloc(double, n_out * 3);
  } else {
    data_recv[0] = NULL;
    data_recv[1] = NULL;
    data_recv[2] = NULL;
  }

  /* Unpack buffer into user allocated memory */
  sarray_transfer_unpack_buffer_to_soa(&cr, n_out, n_fields, sizes, rank_recv, data_recv);

  /* Verification */
  if(n_out != (uint)np-1) {
    if(id==0) printf("Proc %d ERROR: Expected %d items, got %d\n", id, np, n_out);
  } else {
    for(i=0; i<n_out; ++i) {
      int src = rank_recv[i]; /* Should be source rank */

      int val0 = ((int*)data_recv[0])[i]; /* local index from source */
      uint val1_a = ((uint*)data_recv[1])[2*i];
      uint val1_b = ((uint*)data_recv[1])[2*i+1];
      double *val2 = &((double*)data_recv[2])[3*i];

      int exp0 = id; /* source used local index equal to target rank */
      uint exp1_a = src * 10000 + id;
      uint exp1_b = src * 20000 + id;

      double exp2_x = src + 0.1;
      double exp2_y = src + 0.2;
      double exp2_z = src + 0.3;

      if(val0 != exp0) printf("Proc %d ERR: Field 0 (int) expected %d got %d\n", id, exp0, val0);
      if(val1_a != exp1_a) printf("Proc %d ERR: Field 1 (uint) expected %u got %u\n", id, exp1_a, val1_a);
      if(val1_b != exp1_b) printf("Proc %d ERR: Field 1 (uint) expected %u got %u\n", id, exp1_b, val1_b);

      if(!TEQ(val2[0], exp2_x)) printf("Proc %d ERR: Field 2 (x) expected %f got %f\n", id, exp2_x, val2[0]);
      if(!TEQ(val2[1], exp2_y)) printf("Proc %d ERR: Field 2 (y) expected %f got %f\n", id, exp2_y, val2[1]);
      if(!TEQ(val2[2], exp2_z)) printf("Proc %d ERR: Field 2 (z) expected %f got %f\n", id, exp2_z, val2[2]);
    }
  }

  printf("Proc %d finished verification.\n", id);

  /* Round-trip: send received data back to original source ranks using sarray_transfer_soa */
  uint n_in_back = n_out;
  uint *dest_back = rank_recv; /* send back to original sources */
  void **data_send_back = data_recv; /* send all received fields, including local index */

  uint n_out_back = 0;
  uint *rank_recv_back = NULL;
  void **data_recv_back = tmalloc(void*, n_fields);

  sarray_transfer_soa(&cr, n_in_back, dest_back, n_fields, sizes,
                      data_send_back, &n_out_back, &rank_recv_back, data_recv_back);

  /* Verify round-trip against original inputs using local index */
  if (n_out_back != n_in) {
      printf("Proc %d ERR: Expected %u items on round-trip, got %u\n", id, n_in, n_out_back);
  }
  for (i = 0; i < (int)n_out_back; ++i) {
    int idx = ((int*)data_recv_back[0])[i]; /* original local index */
    uint val1_a = ((uint*)data_recv_back[1])[2*i];
    uint val1_b = ((uint*)data_recv_back[1])[2*i+1];
    double *val2 = &((double*)data_recv_back[2])[3*i];

    uint exp1_a = in_1[2*idx];
    uint exp1_b = in_1[2*idx+1];
    double exp2_x = in_2[3*idx + 0];
    double exp2_y = in_2[3*idx + 1];
    double exp2_z = in_2[3*idx + 2];

    if (val1_a != exp1_a)
      printf("Proc %d ERR: RT Field 1a expected %u got %u (idx %u)\n", id, exp1_a, val1_a, idx);
    if (val1_b != exp1_b)
      printf("Proc %d ERR: RT Field 1b expected %u got %u (idx %u)\n", id, exp1_b, val1_b, idx);
    if (!TEQ(val2[0], exp2_x))
      printf("Proc %d ERR: RT Field 2x expected %f got %f (idx %u)\n", id, exp2_x, val2[0], idx);
    if (!TEQ(val2[1], exp2_y))
      printf("Proc %d ERR: RT Field 2y expected %f got %f (idx %u)\n", id, exp2_y, val2[1], idx);
    if (!TEQ(val2[2], exp2_z))
      printf("Proc %d ERR: RT Field 2z expected %f got %f (idx %u)\n", id, exp2_z, val2[2], idx);
  }

  printf("Proc %d finished verification of round-trip.\n", id);

  /* Cleanup initial inputs */
  if(dest) free(dest);
  if(data_send) {
    free(in_0); free(in_1); free(in_2);
    free(data_send);
  }

  /* Cleanup received data after first send */
  if(rank_recv) free(rank_recv);
  if(data_recv[0]) free(data_recv[0]);
  if(data_recv[1]) free(data_recv[1]);
  if(data_recv[2]) free(data_recv[2]);
  if(data_recv) free(data_recv);

  if(rank_recv_back) free(rank_recv_back);
  if(data_recv_back) {
    if(data_recv_back[0]) free(data_recv_back[0]);
    if(data_recv_back[1]) free(data_recv_back[1]);
    if(data_recv_back[2]) free(data_recv_back[2]);
    free(data_recv_back);
  }

  crystal_free(&cr);
  comm_free(&comm);
  MPI_Finalize();
  return 0;
}
