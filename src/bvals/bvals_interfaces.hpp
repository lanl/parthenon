//========================================================================================
// Athena++ astrophysical MHD code
// Copyright(C) 2014 James M. Stone <jmstone@princeton.edu> and other code contributors
// Licensed under the 3-clause BSD License, see LICENSE file for details
//========================================================================================
// (C) (or copyright) 2020. Triad National Security, LLC. All rights reserved.
//
// This program was produced under U.S. Government contract 89233218CNA000001 for Los
// Alamos National Laboratory (LANL), which is operated by Triad National Security, LLC
// for the U.S. Department of Energy/National Nuclear Security Administration. All rights
// in the program are reserved by Triad National Security, LLC, and the U.S. Department
// of Energy/National Nuclear Security Administration. The Government is granted for
// itself and others acting on its behalf a nonexclusive, paid-up, irrevocable worldwide
// license in this material to reproduce, prepare derivative works, distribute copies to
// the public, perform publicly and display publicly, and to permit others to do so.
//========================================================================================
#ifndef BVALS_BVALS_INTERFACES_HPP_
#define BVALS_BVALS_INTERFACES_HPP_
//! \file bvals_interfaces.hpp
//  \brief defines enums, structs, and abstract classes

// TODO(felker): deduplicate forward declarations
// TODO(felker): consider moving enums and structs in a new file? bvals_structs.hpp?

#include <string>
#include <vector>

#include "parthenon_mpi.hpp"

#include "defs.hpp"
#include "parthenon_arrays.hpp"

namespace parthenon {

// forward declarations
class Mesh;
class MeshBlock;
class MeshBlockTree;
class Field;
class ParameterInput;
class Coordinates;
class BoundaryValues;
struct RegionSize;

// TODO(felker): nest these enum definitions inside bvals/ classes, when possible.

// DEPRECATED(felker): maintain old-style (ALL_CAPS) enumerators as unscoped,unnamed types
// Keep for compatibility with user-provided pgen/ files. Use only new types internally.

// GCC 6 added Enumerator Attr (v6.1 released on 2016-04-27)
// TODO(felker): replace with C++14 [[deprecated]] attributes if we ever bump --std=c++14
#if (defined(__GNUC__) && __GNUC__ >= 6) || (defined(__clang__) && __clang_major__ >= 3)
enum {
  FACE_UNDEF __attribute__((deprecated)) = -1,
  INNER_X1 __attribute__((deprecated)),
  OUTER_X1 __attribute__((deprecated)),
  INNER_X2 __attribute__((deprecated)),
  OUTER_X2 __attribute__((deprecated)),
  INNER_X3 __attribute__((deprecated)),
  OUTER_X3 __attribute__((deprecated))
};
enum {
  BLOCK_BNDRY __attribute__((deprecated)) = -1,
  BNDRY_UNDEF __attribute__((deprecated)),
  REFLECTING_BNDRY __attribute__((deprecated)),
  OUTFLOW_BNDRY __attribute__((deprecated)),
  PERIODIC_BNDRY __attribute__((deprecated))
};
#else
enum { FACE_UNDEF = -1, INNER_X1, OUTER_X1, INNER_X2, OUTER_X2, INNER_X3, OUTER_X3 };
enum {
  BLOCK_BNDRY = -1,
  BNDRY_UNDEF,
  REFLECTING_BNDRY,
  OUTFLOW_BNDRY,
  USER_BNDRY,
  PERIODIC_BNDRY,
  POLAR_BNDRY,
  POLAR_BNDRY_WEDGE
};
#endif

// identifiers for all 6 faces of a MeshBlock
enum BoundaryFace {
  undef = -1,
  inner_x1 = 0,
  outer_x1 = 1,
  inner_x2 = 2,
  outer_x2 = 3,
  inner_x3 = 4,
  outer_x3 = 5
};
// TODO(felker): BoundaryFace must be unscoped enum, for now. Its enumerators are used as
// int to index raw arrays (not ParArrayNDs)--> enumerator vals are explicitly specified

// identifiers for boundary conditions
enum class BoundaryFlag { block = -1, undef, reflect, outflow, periodic };

// identifiers for types of neighbor blocks (connectivity with current MeshBlock)
enum class NeighborConnect {
  none,
  face,
  edge,
  corner
}; // degenerate/shared part of block

// identifiers for status of MPI boundary communications
enum class BoundaryStatus { waiting, arrived, completed };

//----------------------------------------------------------------------------------------
//! \struct SimpleNeighborBlock
//  \brief Struct storing only the basic info about a MeshBlocks neighbors. Typically used
//  for convenience to store redundant info from subset of the more complete NeighborBlock
//  objects, e.g. for describing neighbors around pole at same radius and polar angle

struct SimpleNeighborBlock { // aggregate and POD
  int rank;                  // MPI rank of neighbor
  int level;                 // refinement (logical, not physical) level of neighbor
  int lid;                   // local ID of neighbor
  int gid;                   // global ID of neighbor
};

//----------------------------------------------------------------------------------------
//! \struct NeighborConnect
//  \brief data to describe MeshBlock neighbors

struct NeighborIndexes { // aggregate and POD
  int ox1, ox2, ox3;     // 3-vec of offsets in {-1,0,+1} relative to this block's (i,j,k)
  int fi1, fi2; // 2-vec for identifying refined neighbors (up to 4x face neighbors
                // in 3D), entries in {0, 1}={smaller, larger} LogicalLcation::lxi
  NeighborConnect type;
  // User-provided ctor is unnecessary and prevents the type from being POD and aggregate.
  // This struct's implicitly-defined or defaulted default ctor is trivial, implying that
  // NeighborIndexes is a trivial type. Combined with standard layout --> POD. Advantages:
  //   - No user-provided ctor: value initialization first performs zero initialization
  //     (then default initialization if ctor is non-trivial)
  //   - Aggregate type: supports aggregate initialization {}
  //   - POD type: safely copy objects via memcpy, no memory padding in the beginning of
  //     object, C portability, supports static initialization
};

//----------------------------------------------------------------------------------------
//! \struct NeighborBlock
//  \brief

struct NeighborBlock { // aggregate and POD type. Inheritance breaks standard-layout-> POD
                       // : SimpleNeighborBlock, NeighborIndexes {
  // composition:
  SimpleNeighborBlock snb;
  NeighborIndexes ni;

  int bufid, eid, targetid;
  BoundaryFace fid;

  void SetNeighbor(int irank, int ilevel, int igid, int ilid, int iox1, int iox2,
                   int iox3, NeighborConnect itype, int ibid, int itargetid, int ifi1 = 0,
                   int ifi2 = 0);
};

//----------------------------------------------------------------------------------------
//! \struct BoundaryData
//  \brief structure storing boundary information

// TODO(felker): consider renaming/be more specific--- what kind of data/info?
// one for each type of "BoundaryQuantity" corresponding to BoundaryVariable

template <int n = 56>
struct BoundaryData { // aggregate and POD (even when MPI_PARALLEL is defined)
  static constexpr int kMaxNeighbor = n;
  // KGF: "nbmax" only used in bvals_var.cpp, Init/DestroyBoundaryData()
  int nbmax; // actual maximum number of neighboring MeshBlocks
  // currently, sflag[] is only used by Multgrid (send buffers are reused each stage in
  // red-black comm. pattern; need to check if they are available)
  BoundaryStatus flag[kMaxNeighbor], sflag[kMaxNeighbor];
  ParArray1D<Real> send[kMaxNeighbor], recv[kMaxNeighbor];
#ifdef MPI_PARALLEL
  MPI_Request req_send[kMaxNeighbor], req_recv[kMaxNeighbor];
#endif
};

//----------------------------------------------------------------------------------------
// Interfaces = abstract classes containing ONLY pure virtual functions
//              Merely lists functions and their argument lists that must be implemented
//              in derived classes to form a somewhat-strict contract for functionality
//              (can always implement as a do-nothing/no-op function if a derived class
//              instance is the exception to the rule and does not use a particular
//              interface function)
//----------------------------------------------------------------------------------------

//----------------------------------------------------------------------------------------
//! \class BoundaryCommunication
//  \brief contains methods for managing BoundaryStatus flags and MPI requests

class BoundaryCommunication {
 public:
  BoundaryCommunication() {}
  virtual ~BoundaryCommunication() {}
  // create unique tags for each MeshBlock/buffer/quantity and initalize MPI requests:
  virtual void SetupPersistentMPI() = 0;
  // call MPI_Start() on req_recv[]
  virtual void StartReceiving(BoundaryCommSubset phase) = 0;
  // call MPI_Wait() on req_send[] and set flag[] to BoundaryStatus::waiting
  virtual void ClearBoundary(BoundaryCommSubset phase) = 0;
};

//----------------------------------------------------------------------------------------
//! \class BoundaryBuffer
//  \brief contains methods for managing MPI send/recvs and associated loads/stores from
//  communication buffers.

// TODO(KGF): Merge with above BoundaryCommunication interface?

class BoundaryBuffer {
 public:
  BoundaryBuffer() {}
  virtual ~BoundaryBuffer() {}

  // universal buffer management methods for Cartesian grids (unrefined and SMR/AMR)
  virtual void SendBoundaryBuffers() = 0;
  virtual bool ReceiveBoundaryBuffers() = 0;
  // this next fn is used only during problem initialization in mesh.cpp:
  virtual void ReceiveAndSetBoundariesWithWait() = 0;
  virtual void SetBoundaries() = 0;

  virtual void SendFluxCorrection() = 0;
  virtual bool ReceiveFluxCorrection() = 0;

 protected:
  // universal buffer management methods for Cartesian grids (unrefined and SMR/AMR):
  virtual int LoadBoundaryBufferSameLevel(ParArray1D<Real> &buf,
                                          const NeighborBlock &nb) = 0;
  virtual void SetBoundarySameLevel(ParArray1D<Real> &buf, const NeighborBlock &nb) = 0;

  // SMR/AMR-exclusive buffer management methods:
  virtual int LoadBoundaryBufferToCoarser(ParArray1D<Real> &buf,
                                          const NeighborBlock &nb) = 0;
  virtual int LoadBoundaryBufferToFiner(ParArray1D<Real> &buf,
                                        const NeighborBlock &nb) = 0;
  virtual void SetBoundaryFromCoarser(ParArray1D<Real> &buf, const NeighborBlock &nb) = 0;
  virtual void SetBoundaryFromFiner(ParArray1D<Real> &buf, const NeighborBlock &nb) = 0;
};

//----------------------------------------------------------------------------------------
// Abstract classes containing mix of pure virtual, virtual, and concrete functoins
//----------------------------------------------------------------------------------------

//----------------------------------------------------------------------------------------
//! \class BoundaryVariable (abstract)
//  \brief

class BoundaryVariable : public BoundaryCommunication, public BoundaryBuffer {
 public:
  explicit BoundaryVariable(MeshBlock *pmb);
  virtual ~BoundaryVariable() = default;

  // (usuallly the std::size_t unsigned integer type)
  std::vector<BoundaryVariable *>::size_type bvar_index;

  virtual int ComputeVariableBufferSize(const NeighborIndexes &ni, int cng) = 0;
  virtual int ComputeFluxCorrectionBufferSize(const NeighborIndexes &ni, int cng) = 0;

  // BoundaryBuffer public functions with shared implementations
  void SendBoundaryBuffers() override;
  bool ReceiveBoundaryBuffers() override;
  void ReceiveAndSetBoundariesWithWait() override;
  void SetBoundaries() override;

 protected:
  // deferred initialization of BoundaryData objects in derived class constructors
  BoundaryData<> bd_var_, bd_var_flcor_;
  // derived class dtors are also responsible for calling DestroyBoundaryData(bd_var_)

  MeshBlock *pmy_block_; // ptr to MeshBlock containing this BoundaryVariable
  Mesh *pmy_mesh_;

  void CopyVariableBufferSameProcess(NeighborBlock &nb, int ssize);
  void CopyFluxCorrectionBufferSameProcess(NeighborBlock &nb, int ssize);

  void InitBoundaryData(BoundaryData<> &bd, BoundaryQuantity type);
  void DestroyBoundaryData(BoundaryData<> &bd);

  // private:
};

} // namespace parthenon

#endif // BVALS_BVALS_INTERFACES_HPP_
