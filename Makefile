CC=mpicc
AR=ar

CPP=-DMPI -DUNDERSCORE -DGLOBAL_LONG_LONG  -DPREFIX=jl_

SOURCES = gs.c sort.c sarray_transfer.c sarray_sort.c \
gs_local.c crystal.c comm.c tensor.c fail.c fcrystal.c
OBJECTS = $(SOURCES:%.c=obj/%.o)

libgs.a: $(OBJECTS)
	$(AR) -rs $@ $(OBJECTS)

obj/%.o: src/%.c 
	mkdir -p obj
	$(CC) $(CPP) -c $< -o $@

clean:
	rm -rf obj
	rm libgs.a
