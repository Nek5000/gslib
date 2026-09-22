#ifndef GS_CRYSTAL_H
#define GS_CRYSTAL_H

#if !defined(GS_COMM_H) || !defined(GS_MEM_H)
#warning "crystal.h" requires "comm.h" and "mem.h"
#endif

#define crystal_init   GS_PREFIXED_NAME(crystal_init  )
#define crystal_set_verbose GS_PREFIXED_NAME(crystal_set_verbose)
#define crystal_set_max_msg GS_PREFIXED_NAME(crystal_set_max_msg)
#define crystal_free   GS_PREFIXED_NAME(crystal_free  )
#define crystal_router GS_PREFIXED_NAME(crystal_router)

/* NOTE: this struct is duplicated in crystal.c; keep the two copies in sync. */
struct crystal {
  struct comm comm;
  buffer data, work;
  sint verbose;
  ulong max_msg; /* runtime per-MPI-call byte cap; 0 = compile-time CR_MAX_MSG */
};

void crystal_init(struct crystal *cr, const struct comm *comm);
void crystal_set_verbose(struct crystal *cr, const sint verbose);
void crystal_set_max_msg(struct crystal *cr, const ulong max_msg_bytes);
void crystal_free(struct crystal *cr);
void crystal_router(struct crystal *cr);

#endif
