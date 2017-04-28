# GSLIB 

* Gather/Scatter for nearest neighbor data exchange
* XXT solver (parallel direct solver)
* AMG solver 
* Robust spectral element interpolation for a given set of points

# Build Instructions

The build system relies on GNU Make with the `make` command. To compile gslib just run:

```
cd src
make CC=mpicc CFLAGS="-O2"
```

This will create a library called `libgs.a`. They key `ADDUS` determines the name mangling (add underscore) for the Fortran interface. 

# Interface for gs

tbd

# Interface for XXT/AMG solver

tbd

# Interface for interpolation

tbd

# Applications

**\[1]&#160;[https://github.com/Nek5000/Nek5000](https://github.com/Nek5000/Nek5000)**: Nek5000 is the open-source, highly-scalable, spectral element code.
