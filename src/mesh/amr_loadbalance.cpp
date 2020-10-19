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
//! \file mesh_amr.cpp
//  \brief implementation of Mesh::AdaptiveMeshRefinement() and related utilities

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <numeric>
#include <sstream>
#include <string>
#include <tuple>

#include "parthenon_mpi.hpp"

#include "bvals/boundary_conditions.hpp"
#include "defs.hpp"
#include "globals.hpp"
#include "interface/update.hpp"
#include "mesh/mesh.hpp"
#include "mesh/mesh_refinement.hpp"
#include "mesh/meshblock_tree.hpp"
#include "parthenon_arrays.hpp"
#include "utils/buffer_utils.hpp"
#include "utils/error_checking.hpp"

namespace parthenon {

//----------------------------------------------------------------------------------------
// \!fn void Mesh::LoadBalancingAndAdaptiveMeshRefinement(ParameterInput *pin)
// \brief Main function for adaptive mesh refinement

void Mesh::LoadBalancingAndAdaptiveMeshRefinement(ParameterInput *pin,
                                                  ApplicationInput *app_in) {
  int nnew = 0, ndel = 0;

  if (adaptive) {
    UpdateMeshBlockTree(nnew, ndel);
    nbnew += nnew;
    nbdel += ndel;
  }

  lb_flag_ |= lb_automatic_;

  UpdateCostList();

  modified = false;
  if (nnew != 0 || ndel != 0) { // at least one (de)refinement happened
    GatherCostListAndCheckBalance();
    RedistributeAndRefineMeshBlocks(pin, app_in, nbtotal + nnew - ndel);
    modified = true;
  } else if (lb_flag_ && step_since_lb >= lb_interval_) {
    if (!GatherCostListAndCheckBalance()) { // load imbalance detected
      RedistributeAndRefineMeshBlocks(pin, app_in, nbtotal);
      modified = true;
    }
    lb_flag_ = false;
  }
  return;
}

// Private routines
namespace {
/**
 * @brief This routine assigns blocks to ranks by attempting to place index-contiguous
 * blocks of equal total cost on each rank.
 *
 * @param costlist (Input) A map of global block ID to a relative weight.
 * @param ranklist (Output) A map of global block ID to ranks.
 */
void AssignBlocks(std::vector<double> const &costlist, std::vector<int> &ranklist) {
  ranklist.resize(costlist.size());

  double const total_cost = std::accumulate(costlist.begin(), costlist.end(), 0.0);

  int rank = (Globals::nranks)-1;
  double target_cost = total_cost / Globals::nranks;
  double my_cost = 0.0;
  double remaining_cost = total_cost;
  // create rank list from the end: the master MPI rank should have less load
  for (int block_id = costlist.size() - 1; block_id >= 0; block_id--) {
    if (target_cost == 0.0) {
      std::stringstream msg;
      msg << "### FATAL ERROR in CalculateLoadBalance" << std::endl
          << "There is at least one process which has no MeshBlock" << std::endl
          << "Decrease the number of processes or use smaller MeshBlocks." << std::endl;
      PARTHENON_FAIL(msg);
    }
    my_cost += costlist[block_id];
    ranklist[block_id] = rank;
    if (my_cost >= target_cost && rank > 0) {
      rank--;
      remaining_cost -= my_cost;
      my_cost = 0.0;
      target_cost = remaining_cost / (rank + 1);
    }
  }
}

void UpdateBlockList(std::vector<int> const &ranklist, std::vector<int> &nslist,
                     std::vector<int> &nblist) {
  nslist.resize(Globals::nranks);
  nblist.resize(Globals::nranks);

  nslist[0] = 0;
  int rank = 0;
  for (int block_id = 1; block_id < ranklist.size(); block_id++) {
    if (ranklist[block_id] != ranklist[block_id - 1]) {
      nblist[rank] = block_id - nslist[rank];
      nslist[++rank] = block_id;
    }
  }
  nblist[rank] = ranklist.size() - nslist[rank];
}
} // namespace

//----------------------------------------------------------------------------------------
// \brief Calculate distribution of MeshBlocks based on the cost list
void Mesh::CalculateLoadBalance(std::vector<double> const &costlist,
                                std::vector<int> &ranklist, std::vector<int> &nslist,
                                std::vector<int> &nblist) {
  auto const total_blocks = costlist.size();

  using it = std::vector<double>::const_iterator;
  std::pair<it, it> const min_max = std::minmax_element(costlist.begin(), costlist.end());

  double const mincost = min_max.first == costlist.begin() ? 0.0 : *min_max.first;
  double const maxcost = min_max.second == costlist.begin() ? 0.0 : *min_max.second;

  // Assigns blocks to ranks on a rougly cost-equal basis.
  AssignBlocks(costlist, ranklist);

  // Updates nslist with the ID of the starting block on each rank and the count of blocks
  // on each rank.
  UpdateBlockList(ranklist, nslist, nblist);

#ifdef MPI_PARALLEL
  if (total_blocks % (Globals::nranks) != 0 && !adaptive && !lb_flag_ &&
      maxcost == mincost && Globals::my_rank == 0) {
    std::cout << "### Warning in CalculateLoadBalance" << std::endl
              << "The number of MeshBlocks cannot be divided evenly. "
              << "This will result in poor load balancing." << std::endl;
  }
#endif
  if (Globals::nranks > total_blocks) {
    if (!adaptive) {
      // mesh is refined statically, treat this an as error (all ranks need to
      // participate)
      std::stringstream msg;
      msg << "### FATAL ERROR in CalculateLoadBalance" << std::endl
          << "There are fewer MeshBlocks than OpenMP threads on each MPI rank"
          << std::endl
          << "Decrease the number of threads or use more MeshBlocks." << std::endl;
      PARTHENON_FAIL(msg);
    } else if (Globals::my_rank == 0) {
      // we have AMR, print warning only on Rank 0
      std::cout << "### WARNING in CalculateLoadBalance" << std::endl
                << "There are fewer MeshBlocks than OpenMP threads on each MPI rank"
                << std::endl
                << "This is likely fine if the number of meshblocks is expected to grow "
                   "during the "
                   "simulations. Otherwise, it might be worthwhile to decrease the "
                   "number of threads or "
                   "use more meshblocks."
                << std::endl;
    }
  }
}

//----------------------------------------------------------------------------------------
// \!fn void Mesh::ResetLoadBalanceVariables()
// \brief reset counters and flags for load balancing

void Mesh::ResetLoadBalanceVariables() {
  if (lb_automatic_) {
    for (auto &pmb : block_list) {
      costlist[pmb->gid] = TINY_NUMBER;
      pmb->ResetTimeMeasurement();
    }
  }
  lb_flag_ = false;
  step_since_lb = 0;
}

//----------------------------------------------------------------------------------------
// \!fn void Mesh::UpdateCostList()
// \brief update the cost list

void Mesh::UpdateCostList() {
  if (lb_automatic_) {
    double w = static_cast<double>(lb_interval_ - 1) / static_cast<double>(lb_interval_);
    for (auto &pmb : block_list) {
      costlist[pmb->gid] = costlist[pmb->gid] * w + pmb->cost_;
    }
  } else if (lb_flag_) {
    for (auto &pmb : block_list) {
      costlist[pmb->gid] = pmb->cost_;
    }
  }
}

//----------------------------------------------------------------------------------------
// \!fn void Mesh::UpdateMeshBlockTree(int &nnew, int &ndel)
// \brief collect refinement flags and manipulate the MeshBlockTree

void Mesh::UpdateMeshBlockTree(int &nnew, int &ndel) {
  // compute nleaf= number of leaf MeshBlocks per refined block
  int nleaf = 2, dim = 1;
  if (mesh_size.nx2 > 1) nleaf = 4, dim = 2;
  if (mesh_size.nx3 > 1) nleaf = 8, dim = 3;

  // collect refinement flags from all the meshblocks
  // count the number of the blocks to be (de)refined
  nref[Globals::my_rank] = 0;
  nderef[Globals::my_rank] = 0;
  for (auto const &pmb : block_list) {
    if (pmb->pmr->refine_flag_ == 1) nref[Globals::my_rank]++;
    if (pmb->pmr->refine_flag_ == -1) nderef[Globals::my_rank]++;
  }
#ifdef MPI_PARALLEL
  MPI_Allgather(MPI_IN_PLACE, 1, MPI_INT, nref.data(), 1, MPI_INT, MPI_COMM_WORLD);
  MPI_Allgather(MPI_IN_PLACE, 1, MPI_INT, nderef.data(), 1, MPI_INT, MPI_COMM_WORLD);
#endif

  // count the number of the blocks to be (de)refined and displacement
  int tnref = 0, tnderef = 0;
  for (int n = 0; n < Globals::nranks; n++) {
    tnref += nref[n];
    tnderef += nderef[n];
  }
  if (tnref == 0 && tnderef < nleaf) // nothing to do
    return;

  int rd = 0, dd = 0;
  for (int n = 0; n < Globals::nranks; n++) {
    rdisp[n] = rd;
    ddisp[n] = dd;
    // technically could overflow, since sizeof() operator returns
    // std::size_t = long unsigned int > int
    // on many platforms (LP64). However, these are used below in MPI calls for
    // integer arguments (recvcounts, displs). MPI does not support > 64-bit count ranges
    bnref[n] = static_cast<int>(nref[n] * sizeof(LogicalLocation));
    bnderef[n] = static_cast<int>(nderef[n] * sizeof(LogicalLocation));
    brdisp[n] = static_cast<int>(rd * sizeof(LogicalLocation));
    bddisp[n] = static_cast<int>(dd * sizeof(LogicalLocation));
    rd += nref[n];
    dd += nderef[n];
  }

  // allocate memory for the location arrays
  LogicalLocation *lref{}, *lderef{}, *clderef{};
  if (tnref > 0) lref = new LogicalLocation[tnref];
  if (tnderef >= nleaf) {
    lderef = new LogicalLocation[tnderef];
    clderef = new LogicalLocation[tnderef / nleaf];
  }

  // collect the locations and costs
  int iref = rdisp[Globals::my_rank], ideref = ddisp[Globals::my_rank];
  for (auto const &pmb : block_list) {
    if (pmb->pmr->refine_flag_ == 1) lref[iref++] = pmb->loc;
    if (pmb->pmr->refine_flag_ == -1 && tnderef >= nleaf) lderef[ideref++] = pmb->loc;
  }
#ifdef MPI_PARALLEL
  if (tnref > 0) {
    MPI_Allgatherv(MPI_IN_PLACE, bnref[Globals::my_rank], MPI_BYTE, lref, bnref.data(),
                   brdisp.data(), MPI_BYTE, MPI_COMM_WORLD);
  }
  if (tnderef >= nleaf) {
    MPI_Allgatherv(MPI_IN_PLACE, bnderef[Globals::my_rank], MPI_BYTE, lderef,
                   bnderef.data(), bddisp.data(), MPI_BYTE, MPI_COMM_WORLD);
  }
#endif

  // calculate the list of the newly derefined blocks
  int ctnd = 0;
  if (tnderef >= nleaf) {
    int lk = 0, lj = 0;
    if (mesh_size.nx2 > 1) lj = 1;
    if (mesh_size.nx3 > 1) lk = 1;
    for (int n = 0; n < tnderef; n++) {
      if ((lderef[n].lx1 & 1LL) == 0LL && (lderef[n].lx2 & 1LL) == 0LL &&
          (lderef[n].lx3 & 1LL) == 0LL) {
        int r = n, rr = 0;
        for (std::int64_t k = 0; k <= lk; k++) {
          for (std::int64_t j = 0; j <= lj; j++) {
            for (std::int64_t i = 0; i <= 1; i++) {
              if (r < tnderef) {
                if ((lderef[n].lx1 + i) == lderef[r].lx1 &&
                    (lderef[n].lx2 + j) == lderef[r].lx2 &&
                    (lderef[n].lx3 + k) == lderef[r].lx3 &&
                    lderef[n].level == lderef[r].level)
                  rr++;
                r++;
              }
            }
          }
        }
        if (rr == nleaf) {
          clderef[ctnd].lx1 = lderef[n].lx1 >> 1;
          clderef[ctnd].lx2 = lderef[n].lx2 >> 1;
          clderef[ctnd].lx3 = lderef[n].lx3 >> 1;
          clderef[ctnd].level = lderef[n].level - 1;
          ctnd++;
        }
      }
    }
  }
  // sort the lists by level
  if (ctnd > 1) std::sort(clderef, &(clderef[ctnd - 1]), LogicalLocation::Greater);

  if (tnderef >= nleaf) delete[] lderef;

  // Now the lists of the blocks to be refined and derefined are completed
  // Start tree manipulation
  // Step 1. perform refinement
  for (int n = 0; n < tnref; n++) {
    MeshBlockTree *bt = tree.FindMeshBlock(lref[n]);
    bt->Refine(nnew);
  }
  if (tnref != 0) delete[] lref;

  // Step 2. perform derefinement
  for (int n = 0; n < ctnd; n++) {
    MeshBlockTree *bt = tree.FindMeshBlock(clderef[n]);
    bt->Derefine(ndel);
  }
  if (tnderef >= nleaf) delete[] clderef;

  return;
}

//----------------------------------------------------------------------------------------
// \!fn bool Mesh::GatherCostListAndCheckBalance()
// \brief collect the cost from MeshBlocks and check the load balance

bool Mesh::GatherCostListAndCheckBalance() {
  if (lb_manual_ || lb_automatic_) {
#ifdef MPI_PARALLEL
    MPI_Allgatherv(MPI_IN_PLACE, nblist[Globals::my_rank], MPI_DOUBLE, costlist.data(),
                   nblist.data(), nslist.data(), MPI_DOUBLE, MPI_COMM_WORLD);
#endif
    double maxcost = 0.0, avecost = 0.0;
    for (int rank = 0; rank < Globals::nranks; rank++) {
      double rcost = 0.0;
      int ns = nslist[rank];
      int ne = ns + nblist[rank];
      for (int n = ns; n < ne; ++n)
        rcost += costlist[n];
      maxcost = std::max(maxcost, rcost);
      avecost += rcost;
    }
    avecost /= Globals::nranks;

    if (adaptive)
      lb_tolerance_ =
          2.0 * static_cast<double>(Globals::nranks) / static_cast<double>(nbtotal);

    if (maxcost > (1.0 + lb_tolerance_) * avecost) return false;
  }
  return true;
}

//----------------------------------------------------------------------------------------
// \!fn void Mesh::RedistributeAndRefineMeshBlocks(ParameterInput *pin, int ntot)
// \brief redistribute MeshBlocks according to the new load balance

void Mesh::RedistributeAndRefineMeshBlocks(ParameterInput *pin, ApplicationInput *app_in,
                                           int ntot) {
  // compute nleaf= number of leaf MeshBlocks per refined block
  int nleaf = 2;
  if (mesh_size.nx2 > 1) nleaf = 4;
  if (mesh_size.nx3 > 1) nleaf = 8;

  // Step 1. construct new lists
  std::vector<LogicalLocation> newloc(ntot);
  std::vector<int> newrank(ntot);
  std::vector<double> newcost(ntot);
  std::vector<int> newtoold(ntot);
  std::vector<int> oldtonew(nbtotal);

  int nbtold = nbtotal;
  tree.GetMeshBlockList(newloc.data(), newtoold.data(), nbtotal);

  // create a list mapping the previous gid to the current one
  oldtonew[0] = 0;
  int mb_idx = 1;
  for (int n = 1; n < ntot; n++) {
    if (newtoold[n] == newtoold[n - 1] + 1) { // normal
      oldtonew[mb_idx++] = n;
    } else if (newtoold[n] == newtoold[n - 1] + nleaf) { // derefined
      for (int j = 0; j < nleaf - 1; j++)
        oldtonew[mb_idx++] = n - 1;
      oldtonew[mb_idx++] = n;
    }
  }
  // fill the last block
  for (; mb_idx < nbtold; mb_idx++)
    oldtonew[mb_idx] = ntot - 1;

  current_level = 0;
  for (int n = 0; n < ntot; n++) {
    // "on" = "old n" = "old gid" = "old global MeshBlock ID"
    int on = newtoold[n];
    if (newloc[n].level > current_level) // set the current max level
      current_level = newloc[n].level;
    if (newloc[n].level >= loclist[on].level) { // same or refined
      newcost[n] = costlist[on];
    } else {
      double acost = 0.0;
      for (int l = 0; l < nleaf; l++)
        acost += costlist[on + l];
      newcost[n] = acost / nleaf;
    }
  }
#ifdef MPI_PARALLEL
  // store old nbstart and nbend before load balancing in Step 2.
  int onbs = nslist[Globals::my_rank];
  int onbe = onbs + nblist[Globals::my_rank] - 1;
#endif
  // Step 2. Calculate new load balance
  CalculateLoadBalance(newcost, newrank, nslist, nblist);

  int nbs = nslist[Globals::my_rank];
  int nbe = nbs + nblist[Globals::my_rank] - 1;

#ifdef MPI_PARALLEL
  int bnx1 = GetBlockSize().nx1;
  int bnx2 = GetBlockSize().nx2;
  int bnx3 = GetBlockSize().nx3;
  // Step 3. count the number of the blocks to be sent / received
  int nsend = 0, nrecv = 0;
  for (int n = nbs; n <= nbe; n++) {
    int on = newtoold[n];
    if (loclist[on].level > newloc[n].level) { // f2c
      for (int k = 0; k < nleaf; k++) {
        if (ranklist[on + k] != Globals::my_rank) nrecv++;
      }
    } else {
      if (ranklist[on] != Globals::my_rank) nrecv++;
    }
  }
  for (int n = onbs; n <= onbe; n++) {
    int nn = oldtonew[n];
    if (loclist[n].level < newloc[nn].level) { // c2f
      for (int k = 0; k < nleaf; k++) {
        if (newrank[nn + k] != Globals::my_rank) nsend++;
      }
    } else {
      if (newrank[nn] != Globals::my_rank) nsend++;
    }
  }

  // Step 4. calculate buffer sizes
  ParArray1D<Real> *sendbuf, *recvbuf;
  // use the first MeshBlock in the linked list of blocks belonging to this MPI rank as a
  // representative of all MeshBlocks for counting the "load-balancing registered" and
  // "SMR/AMR-enrolled" quantities (loop over MeshBlock::vars_cc_, not MeshRefinement)

  // TODO(felker): add explicit check to ensure that elements of pb->vars_cc/fc_ and
  // pb->pmr->pvars_cc/fc_ v point to the same objects, if adaptive

  // int num_cc = block_list.front().pmr->pvars_cc_.size();
  int num_fc = block_list.front()->vars_fc_.size();
  int nx4_tot = 0;
  for (auto &pvar_cc : block_list.front()->vars_cc_) {
    nx4_tot += pvar_cc->GetDim(4);
  }

  const int f2 = (ndim >= 2) ? 1 : 0; // extra cells/faces from being 2d
  const int f3 = (ndim >= 3) ? 1 : 0; // extra cells/faces from being 3d

  // cell-centered quantities enrolled in SMR/AMR
  int bssame = bnx1 * bnx2 * bnx3 * nx4_tot;
  int bsf2c = (bnx1 / 2) * ((bnx2 + 1) / 2) * ((bnx3 + 1) / 2) * nx4_tot;
  int bsc2f =
      (bnx1 / 2 + 2) * ((bnx2 + 1) / 2 + 2 * f2) * ((bnx3 + 1) / 2 + 2 * f3) * nx4_tot;
  // face-centered quantities enrolled in SMR/AMR
  bssame += num_fc * ((bnx1 + 1) * bnx2 * bnx3 + bnx1 * (bnx2 + f2) * bnx3 +
                      bnx1 * bnx2 * (bnx3 + f3));
  bsf2c += num_fc * (((bnx1 / 2) + 1) * ((bnx2 + 1) / 2) * ((bnx3 + 1) / 2) +
                     (bnx1 / 2) * (((bnx2 + 1) / 2) + f2) * ((bnx3 + 1) / 2) +
                     (bnx1 / 2) * ((bnx2 + 1) / 2) * (((bnx3 + 1) / 2) + f3));
  bsc2f +=
      num_fc *
      (((bnx1 / 2) + 1 + 2) * ((bnx2 + 1) / 2 + 2 * f2) * ((bnx3 + 1) / 2 + 2 * f3) +
       (bnx1 / 2 + 2) * (((bnx2 + 1) / 2) + f2 + 2 * f2) * ((bnx3 + 1) / 2 + 2 * f3) +
       (bnx1 / 2 + 2) * ((bnx2 + 1) / 2 + 2 * f2) * (((bnx3 + 1) / 2) + f3 + 2 * f3));
  // add one more element to buffer size for storing the derefinement counter
  bssame++;

  MPI_Request *req_send, *req_recv;
  // Step 5. allocate and start receiving buffers
  if (nrecv != 0) {
    recvbuf = new ParArray1D<Real>[nrecv];
    req_recv = new MPI_Request[nrecv];
    int rb_idx = 0; // recv buffer index
    for (int n = nbs; n <= nbe; n++) {
      int on = newtoold[n];
      LogicalLocation &oloc = loclist[on];
      LogicalLocation &nloc = newloc[n];
      if (oloc.level > nloc.level) { // f2c
        for (int l = 0; l < nleaf; l++) {
          if (ranklist[on + l] == Globals::my_rank) continue;
          LogicalLocation &lloc = loclist[on + l];
          int ox1 = ((lloc.lx1 & 1LL) == 1LL), ox2 = ((lloc.lx2 & 1LL) == 1LL),
              ox3 = ((lloc.lx3 & 1LL) == 1LL);
          recvbuf[rb_idx] = ParArray1D<Real>("recvbuf" + std::to_string(rb_idx), bsf2c);
          int tag = CreateAMRMPITag(n - nbs, ox1, ox2, ox3);
          MPI_Irecv(recvbuf[rb_idx].data(), bsf2c, MPI_PARTHENON_REAL, ranklist[on + l],
                    tag, MPI_COMM_WORLD, &(req_recv[rb_idx]));
          rb_idx++;
        }
      } else { // same level or c2f
        if (ranklist[on] == Globals::my_rank) continue;
        int size;
        if (oloc.level == nloc.level) {
          size = bssame;
        } else {
          size = bsc2f;
        }
        recvbuf[rb_idx] = ParArray1D<Real>("recvbuf" + std::to_string(rb_idx), size);
        int tag = CreateAMRMPITag(n - nbs, 0, 0, 0);
        MPI_Irecv(recvbuf[rb_idx].data(), size, MPI_PARTHENON_REAL, ranklist[on], tag,
                  MPI_COMM_WORLD, &(req_recv[rb_idx]));
        rb_idx++;
      }
    }
  }
  // Step 6. allocate, pack and start sending buffers
  if (nsend != 0) {
    sendbuf = new ParArray1D<Real>[nsend];
    req_send = new MPI_Request[nsend];
    int sb_idx = 0; // send buffer index
    for (int n = onbs; n <= onbe; n++) {
      int nn = oldtonew[n];
      LogicalLocation &oloc = loclist[n];
      LogicalLocation &nloc = newloc[nn];
      auto pb = FindMeshBlock(n);
      if (nloc.level == oloc.level) { // same level
        if (newrank[nn] == Globals::my_rank) continue;
        sendbuf[sb_idx] =
            ParArray1D<Real>("amr send buf same" + std::to_string(sb_idx), bssame);
        PrepareSendSameLevel(pb.get(), sendbuf[sb_idx]);
        int tag = CreateAMRMPITag(nn - nslist[newrank[nn]], 0, 0, 0);
        MPI_Isend(sendbuf[sb_idx].data(), bssame, MPI_PARTHENON_REAL, newrank[nn], tag,
                  MPI_COMM_WORLD, &(req_send[sb_idx]));
        sb_idx++;
      } else if (nloc.level > oloc.level) { // c2f
        // c2f must communicate to multiple leaf blocks (unlike f2c, same2same)
        for (int l = 0; l < nleaf; l++) {
          if (newrank[nn + l] == Globals::my_rank) continue;
          sendbuf[sb_idx] =
              ParArray1D<Real>("amr send buf c2f" + std::to_string(sb_idx), bsc2f);
          PrepareSendCoarseToFineAMR(pb.get(), sendbuf[sb_idx], newloc[nn + l]);
          int tag = CreateAMRMPITag(nn + l - nslist[newrank[nn + l]], 0, 0, 0);
          MPI_Isend(sendbuf[sb_idx].data(), bsc2f, MPI_PARTHENON_REAL, newrank[nn + l],
                    tag, MPI_COMM_WORLD, &(req_send[sb_idx]));
          sb_idx++;
        }      // end loop over nleaf (unique to c2f branch in this step 6)
      } else { // f2c: restrict + pack + send
        if (newrank[nn] == Globals::my_rank) continue;
        sendbuf[sb_idx] =
            ParArray1D<Real>("amr send buf f2c" + std::to_string(sb_idx), bsf2c);
        PrepareSendFineToCoarseAMR(pb.get(), sendbuf[sb_idx]);
        int ox1 = ((oloc.lx1 & 1LL) == 1LL), ox2 = ((oloc.lx2 & 1LL) == 1LL),
            ox3 = ((oloc.lx3 & 1LL) == 1LL);
        int tag = CreateAMRMPITag(nn - nslist[newrank[nn]], ox1, ox2, ox3);
        MPI_Isend(sendbuf[sb_idx].data(), bsf2c, MPI_PARTHENON_REAL, newrank[nn], tag,
                  MPI_COMM_WORLD, &(req_send[sb_idx]));
        sb_idx++;
      }
    }
  }    // if (nsend !=0)
#endif // MPI_PARALLEL

  // Step 7. construct a new MeshBlock list (moving the data within the MPI rank)
  {
    RegionSize block_size = GetBlockSize();

    BlockList_t new_block_list(nbe - nbs + 1);
    for (int n = nbs; n <= nbe; n++) {
      int on = newtoold[n];
      if ((ranklist[on] == Globals::my_rank) && (loclist[on].level == newloc[n].level)) {
        // on the same MPI rank and same level -> just move it
        new_block_list[n - nbs] = FindMeshBlock(on);
      } else {
        // on a different refinement level or MPI rank - create a new block
        BoundaryFlag block_bcs[6];
        SetBlockSizeAndBoundaries(newloc[n], block_size, block_bcs);
        // append new block to list of MeshBlocks
        new_block_list[n - nbs] =
            MeshBlock::Make(n, n - nbs, newloc[n], block_size, block_bcs, this, pin,
                            app_in, properties, packages, gflag);
        // fill the conservative variables
        if ((loclist[on].level > newloc[n].level)) { // fine to coarse (f2c)
          for (int ll = 0; ll < nleaf; ll++) {
            if (ranklist[on + ll] != Globals::my_rank) continue;
            // fine to coarse on the same MPI rank (different AMR level) - restriction
            auto pob = FindMeshBlock(on + ll);
            FillSameRankFineToCoarseAMR(pob.get(), new_block_list[n - nbs].get(),
                                        loclist[on + ll]);
          }
        } else if ((loclist[on].level < newloc[n].level) && // coarse to fine (c2f)
                   (ranklist[on] == Globals::my_rank)) {
          // coarse to fine on the same MPI rank (different AMR level) - prolongation
          auto pob = FindMeshBlock(on);
          FillSameRankCoarseToFineAMR(pob.get(), new_block_list[n - nbs].get(),
                                      newloc[n]);
        }
        ApplyBoundaryConditions(new_block_list[n - nbs]->real_containers.Get());
        FillDerivedVariables::FillDerived(new_block_list[n - nbs]->real_containers.Get());
      }
    }

    // Replace the MeshBlock list
    block_list = std::move(new_block_list);

    // Ensure local and global ids are correct
    for (int n = nbs; n <= nbe; n++) {
      block_list[n - nbs]->gid = n;
      block_list[n - nbs]->lid = n - nbs;
    }
  }

  // Step 8. Receive the data and load into MeshBlocks
  // This is a test: try MPI_Waitall later.
#ifdef MPI_PARALLEL
  if (nrecv != 0) {
    int rb_idx = 0; // recv buffer index
    for (int n = nbs; n <= nbe; n++) {
      int on = newtoold[n];
      LogicalLocation &oloc = loclist[on];
      LogicalLocation &nloc = newloc[n];
      auto pb = FindMeshBlock(n);
      pb->exec_space.fence();
      if (oloc.level == nloc.level) { // same
        if (ranklist[on] == Globals::my_rank) continue;
        MPI_Wait(&(req_recv[rb_idx]), MPI_STATUS_IGNORE);
        FinishRecvSameLevel(pb.get(), recvbuf[rb_idx]);
        rb_idx++;
      } else if (oloc.level > nloc.level) { // f2c
        for (int l = 0; l < nleaf; l++) {
          if (ranklist[on + l] == Globals::my_rank) continue;
          MPI_Wait(&(req_recv[rb_idx]), MPI_STATUS_IGNORE);
          FinishRecvFineToCoarseAMR(pb.get(), recvbuf[rb_idx], loclist[on + l]);
          rb_idx++;
        }
      } else { // c2f
        if (ranklist[on] == Globals::my_rank) continue;
        MPI_Wait(&(req_recv[rb_idx]), MPI_STATUS_IGNORE);
        FinishRecvCoarseToFineAMR(pb.get(), recvbuf[rb_idx]);
        rb_idx++;
      }
    }
  }
#endif

  // deallocate arrays
  newtoold.clear();
  oldtonew.clear();
#ifdef MPI_PARALLEL
  if (nsend != 0) {
    MPI_Waitall(nsend, req_send, MPI_STATUSES_IGNORE);
    delete[] sendbuf;
    delete[] req_send;
  }
  if (nrecv != 0) {
    delete[] recvbuf;
    delete[] req_recv;
  }
#endif

  // update the lists
  loclist = std::move(newloc);
  ranklist = std::move(newrank);
  costlist = std::move(newcost);

  // re-initialize the MeshBlocks
  for (auto &pmb : block_list) {
    pmb->pbval->SearchAndSetNeighbors(tree, ranklist.data(), nslist.data());
  }
  Initialize(2, pin, app_in);

  BuildMeshBlockPacks();

  ResetLoadBalanceVariables();

  return;
}

// AMR: step 6, branch 1 (same2same: just pack+send)

void Mesh::PrepareSendSameLevel(MeshBlock *pmb, ParArray1D<Real> &sendbuf) {
  // pack
  int p = 0;

  const int f2 = (ndim >= 2) ? 1 : 0; // extra cells/faces from being 2d
  const int f3 = (ndim >= 3) ? 1 : 0; // extra cells/faces from being 3d

  const IndexDomain interior = IndexDomain::interior;
  IndexRange ib = pmb->cellbounds.GetBoundsI(interior);
  IndexRange jb = pmb->cellbounds.GetBoundsJ(interior);
  IndexRange kb = pmb->cellbounds.GetBoundsK(interior);
  // this helper fn is used for AMR and non-refinement load balancing of
  // MeshBlocks. Therefore, unlike PrepareSendCoarseToFineAMR(), etc., it loops over
  // MeshBlock::vars_cc/fc_ containers, not MeshRefinement::pvars_cc/fc_ containers

  // TODO(felker): add explicit check to ensure that elements of pmb->vars_cc/fc_ and
  // pmb->pmr->pvars_cc/fc_ v point to the same objects, if adaptive

  // (C++11) range-based for loop: (automatic type deduction fails when iterating over
  // container with std::reference_wrapper; could use auto var_cc_r = var_cc.get())
  for (auto &pvar_cc : pmb->vars_cc_) {
    int nu = pvar_cc->GetDim(4) - 1;
    ParArray4D<Real> var_cc = pvar_cc->data.Get<4>();
    BufferUtility::PackData(var_cc, sendbuf, 0, nu, ib.s, ib.e, jb.s, jb.e, kb.s, kb.e, p,
                            pmb);
  }
  for (auto &pvar_fc : pmb->vars_fc_) {
    auto &var_fc = *pvar_fc;
    ParArray3D<Real> x1f = var_fc.x1f.Get<3>();
    ParArray3D<Real> x2f = var_fc.x2f.Get<3>();
    ParArray3D<Real> x3f = var_fc.x3f.Get<3>();
    BufferUtility::PackData(x1f, sendbuf, ib.s, ib.e + 1, jb.s, jb.e, kb.s, kb.e, p, pmb);
    BufferUtility::PackData(x2f, sendbuf, ib.s, ib.e, jb.s, jb.e + f2, kb.s, kb.e, p,
                            pmb);
    BufferUtility::PackData(x3f, sendbuf, ib.s, ib.e, jb.s, jb.e, kb.s, kb.e + f3, p,
                            pmb);
  }
  // WARNING(felker): casting from "Real *" to "int *" in order to append single integer
  // to send buffer is slightly unsafe (especially if sizeof(int) > sizeof(Real))
  if (adaptive) {
    Kokkos::deep_copy(pmb->exec_space,
                      Kokkos::View<int, Kokkos::MemoryUnmanaged>(
                          reinterpret_cast<int *>(Kokkos::subview(sendbuf, p).data())),
                      pmb->pmr->deref_count_);
  }
  return;
}

// step 6, branch 2 (c2f: just pack+send)

void Mesh::PrepareSendCoarseToFineAMR(MeshBlock *pb, ParArray1D<Real> &sendbuf,
                                      LogicalLocation &lloc) {
  const int f2 = (ndim >= 2) ? 1 : 0; // extra cells/faces from being 2d
  const int f3 = (ndim >= 3) ? 1 : 0; // extra cells/faces from being 3d
  int ox1 = ((lloc.lx1 & 1LL) == 1LL), ox2 = ((lloc.lx2 & 1LL) == 1LL),
      ox3 = ((lloc.lx3 & 1LL) == 1LL);
  const IndexDomain interior = IndexDomain::interior;
  // pack
  int il, iu, jl, ju, kl, ku;
  if (ox1 == 0) {
    il = pb->cellbounds.is(interior) - 1;
    iu = pb->cellbounds.is(interior) + pb->block_size.nx1 / 2;
  } else {
    il = pb->cellbounds.is(interior) + pb->block_size.nx1 / 2 - 1;
    iu = pb->cellbounds.ie(interior) + 1;
  }
  if (ox2 == 0) {
    jl = pb->cellbounds.js(interior) - f2;
    ju = pb->cellbounds.js(interior) + pb->block_size.nx2 / 2;
  } else {
    jl = pb->cellbounds.js(interior) + pb->block_size.nx2 / 2 - f2;
    ju = pb->cellbounds.je(interior) + f2;
  }
  if (ox3 == 0) {
    kl = pb->cellbounds.ks(interior) - f3;
    ku = pb->cellbounds.ks(interior) + pb->block_size.nx3 / 2;
  } else {
    kl = pb->cellbounds.ks(interior) + pb->block_size.nx3 / 2 - f3;
    ku = pb->cellbounds.ke(interior) + f3;
  }
  int p = 0;
  for (auto cc_pair : pb->pmr->pvars_cc_) {
    ParArray4D<Real> var_cc = std::get<0>(cc_pair).Get<4>();
    int nu = var_cc.extent(0) - 1;
    BufferUtility::PackData(var_cc, sendbuf, 0, nu, il, iu, jl, ju, kl, ku, p, pb);
  }
  for (auto fc_pair : pb->pmr->pvars_fc_) {
    FaceField *var_fc = std::get<0>(fc_pair);
    ParArray3D<Real> x1f = (*var_fc).x1f.Get<3>();
    ParArray3D<Real> x2f = (*var_fc).x2f.Get<3>();
    ParArray3D<Real> x3f = (*var_fc).x3f.Get<3>();
    BufferUtility::PackData(x1f, sendbuf, il, iu + 1, jl, ju, kl, ku, p, pb);
    BufferUtility::PackData(x2f, sendbuf, il, iu, jl, ju + f2, kl, ku, p, pb);
    BufferUtility::PackData(x3f, sendbuf, il, iu, jl, ju, kl, ku + f3, p, pb);
  }
  return;
}

// step 6, branch 3 (f2c: restrict, pack, send)

void Mesh::PrepareSendFineToCoarseAMR(MeshBlock *pb, ParArray1D<Real> &sendbuf) {
  // restrict and pack
  const int f2 = (ndim >= 2) ? 1 : 0; // extra cells/faces from being 2d
  const int f3 = (ndim >= 3) ? 1 : 0; // extra cells/faces from being 3d

  const IndexDomain interior = IndexDomain::interior;
  IndexRange cib = pb->c_cellbounds.GetBoundsI(interior);
  IndexRange cjb = pb->c_cellbounds.GetBoundsJ(interior);
  IndexRange ckb = pb->c_cellbounds.GetBoundsK(interior);

  auto &pmr = pb->pmr;
  int p = 0;
  for (auto cc_pair : pmr->pvars_cc_) {
    ParArrayND<Real> var_cc = std::get<0>(cc_pair);
    ParArrayND<Real> coarse_cc = std::get<1>(cc_pair);
    int nu = var_cc.GetDim(4) - 1;
    pmr->RestrictCellCenteredValues(var_cc, coarse_cc, 0, nu, cib.s, cib.e, cjb.s, cjb.e,
                                    ckb.s, ckb.e);
    // TOGO(pgrete) remove temp var once Restrict func interface is updated
    ParArray4D<Real> coarse_cc_ = coarse_cc.Get<4>();
    BufferUtility::PackData(coarse_cc_, sendbuf, 0, nu, cib.s, cib.e, cjb.s, cjb.e, ckb.s,
                            ckb.e, p, pb);
  }
  for (auto fc_pair : pb->pmr->pvars_fc_) {
    FaceField *var_fc = std::get<0>(fc_pair);
    FaceField *coarse_fc = std::get<1>(fc_pair);
    ParArray3D<Real> x1f = (*coarse_fc).x1f.Get<3>();
    ParArray3D<Real> x2f = (*coarse_fc).x2f.Get<3>();
    ParArray3D<Real> x3f = (*coarse_fc).x3f.Get<3>();
    pmr->RestrictFieldX1((*var_fc).x1f, (*coarse_fc).x1f, cib.s, cib.e + 1, cjb.s, cjb.e,
                         ckb.s, ckb.e);
    BufferUtility::PackData(x1f, sendbuf, cib.s, cib.e + 1, cjb.s, cjb.e, ckb.s, ckb.e, p,
                            pb);
    pmr->RestrictFieldX2((*var_fc).x2f, (*coarse_fc).x2f, cib.s, cib.e, cjb.s, cjb.e + f2,
                         ckb.s, ckb.e);
    BufferUtility::PackData(x2f, sendbuf, cib.s, cib.e, cjb.s, cjb.e + f2, ckb.s, ckb.e,
                            p, pb);
    pmr->RestrictFieldX3((*var_fc).x3f, (*coarse_fc).x3f, cib.s, cib.e, cjb.s, cjb.e,
                         ckb.s, ckb.e + f3);
    BufferUtility::PackData(x3f, sendbuf, cib.s, cib.e, cjb.s, cjb.e, ckb.s, ckb.e + f3,
                            p, pb);
  }
  return;
}

// step 7: f2c, same MPI rank, different level (just restrict+copy, no pack/send)

void Mesh::FillSameRankFineToCoarseAMR(MeshBlock *pob, MeshBlock *pmb,
                                       LogicalLocation &loc) {
  auto &pmr = pob->pmr;
  const IndexDomain interior = IndexDomain::interior;
  int il =
      pmb->cellbounds.is(interior) + ((loc.lx1 & 1LL) == 1LL) * pmb->block_size.nx1 / 2;
  int jl =
      pmb->cellbounds.js(interior) + ((loc.lx2 & 1LL) == 1LL) * pmb->block_size.nx2 / 2;
  int kl =
      pmb->cellbounds.ks(interior) + ((loc.lx3 & 1LL) == 1LL) * pmb->block_size.nx3 / 2;

  IndexRange cib = pob->c_cellbounds.GetBoundsI(interior);
  IndexRange cjb = pob->c_cellbounds.GetBoundsJ(interior);
  IndexRange ckb = pob->c_cellbounds.GetBoundsK(interior);
  // absent a zip() feature for range-based for loops, manually advance the
  // iterator over "SMR/AMR-enrolled" cell-centered quantities on the new
  // MeshBlock in lock-step with pob
  auto pmb_cc_it = pmb->pmr->pvars_cc_.begin();
  // iterate MeshRefinement std::vectors on pob
  for (auto cc_pair : pmr->pvars_cc_) {
    ParArrayND<Real> var_cc = std::get<0>(cc_pair);
    ParArrayND<Real> coarse_cc = std::get<1>(cc_pair);
    int nu = var_cc.GetDim(4) - 1;
    pmr->RestrictCellCenteredValues(var_cc, coarse_cc, 0, nu, cib.s, cib.e, cjb.s, cjb.e,
                                    ckb.s, ckb.e);

    // copy from old/original/other MeshBlock (pob) to newly created block (pmb)
    ParArrayND<Real> src = coarse_cc;
    ParArrayND<Real> dst = std::get<0>(*pmb_cc_it);
    int koff = kl - ckb.s;
    int joff = jl - cjb.s;
    int ioff = il - cib.s;
    pmb->par_for(
        "FillSameRankFineToCoarseAMR", 0, nu, ckb.s, ckb.e, cjb.s, cjb.e, cib.s, cib.e,
        KOKKOS_LAMBDA(const int nv, const int k, const int j, const int i) {
          dst(nv, k + koff, j + joff, i + ioff) = src(nv, k, j, i);
        });
    pmb_cc_it++;
  }

  const int f2 = (ndim >= 2) ? 1 : 0; // extra cells/faces from being 2d
  const int f3 = (ndim >= 3) ? 1 : 0; // extra cells/faces from being 3d

  auto pmb_fc_it = pmb->pmr->pvars_fc_.begin();
  for (auto fc_pair : pmr->pvars_fc_) {
    FaceField *var_fc = std::get<0>(fc_pair);
    FaceField *coarse_fc = std::get<1>(fc_pair);
    pmr->RestrictFieldX1((*var_fc).x1f, (*coarse_fc).x1f, cib.s, cib.e + 1, cjb.s, cjb.e,
                         ckb.s, ckb.e);
    pmr->RestrictFieldX2((*var_fc).x2f, (*coarse_fc).x2f, cib.s, cib.e, cjb.s, cjb.e + f2,
                         ckb.s, ckb.e);
    pmr->RestrictFieldX3((*var_fc).x3f, (*coarse_fc).x3f, cib.s, cib.e, cjb.s, cjb.e,
                         ckb.s, ckb.e + f3);
    FaceField &src_b = *coarse_fc;
    FaceField &dst_b = *std::get<0>(*pmb_fc_it); // pmb->pfield->b;
    for (int k = kl, fk = ckb.s; fk <= ckb.e; k++, fk++) {
      for (int j = jl, fj = cjb.s; fj <= cjb.e; j++, fj++) {
        for (int i = il, fi = cib.s; fi <= cib.e + 1; i++, fi++)
          dst_b.x1f(k, j, i) = src_b.x1f(fk, fj, fi);
      }
    }
    for (int k = kl, fk = ckb.s; fk <= ckb.e; k++, fk++) {
      for (int j = jl, fj = cjb.s; fj <= cjb.e + f2; j++, fj++) {
        for (int i = il, fi = cib.s; fi <= cib.e; i++, fi++)
          dst_b.x2f(k, j, i) = src_b.x2f(fk, fj, fi);
      }
    }

    int ks = pmb->cellbounds.ks(interior);
    int js = pmb->cellbounds.js(interior);
    if (pmb->block_size.nx2 == 1) {
      int iu = il + pmb->block_size.nx1 / 2 - 1;
      for (int i = il; i <= iu; i++)
        dst_b.x2f(ks, js + 1, i) = dst_b.x2f(ks, js, i);
    }
    for (int k = kl, fk = ckb.s; fk <= ckb.e + f3; k++, fk++) {
      for (int j = jl, fj = cjb.s; fj <= cjb.e; j++, fj++) {
        for (int i = il, fi = cib.s; fi <= cib.e; i++, fi++)
          dst_b.x3f(k, j, i) = src_b.x3f(fk, fj, fi);
      }
    }
    if (pmb->block_size.nx3 == 1) {
      int iu = il + pmb->block_size.nx1 / 2 - 1, ju = jl + pmb->block_size.nx2 / 2 - 1;
      if (pmb->block_size.nx2 == 1) ju = jl;
      for (int j = jl; j <= ju; j++) {
        for (int i = il; i <= iu; i++)
          dst_b.x3f(ks + 1, j, i) = dst_b.x3f(ks, j, i);
      }
    }
    pmb_fc_it++;
  }
  return;
}

// step 7: c2f, same MPI rank, different level (just copy+prolongate, no pack/send)

void Mesh::FillSameRankCoarseToFineAMR(MeshBlock *pob, MeshBlock *pmb,
                                       LogicalLocation &newloc) {
  auto &pmr = pmb->pmr;

  const int f2 = (ndim >= 2) ? 1 : 0; // extra cells/faces from being 2d
  const int f3 = (ndim >= 3) ? 1 : 0; // extra cells/faces from being 3d

  const IndexDomain interior = IndexDomain::interior;
  int il = pob->c_cellbounds.is(interior) - 1;
  int iu = pob->c_cellbounds.ie(interior) + 1;
  int jl = pob->c_cellbounds.js(interior) - f2;
  int ju = pob->c_cellbounds.je(interior) + f2;
  int kl = pob->c_cellbounds.ks(interior) - f3;
  int ku = pob->c_cellbounds.ke(interior) + f3;

  int cis = ((newloc.lx1 & 1LL) == 1LL) * pob->block_size.nx1 / 2 +
            pob->cellbounds.is(interior) - 1;
  int cjs = ((newloc.lx2 & 1LL) == 1LL) * pob->block_size.nx2 / 2 +
            pob->cellbounds.js(interior) - f2;
  int cks = ((newloc.lx3 & 1LL) == 1LL) * pob->block_size.nx3 / 2 +
            pob->cellbounds.ks(interior) - f3;

  auto pob_cc_it = pob->pmr->pvars_cc_.begin();
  // iterate MeshRefinement std::vectors on new pmb
  for (auto cc_pair : pmr->pvars_cc_) {
    ParArrayND<Real> var_cc = std::get<0>(cc_pair);
    ParArrayND<Real> coarse_cc = std::get<1>(cc_pair);
    int nu = var_cc.GetDim(4) - 1;

    ParArrayND<Real> src = std::get<0>(*pob_cc_it);
    ParArrayND<Real> dst = coarse_cc;
    // fill the coarse buffer
    // WARNING: potential Cuda stream pitfall (exec space of coarse and fine MB)
    // Need to make sure that both src and dst are done with all other task up to here
    pob->par_for(
        "FillSameRankCoarseToFineAMR", 0, nu, kl, ku, jl, ju, il, iu,
        KOKKOS_LAMBDA(const int nv, const int k, const int j, const int i) {
          dst(nv, k, j, i) = src(nv, k - kl + cks, j - jl + cjs, i - il + cis);
        });
    // keeping the original, following block for reference to indexing
    // for (int nv = 0; nv <= nu; nv++) {
    //   for (int k = kl, ck = cks; k <= ku; k++, ck++) {
    //     for (int j = jl, cj = cjs; j <= ju; j++, cj++) {
    //       for (int i = il, ci = cis; i <= iu; i++, ci++)
    //         dst(nv, k, j, i) = src(nv, ck, cj, ci);
    //     }
    //   }
    // }
    pmr->ProlongateCellCenteredValues(
        dst, var_cc, 0, nu, pob->c_cellbounds.is(interior),
        pob->c_cellbounds.ie(interior), pob->c_cellbounds.js(interior),
        pob->c_cellbounds.je(interior), pob->c_cellbounds.ks(interior),
        pob->c_cellbounds.ke(interior));
    pob_cc_it++;
  }
  auto pob_fc_it = pob->pmr->pvars_fc_.begin();
  // iterate MeshRefinement std::vectors on new pmb
  for (auto fc_pair : pmr->pvars_fc_) {
    FaceField *var_fc = std::get<0>(fc_pair);
    FaceField *coarse_fc = std::get<1>(fc_pair);

    FaceField &src_b = *std::get<0>(*pob_fc_it);
    FaceField &dst_b = *coarse_fc;
    for (int k = kl, ck = cks; k <= ku; k++, ck++) {
      for (int j = jl, cj = cjs; j <= ju; j++, cj++) {
        for (int i = il, ci = cis; i <= iu + 1; i++, ci++)
          dst_b.x1f(k, j, i) = src_b.x1f(ck, cj, ci);
      }
    }
    for (int k = kl, ck = cks; k <= ku; k++, ck++) {
      for (int j = jl, cj = cjs; j <= ju + f2; j++, cj++) {
        for (int i = il, ci = cis; i <= iu; i++, ci++)
          dst_b.x2f(k, j, i) = src_b.x2f(ck, cj, ci);
      }
    }
    for (int k = kl, ck = cks; k <= ku + f3; k++, ck++) {
      for (int j = jl, cj = cjs; j <= ju; j++, cj++) {
        for (int i = il, ci = cis; i <= iu; i++, ci++)
          dst_b.x3f(k, j, i) = src_b.x3f(ck, cj, ci);
      }
    }
    pmr->ProlongateSharedFieldX1(
        dst_b.x1f, (*var_fc).x1f, pob->c_cellbounds.is(interior),
        pob->c_cellbounds.ie(interior) + 1, pob->c_cellbounds.js(interior),
        pob->c_cellbounds.je(interior), pob->c_cellbounds.ks(interior),
        pob->c_cellbounds.ke(interior));
    pmr->ProlongateSharedFieldX2(
        dst_b.x2f, (*var_fc).x2f, pob->c_cellbounds.is(interior),
        pob->c_cellbounds.ie(interior), pob->c_cellbounds.js(interior),
        pob->c_cellbounds.je(interior) + f2, pob->c_cellbounds.ks(interior),
        pob->c_cellbounds.ke(interior));
    pmr->ProlongateSharedFieldX3(
        dst_b.x3f, (*var_fc).x3f, pob->c_cellbounds.is(interior),
        pob->c_cellbounds.ie(interior), pob->c_cellbounds.js(interior),
        pob->c_cellbounds.je(interior), pob->c_cellbounds.ks(interior),
        pob->c_cellbounds.ke(interior) + f3);
    pmr->ProlongateInternalField(
        *var_fc, pob->c_cellbounds.is(interior), pob->c_cellbounds.ie(interior),
        pob->c_cellbounds.js(interior), pob->c_cellbounds.je(interior),
        pob->c_cellbounds.ks(interior), pob->c_cellbounds.ke(interior));

    pob_fc_it++;
  }
  return;
}

// step 8 (receive and load), branch 1 (same2same: unpack)
void Mesh::FinishRecvSameLevel(MeshBlock *pmb, ParArray1D<Real> &recvbuf) {
  int p = 0;

  const int f2 = (ndim >= 2) ? 1 : 0; // extra cells/faces from being 2d
  const int f3 = (ndim >= 3) ? 1 : 0; // extra cells/faces from being 3d

  const IndexDomain interior = IndexDomain::interior;
  IndexRange ib = pmb->cellbounds.GetBoundsI(interior);
  IndexRange jb = pmb->cellbounds.GetBoundsJ(interior);
  IndexRange kb = pmb->cellbounds.GetBoundsK(interior);

  for (auto &pvar_cc : pmb->vars_cc_) {
    int nu = pvar_cc->GetDim(4) - 1;
    ParArray4D<Real> var_cc_ = pvar_cc->data.Get<4>();
    BufferUtility::UnpackData(recvbuf, var_cc_, 0, nu, ib.s, ib.e, jb.s, jb.e, kb.s, kb.e,
                              p, pmb);
  }
  for (auto &pvar_fc : pmb->vars_fc_) {
    auto &var_fc = *pvar_fc;
    ParArray3D<Real> x1f = var_fc.x1f.Get<3>();
    ParArray3D<Real> x2f = var_fc.x2f.Get<3>();
    ParArray3D<Real> x3f = var_fc.x3f.Get<3>();
    BufferUtility::UnpackData(recvbuf, x1f, ib.s, ib.e + 1, jb.s, jb.e, kb.s, kb.e, p,
                              pmb);
    BufferUtility::UnpackData(recvbuf, x2f, ib.s, ib.e, jb.s, jb.e + f2, kb.s, kb.e, p,
                              pmb);
    BufferUtility::UnpackData(recvbuf, x3f, ib.s, ib.e, jb.s, jb.e, kb.s, kb.e + f3, p,
                              pmb);
    if (pmb->block_size.nx2 == 1) {
      for (int i = ib.s; i <= ib.e; i++)
        var_fc.x2f(kb.s, jb.s + 1, i) = var_fc.x2f(kb.s, jb.s, i);
    }
    if (pmb->block_size.nx3 == 1) {
      for (int j = jb.s; j <= jb.e; j++) {
        for (int i = ib.s; i <= ib.e; i++)
          var_fc.x3f(kb.s + 1, j, i) = var_fc.x3f(kb.s, j, i);
      }
    }
  }
  // WARNING(felker): casting from "Real *" to "int *" in order to read single
  // appended integer from received buffer is slightly unsafe
  if (adaptive) {
    Kokkos::deep_copy(pmb->exec_space, pmb->pmr->deref_count_,
                      Kokkos::View<int, Kokkos::MemoryUnmanaged>(
                          reinterpret_cast<int *>(Kokkos::subview(recvbuf, p).data())));
  }
  return;
}

// step 8 (receive and load), branch 2 (f2c: unpack)
void Mesh::FinishRecvFineToCoarseAMR(MeshBlock *pb, ParArray1D<Real> &recvbuf,
                                     LogicalLocation &lloc) {
  const int f2 = (ndim >= 2) ? 1 : 0; // extra cells/faces from being 2d
  const int f3 = (ndim >= 3) ? 1 : 0; // extra cells/faces from being 3d

  const IndexDomain interior = IndexDomain::interior;
  IndexRange ib = pb->cellbounds.GetBoundsI(interior);
  IndexRange jb = pb->cellbounds.GetBoundsJ(interior);
  IndexRange kb = pb->cellbounds.GetBoundsK(interior);

  int ox1 = ((lloc.lx1 & 1LL) == 1LL), ox2 = ((lloc.lx2 & 1LL) == 1LL),
      ox3 = ((lloc.lx3 & 1LL) == 1LL);
  int p = 0, il, iu, jl, ju, kl, ku;

  if (ox1 == 0)
    il = ib.s, iu = ib.s + pb->block_size.nx1 / 2 - 1;
  else
    il = ib.s + pb->block_size.nx1 / 2, iu = ib.e;
  if (ox2 == 0)
    jl = jb.s, ju = jb.s + pb->block_size.nx2 / 2 - f2;
  else
    jl = jb.s + pb->block_size.nx2 / 2, ju = jb.e;
  if (ox3 == 0)
    kl = kb.s, ku = kb.s + pb->block_size.nx3 / 2 - f3;
  else
    kl = kb.s + pb->block_size.nx3 / 2, ku = kb.e;

  for (auto cc_pair : pb->pmr->pvars_cc_) {
    ParArray4D<Real> var_cc = std::get<0>(cc_pair).Get<4>();
    int nu = var_cc.extent(0) - 1;
    BufferUtility::UnpackData(recvbuf, var_cc, 0, nu, il, iu, jl, ju, kl, ku, p, pb);
  }
  for (auto fc_pair : pb->pmr->pvars_fc_) {
    FaceField *var_fc = std::get<0>(fc_pair);
    FaceField &dst_b = *var_fc;
    ParArray3D<Real> x1f = dst_b.x1f.Get<3>();
    ParArray3D<Real> x2f = dst_b.x2f.Get<3>();
    ParArray3D<Real> x3f = dst_b.x3f.Get<3>();
    BufferUtility::UnpackData(recvbuf, x1f, il, iu + 1, jl, ju, kl, ku, p, pb);
    BufferUtility::UnpackData(recvbuf, x2f, il, iu, jl, ju + f2, kl, ku, p, pb);
    BufferUtility::UnpackData(recvbuf, x3f, il, iu, jl, ju, kl, ku + f3, p, pb);
    if (pb->block_size.nx2 == 1) {
      for (int i = il; i <= iu; i++)
        dst_b.x2f(kb.s, jb.s + 1, i) = dst_b.x2f(kb.s, jb.s, i);
    }
    if (pb->block_size.nx3 == 1) {
      for (int j = jl; j <= ju; j++) {
        for (int i = il; i <= iu; i++)
          dst_b.x3f(kb.s + 1, j, i) = dst_b.x3f(kb.s, j, i);
      }
    }
  }
  return;
}

// step 8 (receive and load), branch 2 (c2f: unpack+prolongate)
void Mesh::FinishRecvCoarseToFineAMR(MeshBlock *pb, ParArray1D<Real> &recvbuf) {
  const int f2 = (ndim >= 2) ? 1 : 0; // extra cells/faces from being 2d
  const int f3 = (ndim >= 3) ? 1 : 0; // extra cells/faces from being 3d
  auto &pmr = pb->pmr;
  int p = 0;

  const IndexDomain interior = IndexDomain::interior;
  IndexRange cib = pb->c_cellbounds.GetBoundsI(interior);
  IndexRange cjb = pb->c_cellbounds.GetBoundsJ(interior);
  IndexRange ckb = pb->c_cellbounds.GetBoundsK(interior);

  int il = cib.s - 1, iu = cib.e + 1, jl = cjb.s - f2, ju = cjb.e + f2, kl = ckb.s - f3,
      ku = ckb.e + f3;

  for (auto cc_pair : pb->pmr->pvars_cc_) {
    ParArrayND<Real> var_cc = std::get<0>(cc_pair);
    ParArrayND<Real> coarse_cc = std::get<1>(cc_pair);
    int nu = var_cc.GetDim(4) - 1;
    ParArray4D<Real> coarse_cc_ = coarse_cc.Get<4>();
    BufferUtility::UnpackData(recvbuf, coarse_cc_, 0, nu, il, iu, jl, ju, kl, ku, p, pb);
    pmr->ProlongateCellCenteredValues(coarse_cc, var_cc, 0, nu, cib.s, cib.e, cjb.s,
                                      cjb.e, ckb.s, ckb.e);
  }
  for (auto fc_pair : pb->pmr->pvars_fc_) {
    FaceField *var_fc = std::get<0>(fc_pair);
    FaceField *coarse_fc = std::get<1>(fc_pair);

    ParArray3D<Real> x1f = (*coarse_fc).x1f.Get<3>();
    ParArray3D<Real> x2f = (*coarse_fc).x2f.Get<3>();
    ParArray3D<Real> x3f = (*coarse_fc).x3f.Get<3>();
    BufferUtility::UnpackData(recvbuf, x1f, il, iu + 1, jl, ju, kl, ku, p, pb);
    BufferUtility::UnpackData(recvbuf, x2f, il, iu, jl, ju + f2, kl, ku, p, pb);
    BufferUtility::UnpackData(recvbuf, x3f, il, iu, jl, ju, kl, ku + f3, p, pb);
    pmr->ProlongateSharedFieldX1((*coarse_fc).x1f, (*var_fc).x1f, cib.s, cib.e + 1, cjb.s,
                                 cjb.e, ckb.s, ckb.e);
    pmr->ProlongateSharedFieldX2((*coarse_fc).x2f, (*var_fc).x2f, cib.s, cib.e, cjb.s,
                                 cjb.e + f2, ckb.s, ckb.e);
    pmr->ProlongateSharedFieldX3((*coarse_fc).x3f, (*var_fc).x3f, cib.s, cib.e, cjb.s,
                                 cjb.e, ckb.s, ckb.e + f3);
    pmr->ProlongateInternalField(*var_fc, cib.s, cib.e, cjb.s, cjb.e, ckb.s, ckb.e);
  }
  return;
}

//----------------------------------------------------------------------------------------
//! \fn int CreateAMRMPITag(int lid, int ox1, int ox2, int ox3)
//  \brief calculate an MPI tag for AMR block transfer
// tag = local id of destination (remaining bits) + ox1(1 bit) + ox2(1 bit) + ox3(1 bit)
//       + physics(5 bits)

// See comments on BoundaryBase::CreateBvalsMPITag()

int Mesh::CreateAMRMPITag(int lid, int ox1, int ox2, int ox3) {
  // former "AthenaTagMPI" AthenaTagMPI::amr=8 redefined to 0
  return (lid << 8) | (ox1 << 7) | (ox2 << 6) | (ox3 << 5) | 0;
}

} // namespace parthenon
