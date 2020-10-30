#!/bin/bash


# Load system env only
source /etc/bashrc
source /etc/profile

# Make sure home is pointing to current directory
export PARTHENON=$(pwd)
cd ../
export HOME=$(pwd)
cd $PARTHENON
# Calculate number of available cores
export J=$(( $(nproc --all) )) && echo Using ${J} cores during build

COMPILER_MODULE=${15}
MPI_MODULE=${17}

export TMPDIR=${HOME}/tmp

# Temp directory apparently needed by spack because of OSError: [Errno 18] Invalid cross-device link
if [ -d ${TMPDIR} ] 
then
  echo "Removing ${TMPDIR}"
  rm -rf ${TMPDIR}
fi
echo "Creating tmp directory ${TMPDIR}"
mkdir ${TMPDIR}

compiler_version=$(bash $PARTHENON/scripts/darwin/get_version.sh $COMPILER_MODULE)
compiler_package=$(bash $PARTHENON/scripts/darwin/get_package.sh $COMPILER_MODULE)
mpi_version=$(bash $PARTHENON/scripts/darwin/get_version.sh $MPI_MODULE)
mpi_package=$(bash $PARTHENON/scripts/darwin/get_package.sh $MPI_MODULE)

wrapper_compiler=$(bash $PARTHENON/scripts/darwin/get_cpp.sh $compiler_package)
export NVCC_WRAPPER_DEFAULT_COMPILER=${wrapper_compiler}

# Load system modules
module purge
module load ${13} # cmake
module load ${14} # clang for formatting
module load $COMPILER_MODULE # gcc
module load $MPI_MODULE # mpi
module load ${16} # cuda

# Initialize spack env
. ${HOME}/spack/share/spack/setup-env.sh

spack env activate ci

# Find compilers
spack compiler find

# Load Spack Modules

spack load hdf5@1.10.6%${compiler_package}@${compiler_version} \
  ^${mpi_package}@${mpi_version}%${compiler_package}@${compiler_version}

spack load py-h5py@2.10.0 ^hdf5@1.10.6%${compiler_package}@${compiler_version} \
  ^${mpi_package}@${mpi_version}%${compiler_package}@${compiler_version}

spack load py-mpi4py
spack load py-matplotlib
spack load py-numpy

# Setup build env
export OMP_PROC_BIND=close
export CTEST_OUTPUT_ON_FAILURE=1

# Build
if [ -d $1 ] 
then
  echo "Removing $1"
  rm -rf $1/*
  rmdir $1
fi
echo "Creating build folder $1"
mkdir $1 
cd $1 

# exit when any of the following commands fail
set -e

# Display build command
echo "cmake \
 -DCMAKE_BUILD_TYPE=$2 \
 -DCMAKE_CXX_COMPILER=$3 \
 -DKokkos_ARCH_POWER9=$4 \
 -DKokkos_ARCH_VOLTA70=$5 \
 -DKokkos_ENABLE_CUDA=$6 \
 -DKokkos_ENABLE_CUDA_UVM=$7 \
 -DKokkos_ENABLE_OPENMP=$8 \
 -DNUM_GPU_DEVICES_PER_NODE=${9} \
 -DNUM_MPI_PROC_TESTING=${10} \
 -DOMP_NUM_THREADS=${11} \
 -DPARTHENON_DISABLE_HDF5=${12} \
 ../"

cmake \
 -DCMAKE_BUILD_TYPE=$2 \
 -DCMAKE_CXX_COMPILER=$3 \
 -DKokkos_ARCH_POWER9=$4 \
 -DKokkos_ARCH_VOLTA70=$5 \
 -DKokkos_ENABLE_CUDA=$6 \
 -DKokkos_ENABLE_CUDA_UVM=$7 \
 -DKokkos_ENABLE_OPENMP=$8 \
 -DNUM_GPU_DEVICES_PER_NODE=${9} \
 -DNUM_MPI_PROC_TESTING=${10} \
 -DOMP_NUM_THREADS=${11} \
 -DPARTHENON_DISABLE_HDF5=${12} \
 ../
fail_or_pass=$?
[ ${fail_or_pass} -ne 0 ] && exit 1

make -j $J VERBOSE=1
fail_or_pass=$?
[ ${fail_or_pass} -ne 0 ] && exit 1

# Build in serial
ctest --output-on-failure 
fail_or_pass=$?
[ ${fail_or_pass} -ne 0 ] && exit 1

exit 0
