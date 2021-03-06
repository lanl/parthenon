//========================================================================================
// Parthenon performance portable AMR framework
// Copyright(C) 2021 The Parthenon collaboration
// Licensed under the 3-clause BSD License, see LICENSE file for details
//========================================================================================
// (C) (or copyright) 2021. Triad National Security, LLC. All rights reserved.
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
#ifndef EXAMPLE_PARTICLE_TRACERS_PARTICLE_TRACERS_HPP_
#define EXAMPLE_PARTICLE_TRACERS_PARTICLE_TRACERS_HPP_

#include <memory>

#include "Kokkos_Random.hpp"

#include <parthenon/driver.hpp>
#include <parthenon/package.hpp>

using namespace parthenon::driver::prelude;
using namespace parthenon::package::prelude;
using namespace parthenon;

namespace tracers_example {

class ParticleDriver : public MultiStageDriver {
 public:
  ParticleDriver(ParameterInput *pin, ApplicationInput *app_in, Mesh *pm)
      : MultiStageDriver(pin, app_in, pm) {}
  TaskCollection MakeTaskCollection(BlockList_t &blocks, int stage);
};

void ProblemGenerator(MeshBlock *pmb, ParameterInput *pin);
Packages_t ProcessPackages(std::unique_ptr<ParameterInput> &pin);

namespace particles_package {

std::shared_ptr<StateDescriptor> Initialize(ParameterInput *pin);
Real EstimateTimestepBlock(MeshBlockData<Real> *rc);

} // namespace particles_package

namespace advection_package {

std::shared_ptr<StateDescriptor> Initialize(ParameterInput *pin);
Real EstimateTimestepBlock(MeshBlockData<Real> *rc);

} // namespace advection_package

} // namespace tracers_example

#endif // EXAMPLE_PARTICLE_TRACERS_PARTICLE_TRACERS_HPP_
