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
#ifndef UTILS_GL_QUADRATURE_HPP_
#define UTILS_GL_QUADRATURE_HPP_
//! \file gl_quadrature.hpp
//  \brief functions for computing the Gauss-Legendre (GL) quadrature of a given 1D, 2D,
// or 3D function. Provided for convenience / intended for use in pgen/*.cpp files.

// TODO(felker): add other Gaussian quadratures, or alternative approaches for computing
// the initial condition that outperform GL quadrature for a discontinuous function
#include "defs.hpp"

namespace parthenon {
namespace GaussLegendre {

// TODO(felker): for more complicate f(), use functors/lambdas/pass ParArrayND to both fn

// 1D f(x1)
Real integrate(const int n, Real (*f)(Real), Real x1l, Real x1u);

// 2D f(x1, x2)
Real integrate(const int n, Real (*f)(Real, Real), Real x1l, Real x1u, Real x2l,
               Real x2u);

// 3D f(x1, x2, x3)
Real integrate(const int n, Real (*f)(Real, Real, Real), Real x1l, Real x1u, Real x2l,
               Real x2u, Real x3l, Real x3u);

} // namespace GaussLegendre
} // namespace parthenon

#endif // UTILS_GL_QUADRATURE_HPP_
