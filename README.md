# GSLIB 

* Gather/Scatter
* Parallel direct solver (XXT)
* AMG solver 
* Robust spectral element interpolation

# Build Instructions

The build system relies on GNU Make with the `make` command. To compile gslib just run:

```
cd src
make CC=mpicc CFLAGS="-O2" ADDUS=1
```

This will create a library called `libgs.a`. They key `ADDUS` determines the name mangling (add underscore) for the Fortran interface. 

# Interface

tbd
