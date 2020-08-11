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
#ifndef EXAMPLE_ADVECTION_ADVECTION_PACKAGE_HPP_
#define EXAMPLE_ADVECTION_ADVECTION_PACKAGE_HPP_

#include <memory>

#include <parthenon/package.hpp>

namespace advection_package {
using namespace parthenon::package::prelude;

std::shared_ptr<StateDescriptor> Initialize(ParameterInput *pin);
AmrTag CheckRefinement(std::shared_ptr<Container<Real>> &rc);
void PreFill(std::shared_ptr<Container<Real>> &rc);
void SquareIt(std::shared_ptr<Container<Real>> &rc);
void PostFill(std::shared_ptr<Container<Real>> &rc);
Real EstimateTimestep(std::shared_ptr<Container<Real>> &rc);
TaskStatus CalculateFluxesWithScratch(std::shared_ptr<Container<Real>> &rc);
TaskStatus CalculateFluxesNoScratch(std::shared_ptr<Container<Real>> &rc);

} // namespace advection_package

#endif // EXAMPLE_ADVECTION_ADVECTION_PACKAGE_HPP_
