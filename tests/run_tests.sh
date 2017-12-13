#!/bin/sh

# Set the MPI command and number of MPI ranks
MPI=mpirun
n=3

# Build the tests if they have not been built
# before.
make -C .. CC=mpicc CFLAGS="-O2 -g" tests

for i in *.o; do
  j=${i%.*}
  $MPI -np $n ./$j >> test_log
  if [ "$?" -eq 0 ]; then
    echo "Running test: $j ... Passed."
  else
    echo "Running test: $j ... Failed."
  fi
done
