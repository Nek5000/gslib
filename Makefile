MPI ?= 1
ADDUS ?= 1
USREXIT ?= 0
NBC ?= 0
BLAS ?= 0
MKL ?= 0

CFLAGS ?= -O2
FFLAGS ?= -O2
ARFLAGS ?= cr

LIBNAME ?= gs
CPREFIX ?= gslib_
FPREFIX ?= fgslib_

STATIC ?= 1
SHARED ?= 0

SRCROOT = .
TESTDIR = $(SRCROOT)/tests
FTESTDIR = $(TESTDIR)/fortran
SRCDIR = $(SRCROOT)/src
INCDIR = $(SRCROOT)/src
LIBDIR = $(SRCROOT)/lib

DARWIN := $(filter Darwin,$(shell uname -s))
SO_EXT := $(if $(DARWIN),dylib,so)

ifneq (,$(strip $(DESTDIR)))
  INSTALL_ROOT = $(DESTDIR)
else
  INSTALL_ROOT = $(SRCROOT)/build
endif

ifneq (0,$(SHARED))
  ifneq (0,$(STATIC))
    $(warning Cannot build with both STATIC=1 and SHARED=1, setting SHARED=0)
    override SHARED = 0
  endif
endif

ifneq (0,$(SHARED))
  ifeq ($(filter -fPIC,$(CFLAGS)),)
    override CFLAGS += -fPIC
  endif
  ifneq ($(DARWIN),)
    override LDFLAGS += -install_name @rpath/lib$(LIBNAME).$(SO_EXT)
  endif
endif

$(shell >config.h)
ifneq (0,$(MPI))
  SN = GSLIB_USE_MPI
  G := $(G) -D$(SN)
  $(shell printf "#ifndef ${SN}\n#define ${SN}\n#endif\n" >>config.h)
  ifeq ($(origin CC),default)
    CC = mpicc
  endif
  ifeq ($(origin FC),default)
    FC = mpif77
  endif
endif

ifneq (0,$(ADDUS))
  SN = GSLIB_UNDERSCORE
  G := $(G) -D$(SN)
  $(shell printf "#ifndef ${SN}\n#define ${SN}\n#endif\n" >>config.h)
endif

SN = GSLIB_PREFIX
G := $(G) -D$(SN)=$(CPREFIX)
$(shell printf "#ifndef ${SN}\n#define ${SN} ${CPREFIX}\n#endif\n" >>config.h)

SN = GSLIB_FPREFIX
G := $(G) -D$(SN)=$(FPREFIX)
$(shell printf "#ifndef ${SN}\n#define ${SN} ${FPREFIX}\n#endif\n" >>config.h)

SN = GSLIB_USE_GLOBAL_LONG_LONG
G := $(G) -D$(SN)
$(shell printf "#ifndef ${SN}\n#define ${SN}\n#endif\n" >>config.h)

ifneq (0,$(USREXIT))
  G += -DGSLIB_USE_USR_EXIT
endif

ifneq (0,$(NBC))
  G += -DGSLIB_USE_NBC
endif

ifeq (0,$(BLAS))
  SN = GSLIB_USE_NAIVE_BLAS
  G := $(G) -D$(SN)
  $(shell printf "#ifndef ${SN}\n#define ${SN}\n#endif\n" >>config.h)
endif

ifeq (1,$(BLAS))
  SN = GSLIB_USE_CBLAS
  G := $(G) -D$(SN)
  $(shell printf "#ifndef ${SN}\n#define ${SN}\n#endif\n" >>config.h)
  ifeq (1,$(MKL))
    SN = GSLIB_USE_MKL
    G := $(G) -D$(SN)
    $(shell printf "#ifndef ${SN}\n#define ${SN}\n#endif\n" >>config.h)
  endif
endif

CCCMD = $(CC) $(CFLAGS) -I$(INCDIR) $(G)
FCCMD = $(FC) $(FFLAGS) -I$(INCDIR) $(G)

TESTS = $(TESTDIR)/sort_test $(TESTDIR)/sort_test2 $(TESTDIR)/sarray_sort_test \
        $(TESTDIR)/comm_test $(TESTDIR)/crystal_test \
        $(TESTDIR)/sarray_transfer_test $(TESTDIR)/gs_test \
        $(TESTDIR)/gs_test_gop_blocking $(TESTDIR)/gs_test_gop_nonblocking \
        $(TESTDIR)/gs_unique_test \
        $(TESTDIR)/findpts_el_2_test \
        $(TESTDIR)/findpts_el_2_test2 $(TESTDIR)/findpts_el_3_test \
        $(TESTDIR)/findpts_el_3_test2 $(TESTDIR)/findpts_local_test \
        $(TESTDIR)/findpts_test $(TESTDIR)/findpts_test_ms $(TESTDIR)/poly_test \
        $(TESTDIR)/lob_bnd_test $(TESTDIR)/obbox_test

FTESTS = $(FTESTDIR)/f-igs

GS = $(SRCDIR)/gs.o $(SRCDIR)/sort.o $(SRCDIR)/sarray_transfer.o \
     $(SRCDIR)/sarray_sort.o $(SRCDIR)/gs_local.o $(SRCDIR)/fail.o \
     $(SRCDIR)/crystal.o $(SRCDIR)/comm.o $(SRCDIR)/tensor.o

FWRAPPER = $(SRCDIR)/fcrystal.o $(SRCDIR)/findpts.o

INTP = $(SRCDIR)/findpts_local.o $(SRCDIR)/obbox.o $(SRCDIR)/poly.o \
       $(SRCDIR)/lob_bnd.o $(SRCDIR)/findpts_el_3.o $(SRCDIR)/findpts_el_2.o

.PHONY: all lib install tests clean objects

all : lib install

lib: $(if $(filter-out 0,$(STATIC)),$(SRCDIR)/lib$(LIBNAME).a) $(if $(filter-out 0,$(SHARED)),$(SRCDIR)/lib$(LIBNAME).$(SO_EXT))

$(SRCDIR)/lib$(LIBNAME).a: $(GS) $(FWRAPPER) $(INTP)
	$(AR) $(ARFLAGS) $@ $^
	ranlib $@

$(SRCDIR)/lib$(LIBNAME).$(SO_EXT): $(GS) $(FWRAPPER) $(INTP)
	$(CC) $(CFLAGS) -shared -o $@ $^ $(LDFLAGS)

install: lib
	@mkdir -p $(INSTALL_ROOT)/lib 2>/dev/null
	$(if $(filter-out 0,$(STATIC)),cp $(SRCDIR)/lib$(LIBNAME).a $(INSTALL_ROOT)/lib)
	$(if $(filter-out 0,$(SHARED)),cp $(SRCDIR)/lib$(LIBNAME).$(SO_EXT) $(INSTALL_ROOT)/lib)
	@mkdir -p $(INSTALL_ROOT)/include/gslib 2>/dev/null
	cp $(SRCDIR)/*.h $(INSTALL_ROOT)/include/gslib
	mv config.h $(INSTALL_ROOT)/include/gslib
	@printf '// Automatically generated file\n#include "gslib/gslib.h"\n' \
	  > $(INSTALL_ROOT)/include/gslib.h && chmod 644 $(INSTALL_ROOT)/include/gslib.h

tests: $(TESTS)

clean:
	$(RM) config.h $(SRCDIR)/*.o $(SRCDIR)/*.s $(SRCDIR)/*.a $(SRCDIR)/*.$(SO_EXT) $(TESTDIR)/*.o $(FTESTDIR)/*.o $(TESTS)

$(TESTS): % : %.c | lib install
	$(CC) $(CFLAGS) -I$(INSTALL_ROOT)/include $< -o $@ -L$(INSTALL_ROOT)/lib -l$(LIBNAME) -lm $(LDFLAGS)

$(FTESTS): % : %.o | lib install
	$(FCCMD) $^ -o $@ -L$(SRCDIR) -l$(LIBNAME)

%.o: %.c ; $(CCCMD) -c $< -o $@
%.o: %.f ; $(FCCMD) -c $< -o $@
%.s: %.c ; $(CCCMD) -S $< -o $@
objects: $(OBJECTS) ;
