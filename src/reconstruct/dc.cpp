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
//! \file dc.cpp
//  \brief piecewise constant (donor cell) reconstruction

#include "reconstruction.hpp"

//----------------------------------------------------------------------------------------
//! \fn Reconstruction::DonorCellX1()
//  \brief reconstruct L/R surfaces of the i-th cells

void Reconstruction::DonorCellX1(const int k, const int j, const int il, const int iu,
                                 const AthenaArray<Real> &w, const AthenaArray<Real> &bcc,
                                 AthenaArray<Real> &wl, AthenaArray<Real> &wr) {
  // compute L/R states for each variable
  for (int n=0; n<NHYDRO; ++n) {
#pragma omp simd
    for (int i=il; i<=iu; ++i) {
      wl(n,i+1) =  wr(n,i) = w(n,k,j,i);
    }
  }
}

//----------------------------------------------------------------------------------------
//! \fn Reconstruction::DonorCellX2()
//  \brief


void Reconstruction::DonorCellX2(const int k, const int j, const int il, const int iu,
                                 const AthenaArray<Real> &w, const AthenaArray<Real> &bcc,
                                 AthenaArray<Real> &wl, AthenaArray<Real> &wr) {
  // compute L/R states for each variable
  for (int n=0; n<NHYDRO; ++n) {
#pragma omp simd
    for (int i=il; i<=iu; ++i) {
      wl(n,i) = wr(n,i) = w(n,k,j,i);
    }
  }
}

//----------------------------------------------------------------------------------------
//! \fn Reconstruction::DonorCellX3()
//  \brief

void Reconstruction::DonorCellX3(const int k, const int j, const int il, const int iu,
                                 const AthenaArray<Real> &w, const AthenaArray<Real> &bcc,
                                 AthenaArray<Real> &wl, AthenaArray<Real> &wr) {
  // compute L/R states for each variable
  for (int n=0; n<NHYDRO; ++n) {
#pragma omp simd
    for (int i=il; i<=iu; ++i) {
      wl(n,i) = wr(n,i) = w(n,k,j,i);
    }
  }
}
