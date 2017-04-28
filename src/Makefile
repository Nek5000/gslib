MPI ?= 1
ADDUS ?= 1
LIBNAME ?= gs
PREFIX ?= gslib
CBLAS ?= 0

CFLAGS ?= -O2

ifeq (0,$(MPI))
  LIBNAME = gsserial
else
  CPPFLAGS += -DMPI
endif

ifneq (0,$(ADDUS))
  CPPFLAGS += -DUNDERSCORE
endif

CPPFLAGS += -DPREFIX=$(PREFIX) -DUSE_NAIVE_BLAS -DGLOBAL_LONG_LONG -DAMG_BLOCK_ROWS=2400
# CPPFLAGS += -DPRINT_MALLOCS=1
# CPPFLAGS += -DGS_TIMING -DGS_BARRIER
# CPPFLAGS += -DUSE_NAIVE_BLAS

CPPFLAGS += -I.
LDLIBS += -l$(LIBNAME)
LDFLAGS += -L. 

RAND_TESTS = obbox_test findpts_el_2_test2 findpts_el_2_test findpts_el_3_test2 findpts_el_3_test \
						 findpts_local_test findpts_test

NORAND_TESTS = sort_test sort_test2 sarray_sort_test spchol_test \
      comm_test crystal_test sarray_transfer_test \
      gs_test gs_test_old gs_unique_test \
      xxt_test xxt_test2 crs_test \
      poly_test poly_test2 lob_bnd_test 

GS=gs.o sort.o sarray_transfer.o sarray_sort.o \
   gs_local.o fail.o crystal.o comm.o tensor.o

XXT=sparse_cholesky.o xxt.o

AMG=amg.o

FWRAPPER=fcrystal.o fcrs.o findpts.o

INTP=findpts_local.o obbox.o poly.o lob_bnd.o findpts_el_3.o findpts_el_2.o

.PHONY: all lib deps tests clean objects odepinfo

all : lib tests

lib: $(GS) $(XXT) $(AMG) $(FWRAPPER) $(INTP)
	$(AR) cr lib$(LIBNAME).a $?
	ranlib lib$(LIBNAME).a

tests: $(NORAND_TESTS) $(RAND_TESTS)

clean: 
	$(RM) *.o *.s *.a $(TESTS)

deps: 
	./cdep.py *.c > makefile.cdep;

odepinfo: deps objects
	@./odep_info.py *.o

-include makefile.cdep

$(NORAND_TESTS) : % : %.o lib
	$(CC) $(LDFLAGS) $< $(LDLIBS) -o $@

$(RAND_TESTS) : % : %.o rand_elt_test.o lib
	$(CC) $(LDFLAGS) $< rand_elt_test.o $(LDLIBS) -o $@
