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
#ifndef BVALS_BVALS_HPP_
#define BVALS_BVALS_HPP_
//! \file bvals.hpp
//  \brief defines BoundaryBase, BoundaryValues classes used for setting BCs on all data

// C headers

// C++ headers
#include <string>   // string
#include <vector>

// Athena++ headers
#include "athena.hpp"
#include "athena_arrays.hpp"
#include "bvals_interfaces.hpp"

// MPI headers
#ifdef MPI_PARALLEL
#include <mpi.h>
#endif

// forward declarations
// TODO(felker): how many of these foward declarations are actually needed now?
// Can #include "./bvals_interfaces.hpp" suffice?
class Mesh;
class MeshBlock;
class MeshBlockTree;
class ParameterInput;
class Coordinates;
struct RegionSize;

// free functions to return boundary flag given input string, and vice versa
BoundaryFlag GetBoundaryFlag(const std::string& input_string);
std::string GetBoundaryString(BoundaryFlag input_flag);
// + confirming that the MeshBlock's boundaries are all valid selections
void CheckBoundaryFlag(BoundaryFlag block_flag, CoordinateDirection dir);

//----------------------------------------------------------------------------------------
//! \class BoundaryBase
//  \brief Base class for all BoundaryValues classes (BoundaryValues and MGBoundaryValues)

class BoundaryBase {
 public:
  BoundaryBase(Mesh *pm, LogicalLocation iloc, RegionSize isize,
               BoundaryFlag *input_bcs);
  virtual ~BoundaryBase() = default;
  // 1x pair (neighbor index, buffer ID) per entire SET of separate variable buffers
  // (Hydro, Field, Passive Scalar, etc.). Greedy allocation for worst-case
  // of refined 3D; only 26 entries needed/initialized if unrefined 3D, e.g.
  static NeighborIndexes ni[56];
  static int bufid[56];

  NeighborBlock neighbor[56];
  int nneighbor;
  int nblevel[3][3][3];
  LogicalLocation loc;
  BoundaryFlag block_bcs[6];

  static int CreateBvalsMPITag(int lid, int bufid, int phys);
  static int CreateBufferID(int ox1, int ox2, int ox3, int fi1, int fi2);
  static int BufferID(int dim, bool multilevel);
  static int FindBufferID(int ox1, int ox2, int ox3, int fi1, int fi2);

  void SearchAndSetNeighbors(MeshBlockTree &tree, int *ranklist, int *nslist);

 protected:
  // 1D refined or unrefined=2
  // 2D refined=12, unrefined=8
  // 3D refined=56, unrefined=26. Refinement adds: 3*6 faces + 1*12 edges = +30 neighbors
  static int maxneighbor_;

  Mesh *pmy_mesh_;
  RegionSize block_size_;
  AthenaArray<Real> sarea_[2];

 private:
  // calculate 3x shared static data members when constructing only the 1st class instance
  // int maxneighbor_=BufferID() computes ni[] and then calls bufid[]=CreateBufferID()
  static bool called_;
};

//----------------------------------------------------------------------------------------
//! \class BoundaryValues
//  \brief centralized class for interacting with each individual variable boundary data
//         (design pattern ~ mediator)

class BoundaryValues : public BoundaryBase, //public BoundaryPhysics,
                       public BoundaryCommunication {
 public:
  BoundaryValues(MeshBlock *pmb, BoundaryFlag *input_bcs, ParameterInput *pin);

  // variable-length arrays of references to BoundaryVariable instances
  // containing all BoundaryVariable instances:
  std::vector<BoundaryVariable *> bvars;
  // subset of bvars that are exchanged in the main TimeIntegratorTaskList
  std::vector<BoundaryVariable *> bvars_main_int;

  void SetBoundaryFlags(BoundaryFlag bc_flag[]) {for (int i=0; i<6; i++) bc_flag[i]=block_bcs[i];}

  // inherited functions (interface shared with BoundaryVariable objects):
  // ------
  // called before time-stepper:
  void SetupPersistentMPI() final; // setup MPI requests

  // called before and during time-stepper:
  void StartReceiving(BoundaryCommSubset phase) final;
  void ClearBoundary(BoundaryCommSubset phase) final;

  // non-inhertied / unique functions (do not exist in BoundaryVariable objects):
  // (these typically involve a coupled interaction of boundary variable/quantities)
  // ------
  void ProlongateBoundaries(const Real time, const Real dt);

  int AdvanceCounterPhysID(int num_phys);

 private:
  MeshBlock *pmy_block_;      // ptr to MeshBlock containing this BoundaryValues
  int nface_, nedge_;         // used only in fc/flux_correction_fc.cpp calculations

  // if a BoundaryPhysics or user fn should be applied at each MeshBlock boundary
  // false --> e.g. block, polar, periodic boundaries
  bool apply_bndry_fn_[6]{};   // C++11: in-class initializer of non-static member
  // C++11: direct-list-initialization -> value init of array -> zero init of each scalar

  // local counter for generating unique MPI tags for per-MeshBlock BoundaryVariable
  // communication (subset of Mesh::next_phys_id_)
  int bvars_next_phys_id_;

  // ProlongateBoundaries() wraps the following S/AMR-operations (within nneighbor loop):
  // (the next function is also called within 3x nested loops over nk,nj,ni)
  void RestrictGhostCellsOnSameLevel(const NeighborBlock& nb, int nk, int nj, int ni);
  void ApplyPhysicalBoundariesOnCoarseLevel(
      const NeighborBlock& nb, const Real time, const Real dt,
      int si, int ei, int sj, int ej, int sk, int ek);
  void ProlongateGhostCells(const NeighborBlock& nb,
                            int si, int ei, int sj, int ej, int sk, int ek);

  // temporary--- Added by @tomidakn on 2015-11-27 in f0f989f85f
  // TODO(KGF): consider removing this friendship designation
  friend class Mesh;
  // currently, this class friendship is required for copying send/recv buffers between
  // BoundaryVariable objects within different MeshBlocks on the same MPI rank:
  friend class BoundaryVariable;
  friend class FaceCenteredBoundaryVariable;  // needs nface_, nedge_, num_north/south_...
  // TODO(KGF): consider removing these friendship designations:
  friend class CellCenteredBoundaryVariable;
};
#endif // BVALS_BVALS_HPP_
