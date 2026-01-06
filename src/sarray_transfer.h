#ifndef GS_SARRAY_TRANSFER_H
#define GS_SARRAY_TRANSFER_H

#if !defined(GS_CRYSTAL_H)
#warning "sarray_transfer.h" requires "crystal.h"
#endif

/*
  High-level interface for the crystal router.
  Given an array of structs, transfers each to the process indicated
  by a field of the struct, which gets set to the source process on output.

  For the dynamic "array" type, see "mem.h".

  Requires a "crystal router" object:

    struct comm c;
    struct crystal cr;

    comm_init(&c, MPI_COMM_WORLD);
    crystal_init(&cr, &c);

  Example sarray_transfer usage:

    struct T { ...; uint proc; ...; };
    struct array A = null_array;
    struct T *p, *e;

    // resize A to 100 struct T's, fill up with data
    p = array_reserve(struct T, &A, 100), A.n=100;
    for(e=p+A.n;p!=e;++p) {
      ...
      p->proc = ...;
      ...
    }

    // array A represents the array
    //   struct T ar[A.n]    where &ar[0] == A.ptr
    // transfer ar[i] to processor ar[i].proc  for each i=0,...,A.n-1:

    sarray_transfer(struct T, A, proc,set_src, &cr);

    // now array A represents a different array with a different size
    //   struct T ar[A.n]    where &ar[0] == A.ptr
    // the ordering is arbitrary
    // if set_src != 0, ar[i].proc is set to the proc where ar[i] came from
    // otherwise ar[i].proc is unchanged (and == this proc id)

    // note: two calls of
    sarray_transfer(struct T, A, proc,1, &cr);
    // in a row should return A to its original state, up to ordering

  Cleanup:
    array_free(&A);
    crystal_free(&cr);
    comm_free(&c);

  Example sarray_transfer_ext usage:

    struct T { ... };
    struct array A;
    uint proc[A.n];

    // array A represents the array
    //   struct T ar[A.n]    where &ar[0] == A.ptr
    // transfer ar[i] to processor proc[i]  for each i=0,...,A.n-1:
    sarray_transfer_ext(struct T, &A, proc, &cr);

    // no information is available now on where each struct came from

  There is a variant of sarray_transfer that transfers multiple arrays without
  requiring the data to be packed in array of structs first. See sarray_transfer_soa.
*/

#define sarray_transfer_many GS_PREFIXED_NAME(sarray_transfer_many)
#define sarray_transfer_     GS_PREFIXED_NAME(sarray_transfer_    )
#define sarray_transfer_ext_ GS_PREFIXED_NAME(sarray_transfer_ext_)

uint sarray_transfer_many(
  struct array *const *const A, const unsigned *const size, const unsigned An,
  const int fixed, const int ext, const int set_src, const unsigned p_off,
  const uint *const restrict proc, const unsigned proc_stride,
  struct crystal *const cr);
void sarray_transfer_(struct array *const A, const unsigned size,
                      const unsigned p_off, const int set_src,
                      struct crystal *const cr);
void sarray_transfer_ext_(struct array *const A, const unsigned size,
                          const uint *const proc, const unsigned proc_stride,
                          struct crystal *const cr);

#define sarray_transfer(T,A,proc_field,set_src,cr) \
  sarray_transfer_(A,sizeof(T),offsetof(T,proc_field),set_src,cr)

#define sarray_transfer_ext(T,A,proc,proc_stride,cr) \
  sarray_transfer_ext_(A,sizeof(T),proc,proc_stride,cr)

/*
  Transfer multiple arrays.
  Input:
    cr:           crystal router object
    n_in:         number of entities to transfer per array
    dest:         destination ranks (size n_in)
    n_fields:     number of arrays to transfer
    byte_sizes:   array of sizes in bytes (size n_fields).
                  Each element of this array tells the size of a single entry in the corresponding array. e.g.,
                  for an array of double, byte_sizes[i] = sizeof(double).
    data_send:    array of pointers to input arrays (size n_fields).
                  data_send[i] points to the input array for field i
                  (must be contiguous in memory with size n_in*byte_sizes[i]).
  Output:
    Returns n_out (number of received elements after crystal router transfer).

  E.g. usage: Consider there are n_in = 10 particles in 3D space, and we have
  the following n_fields=3 associated with each particle:
  (i) coordinates [x, y, and z, each a double] - we assume that the coordinates
                  are packed as [x0,y0,z0,x1,y1,z1,...,x9,y9,z9].
  (ii) color [uint]
  (iii) and charge [double]
  These could be initialized on each rank after allocating memory:

  double *coords = tmalloc(double, n_in * dim);
  uint *color = tmalloc(uint, n_in);
  double *charge = tmalloc(double, n_in);

  .
  .
  assign coords, color, and charge for each particle
  .
  .

  Assuming we want to transfer these particles to ranks specified in dest:

  uint *dest = tmalloc(uint, n_in);
  .
  .
  assign destination for each particle
  .
  .

  To transfer the particles, first specify byte size for each field:
  size_t sizes[] = {sizeof(double)*dim, sizeof(uint), sizeof(double)};

  Then specify the pointers to each of the fields to transfer:
  n_fields = 3
  void **data_send = tmalloc(void*, n_fields);
  data_send[0] = coords
  data_send[1] = color
  data_send[2] = charge

  Note that the information in data_send should be consistent with the byte sizes specified in sizes.

  uint n_out = sarray_transfer_soa_to_buffer(&cr, n_in, dest, n_fields, sizes, data_send);

  This returns number of particles received after transfer. The data for each
  particle is currently stored in crystal router buffer and must be unpacked
  into user allocated data memory. Note that the crystal object must not be
  modified until the data is unpacked using sarray_transfer_unpack_buffer_to_soa.

  Note since "coords" is setup such that the x,y,z coordinate of
  each particle is contiguously packed, we specify the byte_size for coordinates to be sizeof(double)*dim. This allows the crystal router
  to pack/unpack the xyz coordinate of each particle via a single memcpy.
  If instead the coordinates are packed
  as [x0,x1,x2,..xN,y0,y1,...yN,z0,...zN], they should be transferred as
  three separate fields, each with byte_size = sizeof(double)
  and corresponding pointer in memory. In that case,

  n_fields = 5
  void **data_send = tmalloc(void*, n_fields);
  data_send[0] = coords
  data_send[1] = coords + n_in
  data_send[2] = coords + 2*n_in
  data_send[3] = color
  data_send[4] = charge

  size_t sizes[] = {sizeof(double), sizeof(double), sizeof(double),
                    sizeof(uint), sizeof(double)};
*/
uint sarray_transfer_soa_to_buffer(struct crystal *cr, const uint n_in,
                                   const uint *dest, int n_fields,
                                   const size_t *byte_sizes, void **data_send);

/*
  Unpack transferred data from crystal router's internal buffer into provided output arrays. Must be called immediately after sarray_transfer_soa_to_buffer, and before any other crystal operation that may overwrite cr->data.

  Input:
    cr: crystal router object
    n_out: number of received elements (returned by
                                        sarray_transfer_soa_to_buffer)
    n_fields: number of data fields
    byte_sizes: array of sizes (in bytes) for each field (size n_fields)

  Output:
    rank_recv: array to store source ranks (size n_out).
              Caller must allocate this. Only specify it if keep_src was true
              in sarray_transfer_soa_to_buffer.
    data_recv: array of pointers to output arrays (size n_fields).
              data_recv[k] must point to a buffer of size n_out*byte_sizes[k].
              Caller must allocate these buffers.

  Following the example from sarray_transfer_soa_to_buffer, the user can unpack
  received information by allocating memory for received particles

  rank_recv = tmalloc(uint, n_out)
  void **data_recv = tmalloc(void*, n_fields);
  data_recv[0] = tmalloc(double, n_out*dim)
  data_recv[1] = tmalloc(uint, n_out)
  data_recv[2] = tmalloc(double, n_out)

  sarray_transfer_unpack_buffer_to_soa(&cr, n_out, n_fields, sizes, rank_recv, data_recv);

  Alternatively, see sarray_transfer_soa if you would like the crystal router
  to pack, transfer, and unpack in a single function call. In that method,
  the function allocates the output buffers (`*rank_recv` and `data_recv[k]`);
  the caller must free them with `free()`.
*/
void sarray_transfer_unpack_buffer_to_soa(struct crystal *cr, uint n_out,
                                          int n_fields,
                                          const size_t *byte_sizes,
                                          uint *rank_recv, void **data_recv);

/*
  Structure-of-Arrays (SoA).
  Transfers multiple arrays to different ranks directly without packing in
  array of structs first.
  (1) Uses sarray_transfer_soa_to_buffer to pack data in crystal router buffer
      and transfer.
  (2) Unpacks crystal router buffer post-transfer and returns the pointers
      to the unpacked data.
  Note the memory must be freed by the caller using free(ptr) for each
  pointer in data_recv.
*/
void sarray_transfer_soa(struct crystal *cr, const uint n_in, const uint *dest,
                         int n_fields, const size_t *byte_sizes,
                         void **data_send,
                         uint *n_out, uint **rank_recv, void **data_recv);

#endif
