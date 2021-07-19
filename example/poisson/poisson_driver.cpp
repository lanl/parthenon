//========================================================================================
// (C) (or copyright) 2020-2021. Triad National Security, LLC. All rights reserved.
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

#include <memory>
#include <string>
#include <vector>

// Local Includes
#include "bvals/cc/bvals_cc_in_one.hpp"
#include "interface/metadata.hpp"
#include "interface/update.hpp"
#include "mesh/meshblock_pack.hpp"
#include "mesh/refinement_cc_in_one.hpp"
#include "parthenon/driver.hpp"
#include "poisson_driver.hpp"
#include "poisson_package.hpp"
#include "refinement/refinement.hpp"

using namespace parthenon::driver::prelude;

namespace poisson_example {

parthenon::DriverStatus PoissonDriver::Execute() {
  pouts->MakeOutputs(pmesh, pinput);
  ConstructAndExecuteTaskLists<>(this);
  pouts->MakeOutputs(pmesh, pinput);
  return DriverStatus::complete;
}

TaskCollection PoissonDriver::MakeTaskCollection(BlockList_t &blocks) {
  using namespace parthenon::Update;
  TaskCollection tc;
  TaskID none(0);

  for (int i = 0; i < blocks.size(); i++) {
    auto &pmb = blocks[i];
    auto &base = pmb->meshblock_data.Get();
    pmb->meshblock_data.Add("delta", base);
  }

  // make/get a mesh_data container for the state
  auto &md = pmesh->mesh_data.GetOrAdd("base", 0);
  // make a mesh_data container for dphi
  auto &mdelta = pmesh->mesh_data.GetOrAdd("delta", 0);

  TaskRegion &solver_region = tc.AddRegion(1);
  TaskList &tl = solver_region[0];

  auto start_recv = tl.AddTask(none, &MeshData<Real>::StartReceiving, md.get(),
                               BoundaryCommSubset::all);
  auto &solver = tl.AddIteration();
  auto update = solver.AddTask(none, poisson_package::UpdatePhi, md.get(), mdelta.get());

  auto send =
      solver.AddTask(update, parthenon::cell_centered_bvars::SendBoundaryBuffers, md);

  auto recv = solver.AddTask(update | start_recv,
                             parthenon::cell_centered_bvars::ReceiveBoundaryBuffers, md);

  auto setb = solver.AddTask(recv, parthenon::cell_centered_bvars::SetBoundaries, md);

  auto clear = solver.AddTask(recv, &MeshData<Real>::ClearBoundary, md.get(),
                              BoundaryCommSubset::all);

  auto check = solver.AddCompletionTask(setb, poisson_package::CheckConvergence, md.get(),
                                        mdelta.get());

  int max_iters = pmesh->packages.Get("poisson_package")->Param<int>("max_iterations");
  solver.SetMaxIterations(max_iters);
  bool fail_flag =
      pmesh->packages.Get("poisson_package")->Param<bool>("fail_without_convergence");
  solver.SetFailWithMaxIterations(fail_flag);
  bool warn_flag =
      pmesh->packages.Get("poisson_package")->Param<bool>("warn_without_convergence");
  solver.SetWarnWithMaxIterations(warn_flag);

  return tc;
}

} // namespace poisson_example
