#ifndef GS_CRYSTAL_H
#define GS_CRYSTAL_H

#if !defined(GS_COMM_H) || !defined(GS_MEM_H)
#warning "crystal.h" requires "comm.h" and "mem.h"
#endif

#define crystal_init   GS_PREFIXED_NAME(crystal_init  )
#define crystal_free   GS_PREFIXED_NAME(crystal_free  )
#define crystal_router GS_PREFIXED_NAME(crystal_router)

struct crystal {
  struct comm comm;
  buffer data, work;
};

void crystal_init(struct crystal *cr, const struct comm *comm);
void crystal_free(struct crystal *cr);
void crystal_router(struct crystal *cr);

#endif
