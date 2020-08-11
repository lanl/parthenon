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

// Self Include
#include "calculate_pi.hpp"

// Standard Includes
#include <iostream>
#include <string>
#include <utility>

// Parthenon Includes
#include <coordinates/coordinates.hpp>
#include <parthenon/package.hpp>

using namespace parthenon::package::prelude;

namespace parthenon {

// can be used to set global properties that all meshblocks want to know about
// no need in this app so use the weak version that ships with parthenon
// Properties_t ParthenonManager::ProcessProperties(std::unique_ptr<ParameterInput>& pin)
// {
//  Properties_t props;
//  return props;
//}

Packages_t ParthenonManager::ProcessPackages(std::unique_ptr<ParameterInput> &pin) {
  Packages_t packages;
  // only have one package for this app, but will typically have more things added to
  packages["calculate_pi"] = calculate_pi::Initialize(pin.get());
  return packages;
}

// this should set up initial conditions of independent variables on the block
// this app only has one variable of derived type, so nothing to do here.
// in this case, just use the weak version
// void MeshBlock::ProblemGenerator(ParameterInput *pin) {
//  // nothing to do here for this app
//}

// applications can register functions to fill shared derived quantities
// before and/or after all the package FillDerived call backs
// in this case, just use the weak version that sets these to nullptr
// void ParthenonManager::SetFillDerivedFunctions() {
//  FillDerivedVariables::SetFillDerivedFunctions(nullptr,nullptr);
//}

} // namespace parthenon

// This defines a "physics" package
// In this case, calculate_pi provides the functions required to set up
// an indicator function in_or_out(x,y) = (r < r0 ? 1 : 0), and compute the area
// of a circle of radius r0 as A = \int d^x in_or_out(x,y) over the domain. Then
// pi \approx A/r0^2
namespace calculate_pi {

void SetInOrOut(std::shared_ptr<Container<Real>> &rc) {
  MeshBlock *pmb = rc->pmy_block;
  IndexRange ib = pmb->cellbounds.GetBoundsI(IndexDomain::interior);
  IndexRange jb = pmb->cellbounds.GetBoundsJ(IndexDomain::interior);
  IndexRange kb = pmb->cellbounds.GetBoundsK(IndexDomain::interior);
  ParArrayND<Real> &v = rc->Get("in_or_out").data;
  const auto &radius = pmb->packages["calculate_pi"]->Param<Real>("radius");
  auto &coords = pmb->coords;
  // Set an indicator function that indicates whether the cell center
  // is inside or outside of the circle we're interating the area of.
  // Loop bounds are set to catch the case where the edge is between the
  // cell centers of the first/last real cell and the first ghost cell
  pmb->par_for(
      "SetInOrOut", kb.s, kb.e, jb.s - 1, jb.e + 1, ib.s - 1, ib.e + 1,
      KOKKOS_LAMBDA(const int k, const int j, const int i) {
        Real rsq = std::pow(coords.x1v(i), 2) + std::pow(coords.x2v(j), 2);
        if (rsq < radius * radius) {
          v(k, j, i) = 1.0;
        } else {
          v(k, j, i) = 0.0;
        }
      });
}

std::shared_ptr<StateDescriptor> Initialize(ParameterInput *pin) {
  auto package = std::make_shared<StateDescriptor>("calculate_pi");
  Params &params = package->AllParams();

  Real radius = pin->GetOrAddReal("Pi", "radius", 1.0);
  params.Add("radius", radius);

  // add a variable called in_or_out that will hold the value of the indicator function
  std::string field_name("in_or_out");
  Metadata m({Metadata::Cell, Metadata::Derived});
  package->AddField(field_name, m, DerivedOwnership::unique);

  // All the package FillDerived and CheckRefinement functions are called by parthenon
  package->FillDerived = SetInOrOut;
  // could use package specific refinement tagging routine (see advection example), but
  // instead this example will make use of the parthenon shipped first derivative
  // criteria, as invoked in the input file
  // package->CheckRefinement = CheckRefinement;
  return package;
}

TaskStatus ComputeArea(MeshBlock *pmb) {
  // compute 1/r0^2 \int d^2x in_or_out(x,y) over the block's domain
  auto &rc = pmb->real_containers.Get();
  IndexRange ib = pmb->cellbounds.GetBoundsI(IndexDomain::interior);
  IndexRange jb = pmb->cellbounds.GetBoundsJ(IndexDomain::interior);
  IndexRange kb = pmb->cellbounds.GetBoundsK(IndexDomain::interior);
  auto &coords = pmb->coords;

  ParArrayND<Real> &v = rc->Get("in_or_out").data;

  Real area;
  Kokkos::parallel_reduce(
      "calculate_pi compute area",
      parthenon::par_policy(Kokkos::MDRangePolicy<Kokkos::Rank<3>>(
          pmb->exec_space, {kb.s, jb.s, ib.s}, {kb.e + 1, jb.e + 1, ib.e + 1},
          {1, 1, ib.e + 1 - ib.s})),
      KOKKOS_LAMBDA(int k, int j, int i, Real &larea) {
        larea += v(k, j, i) * coords.Area(parthenon::X3DIR, k, j, i);
      },
      area);
  Kokkos::deep_copy(pmb->exec_space, v.Get(0, 0, 0, 0, 0, 0), area);

  return TaskStatus::complete;
}

} // namespace calculate_pi
