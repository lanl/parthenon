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

#ifndef EXAMPLE_ADVECTION_ADVECTION_DRIVER_HPP_
#define EXAMPLE_ADVECTION_ADVECTION_DRIVER_HPP_

#include <parthenon/driver.hpp>

namespace advection_example {
using namespace parthenon::driver::prelude;

class AdvectionDriver : public MultiStageBlockTaskDriver {
 public:
  AdvectionDriver(ParameterInput *pin, Mesh *pm);
  // This next function essentially defines the driver.
  // Call graph looks like
  // main()
  //   EvolutionDriver::Execute (driver.cpp)
  //     MultiStageBlockTaskDriver::Step (multistage.cpp)
  //       DriverUtils::ConstructAndExecuteBlockTasks (driver.hpp)
  //         AdvectionDriver::MakeTaskList (advection.cpp)
  TaskList MakeTaskList(MeshBlock *pmb, int stage);
};

} // namespace advection_example

#endif // EXAMPLE_ADVECTION_ADVECTION_DRIVER_HPP_
