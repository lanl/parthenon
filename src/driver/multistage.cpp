
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

#include "driver/multistage.hpp"

namespace parthenon {

MultiStageDriver::MultiStageDriver(ParameterInput *pin, ApplicationInput *app_in,
                                   Mesh *pm)
    : EvolutionDriver(pin, app_in, pm) {
  std::string integrator_name =
      pin->GetOrAddString("parthenon/time", "integrator", "rk2");
  int nstages = 2; // default rk2

  std::vector<Real> beta;
  if (!integrator_name.compare("rk1")) {
    nstages = 1;
    beta.resize(nstages);
    beta[0] = 1.0;
  } else if (!integrator_name.compare("rk2")) {
    nstages = 2;
    beta.resize(nstages);
    beta[0] = 1.0;
    beta[1] = 0.5;
  } else if (!integrator_name.compare("vl2")) {
    nstages = 2;
    beta.resize(nstages);
    beta[0] = 0.5;
    beta[1] = 1.0;
  } else if (!integrator_name.compare("rk3")) {
    nstages = 3;
    beta.resize(nstages);
    beta[0] = 1.0;
    beta[1] = 0.25;
    beta[2] = 2.0 / 3.0;
  } else {
    throw std::invalid_argument("Invalid selection for the time integrator: " +
                                integrator_name);
  }

  integrator = new Integrator(nstages, beta);
  stage_name.resize(nstages + 1);
  stage_name[0] = "base";
  for (int i = 1; i < nstages; i++) {
    stage_name[i] = std::to_string(i);
  }
  stage_name[nstages] = stage_name[0];
}

TaskListStatus MultiStageBlockTaskDriver::Step() {
  using DriverUtils::ConstructAndExecuteBlockTasks;
  TaskListStatus status;
  integrator->dt = tm.dt;
  for (int stage = 1; stage <= integrator->nstages; stage++) {
    status = ConstructAndExecuteBlockTasks<>(this, stage);
    if (status != TaskListStatus::complete) break;
  }
  return status;
}

} // namespace parthenon
