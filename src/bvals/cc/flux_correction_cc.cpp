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
//! \file flux_correction_cc.cpp
//  \brief functions that perform flux correction for CELL_CENTERED variables

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

#include "parthenon_mpi.hpp"

#include "bvals/cc/bvals_cc.hpp"
#include "coordinates/coordinates.hpp"
#include "defs.hpp"
#include "globals.hpp"
#include "mesh/mesh.hpp"
#include "mesh/meshblock.hpp"
#include "parameter_input.hpp"
#include "utils/buffer_utils.hpp"

namespace parthenon {

//----------------------------------------------------------------------------------------
//! \fn void CellCenteredBoundaryVariable::SendFluxCorrection()
//  \brief Restrict, pack and send the surface flux to the coarse neighbor(s)

void CellCenteredBoundaryVariable::SendFluxCorrection() {
  std::shared_ptr<MeshBlock> pmb = GetBlockPointer();
  auto &coords = pmb->coords;
  const IndexDomain interior = IndexDomain::interior;

  for (int n = 0; n < pmb->pbval->nneighbor; n++) {
    NeighborBlock &nb = pmb->pbval->neighbor[n];
    if (nb.ni.type != NeighborConnect::face) break;
    if (bd_var_flcor_.sflag[nb.bufid] == BoundaryStatus::completed) continue;
    if (nb.snb.level == pmb->loc.level - 1) {
      int psize = 0;
      IndexRange ib = pmb->cellbounds.GetBoundsI(interior);
      IndexRange jb = pmb->cellbounds.GetBoundsJ(interior);
      IndexRange kb = pmb->cellbounds.GetBoundsK(interior);
      int nx1 = pmb->cellbounds.ncellsi(interior);
      int nx2 = pmb->cellbounds.ncellsj(interior);
      int nx3 = pmb->cellbounds.ncellsk(interior);
      ParArray1D<Real> &sbuf = bd_var_flcor_.send[nb.bufid];
      int nl = nl_;
      // x1 direction
      if (nb.fid == BoundaryFace::inner_x1 || nb.fid == BoundaryFace::outer_x1) {
        int i = ib.s + nx1 * nb.fid;
        int ks = kb.s;
        int js = jb.s;
        int ksize = (kb.e - kb.s + 1) / 2;
        int jsize = (jb.e - jb.s + 1) / 2;
        auto &x1flx = x1flux;
        if (pmb->block_size.nx3 > 1) { // 3D
          psize = jsize * ksize * (nu_ - nl_ + 1);
          pmb->par_for(
              "SendFluxCorrection3D_x1", nl_, nu_, 0, ksize - 1, 0, jsize - 1,
              KOKKOS_LAMBDA(const int nn, const int k, const int j) {
                const int kf = 2 * k + ks;
                const int jf = 2 * j + js;
                const Real amm = coords.Area(X1DIR, kf, jf, i);
                const Real amp = coords.Area(X1DIR, kf, jf + 1, i);
                const Real apm = coords.Area(X1DIR, kf + 1, jf, i);
                const Real app = coords.Area(X1DIR, kf + 1, jf + 1, i);
                const Real tarea = amm + amp + apm + app;
                const int p = j + jsize * (k + ksize * (nn - nl));
                sbuf(p) = (x1flx(nn, kf, jf, i) * amm + x1flx(nn, kf, jf + 1, i) * amp +
                           x1flx(nn, kf + 1, jf, i) * apm +
                           x1flx(nn, kf + 1, jf + 1, i) * app) /
                          tarea;
              });
        } else if (pmb->block_size.nx2 > 1) { // 2D
          int k = kb.s;
          psize = jsize * (nu_ - nl + 1);
          pmb->par_for(
              "SendFluxCorrection2D_x1", nl_, nu_, 0, jsize - 1,
              KOKKOS_LAMBDA(const int nn, const int j) {
                const int jf = 2 * j + js;
                const Real am = coords.Area(X1DIR, k, jf, i);
                const Real ap = coords.Area(X1DIR, k, jf + 1, i);
                const Real tarea = am + ap;
                const int p = j + jsize * (nn - nl);
                sbuf(p) =
                    (x1flx(nn, k, jf, i) * am + x1flx(nn, k, jf + 1, i) * ap) / tarea;
              });
        } else { // 1D
          int k = kb.s, j = jb.s;
          psize = nu_ - nl_ + 1;
          pmb->par_for(
              "SendFluxCorrection1D_x1", nl_, nu_,
              KOKKOS_LAMBDA(const int nn) { sbuf(nn - nl) = x1flx(nn, k, j, i); });
        }
        // x2 direction
      } else if (nb.fid == BoundaryFace::inner_x2 || nb.fid == BoundaryFace::outer_x2) {
        int j = jb.s + nx2 * (nb.fid & 1);
        int ks = kb.s;
        int is = ib.s;
        int ksize = (kb.e - kb.s + 1) / 2;
        int isize = (ib.e - ib.s + 1) / 2;
        auto &x2flx = x2flux;
        if (pmb->block_size.nx3 > 1) { // 3D
          psize = isize * ksize * (nu_ - nl_ + 1);
          pmb->par_for(
              "SendFluxCorrection3D_x2", nl_, nu_, 0, ksize - 1, 0, isize - 1,
              KOKKOS_LAMBDA(const int nn, const int k, const int i) {
                const int kf = 2 * k + ks;
                const int ii = 2 * i + is;
                const Real area00 = coords.Area(X2DIR, kf, j, ii);
                const Real area01 = coords.Area(X2DIR, kf, j, ii + 1);
                const Real area10 = coords.Area(X2DIR, kf + 1, j, ii);
                const Real area11 = coords.Area(X2DIR, kf + 1, j, ii + 1);
                const Real tarea = area00 + area01 + area10 + area11;
                const int p = i + isize * (k + ksize * (nn - nl));
                sbuf(p) =
                    (x2flx(nn, kf, j, ii) * area00 + x2flx(nn, kf, j, ii + 1) * area01 +
                     x2flx(nn, kf + 1, j, ii) * area10 +
                     x2flx(nn, kf + 1, j, ii + 1) * area11) /
                    tarea;
              });
        } else if (pmb->block_size.nx2 > 1) { // 2D
          int k = kb.s;
          psize = isize * (nu_ - nl_ + 1);
          pmb->par_for(
              "SendFluxCorrection2D_x2", nl_, nu_, 0, isize - 1,
              KOKKOS_LAMBDA(const int nn, const int i) {
                const int ii = 2 * i + is;
                const Real area0 = coords.Area(X2DIR, k, j, ii);
                const Real area1 = coords.Area(X2DIR, k, j, ii + 1);
                const Real tarea = area0 + area1;
                const int p = i + isize * (nn - nl);
                sbuf(p) =
                    (x2flx(nn, k, j, ii) * area0 + x2flx(nn, k, j, ii + 1) * area1) /
                    tarea;
              });
        }
        // x3 direction - 3D only
      } else if (nb.fid == BoundaryFace::inner_x3 || nb.fid == BoundaryFace::outer_x3) {
        int k = kb.s + nx3 * (nb.fid & 1);
        int js = jb.s;
        int is = ib.s;
        int jsize = (jb.e - jb.s + 1) / 2;
        int isize = (ib.e - ib.s + 1) / 2;
        auto &x3flx = x3flux;
        psize = isize * jsize * (nu_ - nl_ + 1);
        pmb->par_for(
            "SendFluxCorrection3D_x3", nl_, nu_, 0, jsize - 1, 0, isize - 1,
            KOKKOS_LAMBDA(const int nn, const int j, const int i) {
              const int jf = 2 * j + js;
              const int ii = 2 * i + is;
              const Real area00 = coords.Area(X3DIR, k, jf, ii);
              const Real area01 = coords.Area(X3DIR, k, jf, ii + 1);
              const Real area10 = coords.Area(X3DIR, k, jf + 1, ii);
              const Real area11 = coords.Area(X3DIR, k, jf + 1, ii + 1);
              const Real tarea = area00 + area01 + area10 + area11;
              const int p = i + isize * (j + jsize * (nn - nl));
              sbuf(p) =
                  (x3flx(nn, k, jf, ii) * area00 + x3flx(nn, k, jf, ii + 1) * area01 +
                   x3flx(nn, k, jf + 1, ii) * area10 +
                   x3flx(nn, k, jf + 1, ii + 1) * area11) /
                  tarea;
            });
      }
      pmb->exec_space.fence();
      if (nb.snb.rank == Globals::my_rank) { // on the same node
        CopyFluxCorrectionBufferSameProcess(nb, psize);
      }
#ifdef MPI_PARALLEL
      else
        PARTHENON_MPI_CHECK(MPI_Start(&(bd_var_flcor_.req_send[nb.bufid])));
#endif
      bd_var_flcor_.sflag[nb.bufid] = BoundaryStatus::completed;
    }
  }
  return;
}

//----------------------------------------------------------------------------------------
//! \fn bool CellCenteredBoundaryVariable::ReceiveFluxCorrection()
//  \brief Receive and apply the surface flux from the finer neighbor(s)

bool CellCenteredBoundaryVariable::ReceiveFluxCorrection() {
  std::shared_ptr<MeshBlock> pmb = GetBlockPointer();
  bool bflag = true;

  for (int n = 0; n < pmb->pbval->nneighbor; n++) {
    NeighborBlock &nb = pmb->pbval->neighbor[n];
    if (nb.ni.type != NeighborConnect::face) break;
    if (nb.snb.level == pmb->loc.level + 1) {
      if (bd_var_flcor_.flag[nb.bufid] == BoundaryStatus::completed) continue;
      if (bd_var_flcor_.flag[nb.bufid] == BoundaryStatus::waiting) {
        if (nb.snb.rank == Globals::my_rank) { // on the same process
          bflag = false;
          continue;
        }
#ifdef MPI_PARALLEL
        else { // NOLINT
          int test;
          PARTHENON_MPI_CHECK(MPI_Iprobe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD,
                                         &test, MPI_STATUS_IGNORE));
          PARTHENON_MPI_CHECK(
              MPI_Test(&(bd_var_flcor_.req_recv[nb.bufid]), &test, MPI_STATUS_IGNORE));
          if (!static_cast<bool>(test)) {
            bflag = false;
            continue;
          }
          bd_var_flcor_.flag[nb.bufid] = BoundaryStatus::arrived;
        }
#endif
      }
      // boundary arrived; apply flux correction
      ParArray1D<Real> &rbuf = bd_var_flcor_.recv[nb.bufid];
      int nl = nl_;
      const IndexDomain interior = IndexDomain::interior;
      IndexRange ib = pmb->cellbounds.GetBoundsI(interior);
      IndexRange jb = pmb->cellbounds.GetBoundsJ(interior);
      IndexRange kb = pmb->cellbounds.GetBoundsK(interior);
      if (nb.fid == BoundaryFace::inner_x1 || nb.fid == BoundaryFace::outer_x1) {
        int il = ib.s + (ib.e - ib.s) * nb.fid + nb.fid;
        int jl = jb.s, ju = jb.e, kl = kb.s, ku = kb.e;
        if (nb.ni.fi1 == 0)
          ju -= pmb->block_size.nx2 / 2;
        else
          jl += pmb->block_size.nx2 / 2;
        if (nb.ni.fi2 == 0)
          ku -= pmb->block_size.nx3 / 2;
        else
          kl += pmb->block_size.nx3 / 2;
        int jsize = ju - jl + 1;
        int ksize = ku - kl + 1;
        auto &x1flx = x1flux;
        pmb->par_for(
            "ReceiveFluxCorrection_x1", nl_, nu_, kl, ku, jl, ju,
            KOKKOS_LAMBDA(const int nn, const int k, const int j) {
              const int p = j - jl + jsize * ((k - kl) + ksize * (nn - nl));
              x1flx(nn, k, j, il) = rbuf(p);
            });
      } else if (nb.fid == BoundaryFace::inner_x2 || nb.fid == BoundaryFace::outer_x2) {
        int jl = jb.s + (jb.e - jb.s) * (nb.fid & 1) + (nb.fid & 1);
        int il = ib.s, iu = ib.e, kl = kb.s, ku = kb.e;
        if (nb.ni.fi1 == 0)
          iu -= pmb->block_size.nx1 / 2;
        else
          il += pmb->block_size.nx1 / 2;
        if (nb.ni.fi2 == 0)
          ku -= pmb->block_size.nx3 / 2;
        else
          kl += pmb->block_size.nx3 / 2;
        int ksize = ku - kl + 1;
        int isize = iu - il + 1;
        auto &x2flx = x2flux;
        pmb->par_for(
            "ReceiveFluxCorrection_x2", nl_, nu_, kl, ku, il, iu,
            KOKKOS_LAMBDA(const int nn, const int k, const int i) {
              const int p = i - il + isize * ((k - kl) + ksize * (nn - nl));
              x2flx(nn, k, jl, i) = rbuf(p);
            });
      } else if (nb.fid == BoundaryFace::inner_x3 || nb.fid == BoundaryFace::outer_x3) {
        int kl = kb.s + (kb.e - kb.s) * (nb.fid & 1) + (nb.fid & 1);
        int il = ib.s, iu = ib.e, jl = jb.s, ju = jb.e;
        if (nb.ni.fi1 == 0)
          iu -= pmb->block_size.nx1 / 2;
        else
          il += pmb->block_size.nx1 / 2;
        if (nb.ni.fi2 == 0)
          ju -= pmb->block_size.nx2 / 2;
        else
          jl += pmb->block_size.nx2 / 2;
        int jsize = ju - jl + 1;
        int isize = iu - il + 1;
        auto &x3flx = x3flux;
        pmb->par_for(
            "ReceiveFluxCorrection_x1", nl_, nu_, jl, ju, il, iu,
            KOKKOS_LAMBDA(const int nn, const int j, const int i) {
              const int p = i - il + isize * ((j - jl) + jsize * (nn - nl));
              x3flx(nn, kl, j, i) = rbuf(p);
            });
      }
      bd_var_flcor_.flag[nb.bufid] = BoundaryStatus::completed;
    }
  }
  return bflag;
}

} // namespace parthenon
