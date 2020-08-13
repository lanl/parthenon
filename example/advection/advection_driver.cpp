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

#include <memory>
#include <string>
#include <vector>

// Local Includes
#include "advection_driver.hpp"
#include "advection_package.hpp"

using namespace parthenon::driver::prelude;

namespace advection_example {

// *************************************************//
// define the application driver. in this case,    *//
// that mostly means defining the MakeTaskList     *//
// function.                                       *//
// *************************************************//
AdvectionDriver::AdvectionDriver(ParameterInput *pin, ApplicationInput *app_in, Mesh *pm)
    : MultiStageBlockTaskDriver(pin, app_in, pm) {
  // fail if these are not specified in the input file
  pin->CheckRequired("parthenon/mesh", "ix1_bc");
  pin->CheckRequired("parthenon/mesh", "ox1_bc");
  pin->CheckRequired("parthenon/mesh", "ix2_bc");
  pin->CheckRequired("parthenon/mesh", "ox2_bc");

  // warn if these fields aren't specified in the input file
  pin->CheckDesired("parthenon/mesh", "refinement");
  pin->CheckDesired("parthenon/mesh", "numlevel");
  pin->CheckDesired("Advection", "cfl");
  pin->CheckDesired("Advection", "vx");
  pin->CheckDesired("Advection", "refine_tol");
  pin->CheckDesired("Advection", "derefine_tol");
}
// first some helper tasks
TaskStatus UpdateContainer(MeshBlock *pmb, int stage,
                           std::vector<std::string> &stage_name, Integrator *integrator) {
  // const Real beta = stage_wghts[stage-1].beta;
  const Real beta = integrator->beta[stage - 1];
  const Real dt = integrator->dt;
  auto &base = pmb->real_containers.Get();
  auto &cin = pmb->real_containers.Get(stage_name[stage - 1]);
  auto &cout = pmb->real_containers.Get(stage_name[stage]);
  auto &dudt = pmb->real_containers.Get("dUdt");
  parthenon::Update::AverageContainers(cin, base, beta);
  parthenon::Update::UpdateContainer(cin, dudt, beta * dt, cout);
  return TaskStatus::complete;
}

// See the advection.hpp declaration for a description of how this function gets called.
TaskList AdvectionDriver::MakeTaskList(MeshBlock *pmb, int stage) {
  TaskList tl;

  TaskID none(0);
  // first make other useful containers
  if (stage == 1) {
    auto &base = pmb->real_containers.Get();
    pmb->real_containers.Add("dUdt", base);
    for (int i = 1; i < integrator->nstages; i++)
      pmb->real_containers.Add(stage_name[i], base);
  }

  // pull out the container we'll use to get fluxes and/or compute RHSs
  auto &sc0 = pmb->real_containers.Get(stage_name[stage - 1]);
  // pull out a container we'll use to store dU/dt.
  // This is just -flux_divergence in this example
  auto &dudt = pmb->real_containers.Get("dUdt");
  // pull out the container that will hold the updated state
  // effectively, sc1 = sc0 + dudt*dt
  auto &sc1 = pmb->real_containers.Get(stage_name[stage]);

  auto start_recv = tl.AddTask(&Container<Real>::StartReceiving, sc1.get(), none,
                               BoundaryCommSubset::all);

  auto advect_flux = tl.AddTask(advection_package::CalculateFluxes, none, sc0);

  auto send_flux =
      tl.AddTask(&Container<Real>::SendFluxCorrection, sc0.get(), advect_flux);
  auto recv_flux =
      tl.AddTask(&Container<Real>::ReceiveFluxCorrection, sc0.get(), advect_flux);

  // compute the divergence of fluxes of conserved variables
  auto flux_div = tl.AddTask(parthenon::Update::FluxDivergence, recv_flux, sc0, dudt);

  // apply du/dt to all independent fields in the container
  auto update_container =
      tl.AddTask(UpdateContainer, flux_div, pmb, stage, stage_name, integrator);

  // update ghost cells
  auto send =
      tl.AddTask(&Container<Real>::SendBoundaryBuffers, sc1.get(), update_container);
  auto recv = tl.AddTask(&Container<Real>::ReceiveBoundaryBuffers, sc1.get(), send);
  auto fill_from_bufs = tl.AddTask(&Container<Real>::SetBoundaries, sc1.get(), recv);
  auto clear_comm_flags = tl.AddTask(&Container<Real>::ClearBoundary, sc1.get(),
                                     fill_from_bufs, BoundaryCommSubset::all);

  auto prolongBound = tl.AddTask(
      [](MeshBlock *pmb) {
        pmb->pbval->ProlongateBoundaries(0.0, 0.0);
        return TaskStatus::complete;
      },
      fill_from_bufs, pmb);

  // set physical boundaries
  auto set_bc = tl.AddTask(parthenon::ApplyBoundaryConditions, prolongBound, sc1);

  // fill in derived fields
  auto fill_derived =
      tl.AddTask(parthenon::FillDerivedVariables::FillDerived, set_bc, sc1);

  // estimate next time step
  if (stage == integrator->nstages) {
    auto new_dt = tl.AddTask(
        [](std::shared_ptr<Container<Real>> &rc) {
          MeshBlock *pmb = rc->pmy_block;
          pmb->SetBlockTimestep(parthenon::Update::EstimateTimestep(rc));
          return TaskStatus::complete;
        },
        fill_derived, sc1);

    // Update refinement
    if (pmesh->adaptive) {
      auto tag_refine = tl.AddTask(
          [](MeshBlock *pmb) {
            pmb->pmr->CheckRefinementCondition();
            return TaskStatus::complete;
          },
          fill_derived, pmb);
    }
  }
  return tl;
}

} // namespace advection_example
