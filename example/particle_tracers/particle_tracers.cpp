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

#include "particle_tracers.hpp"
#include "Kokkos_CopyViews.hpp"
#include "Kokkos_HostSpace.hpp"
#include "basic_types.hpp"
#include "config.hpp"
#include "globals.hpp"
#include "interface/metadata.hpp"
#include "interface/update.hpp"
#include "kokkos_abstraction.hpp"
#include "parthenon/driver.hpp"
#include "refinement/refinement.hpp"
#include "utils/error_checking.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace parthenon::driver::prelude;
using namespace parthenon::Update;

typedef Kokkos::Random_XorShift64_Pool<> RNGPool;

namespace particle_tracers {

Packages_t ProcessPackages(std::unique_ptr<ParameterInput> &pin) {
  Packages_t packages;
  packages.Add(particle_tracers::Particles::Initialize(pin.get()));
  return packages;
}

namespace Particles {

// *************************************************//
// define the Particles package, including         *//
// initialization and update functions.            *//
// *************************************************//

std::shared_ptr<StateDescriptor> Initialize(ParameterInput *pin) {
  auto pkg = std::make_shared<StateDescriptor>("particles_package");

  Real vx = pin->GetOrAddReal("Background", "vx", 1.0);
  pkg->AddParam<>("vx", vx);
  Real vy = pin->GetOrAddReal("Background", "vy", 0.0);
  pkg->AddParam<>("vy", vy);
  Real vz = pin->GetOrAddReal("Background", "vz", 0.0);
  pkg->AddParam<>("vz", vz);

  Real cfl = pin->GetOrAddReal("Background", "cfl", 0.3);
  pkg->AddParam<>("cfl", cfl);

  int num_tracers = pin->GetOrAddReal("Tracers", "num_tracers", 100);
  pkg->AddParam<>("num_tracers", num_tracers);

  // Initialize random number generator pool
  int rng_seed = pin->GetOrAddInteger("Tracers", "rng_seed", 1273);
  pkg->AddParam<>("rng_seed", rng_seed);
  RNGPool rng_pool(rng_seed);
  pkg->AddParam<>("rng_pool", rng_pool);

  // Add swarm of tracer particles
  std::string swarm_name = "tracers";
  Metadata swarm_metadata;
  pkg->AddSwarm(swarm_name, swarm_metadata);
  Metadata real_swarmvalue_metadata({Metadata::Real});
  pkg->AddSwarmValue("id", swarm_name, Metadata({Metadata::Integer}));

  // Add advected field
  std::string field_name = "advected";
  Metadata mfield({Metadata::Cell, Metadata::Independent, Metadata::FillGhost});
  pkg->AddField(field_name, mfield);

  // Add field in which to deposit tracer densities
  field_name = "tracer_deposition";
  pkg->AddField(field_name, mfield);

  pkg->EstimateTimestepBlock = EstimateTimestepBlock;

  return pkg;
}

Real EstimateTimestepBlock(MeshBlockData<Real> *mbd) {
  auto pmb = mbd->GetBlockPointer();
  auto pkg = pmb->packages.Get("particles_package");
  const auto &cfl = pkg->Param<Real>("cfl");

  const auto &vx = pkg->Param<Real>("vx");
  const auto &vy = pkg->Param<Real>("vy");
  const auto &vz = pkg->Param<Real>("vz");

  // Assumes a grid with constant dx, dy, dz within a block
  const Real &dx_i = pmb->coords.dx1v(0);
  const Real &dx_j = pmb->coords.dx2v(0);
  const Real &dx_k = pmb->coords.dx3v(0);

  Real min_dt = dx_i / std::abs(vx + TINY_NUMBER);
  min_dt = std::min(min_dt, dx_j / std::abs(vy + TINY_NUMBER));
  min_dt = std::min(min_dt, dx_k / std::abs(vz + TINY_NUMBER));

  return cfl * min_dt;
}

TaskStatus AdvectTracers(MeshBlock *pmb, const StagedIntegrator *integrator) {
  auto swarm = pmb->swarm_data.Get()->Get("tracers");
  auto pkg = pmb->packages.Get("particles_package");

  int max_active_index = swarm->GetMaxActiveIndex();

  Real dt = integrator->dt;

  auto &x = swarm->Get<Real>("x").Get();
  auto &y = swarm->Get<Real>("y").Get();
  auto &z = swarm->Get<Real>("z").Get();

  const auto &vx = pkg->Param<Real>("vx");
  const auto &vy = pkg->Param<Real>("vy");
  const auto &vz = pkg->Param<Real>("vz");

  auto swarm_d = swarm->GetDeviceContext();
  pmb->par_for(
      "Tracer advection", 0, max_active_index, KOKKOS_LAMBDA(const int n) {
        if (swarm_d.IsActive(n)) {

          x(n) += vx * dt;
          y(n) += vy * dt;
          z(n) += vz * dt;

          bool on_current_mesh_block = true;
          swarm_d.GetNeighborBlockIndex(n, x(n), y(n), z(n), on_current_mesh_block);
        }
      });

  return TaskStatus::complete;
}

TaskStatus DepositTracers(MeshBlock *pmb) {
  auto swarm = pmb->swarm_data.Get()->Get("tracers");

  // Meshblock geometry
  const IndexRange &ib = pmb->cellbounds.GetBoundsI(IndexDomain::interior);
  const IndexRange &jb = pmb->cellbounds.GetBoundsJ(IndexDomain::interior);
  const IndexRange &kb = pmb->cellbounds.GetBoundsK(IndexDomain::interior);
  // again using scalar dx_D for assuming a uniform grid in this example
  const Real &dx_i = pmb->coords.dx1v(0);
  const Real &dx_j = pmb->coords.dx2f(0);
  const Real &dx_k = pmb->coords.dx3f(0);
  const Real &minx_i = pmb->coords.x1f(ib.s);
  const Real &minx_j = pmb->coords.x2f(jb.s);
  const Real &minx_k = pmb->coords.x3f(kb.s);

  const auto &x = swarm->Get<Real>("x").Get();
  const auto &y = swarm->Get<Real>("y").Get();
  const auto &z = swarm->Get<Real>("z").Get();
  auto swarm_d = swarm->GetDeviceContext();

  auto &tracer_dep = pmb->meshblock_data.Get()->Get("tracer_deposition").data;
  // Reset particle count
  pmb->par_for(
      "ZeroParticleDep", kb.s, kb.e, jb.s, jb.e, ib.s, ib.e,
      KOKKOS_LAMBDA(const int k, const int j, const int i) { tracer_dep(k, j, i) = 0.; });

  const int ndim = pmb->pmy_mesh->ndim;

  pmb->par_for(
      "DepositTracers", 0, swarm->GetMaxActiveIndex(), KOKKOS_LAMBDA(const int n) {
        if (swarm_d.IsActive(n)) {
          int i = static_cast<int>(std::floor((x(n) - minx_i) / dx_i) + ib.s);
          int j = 0;
          if (ndim > 1) {
            j = static_cast<int>(std::floor((y(n) - minx_j) / dx_j) + jb.s);
          }
          int k = 0;
          if (ndim > 2) {
            k = static_cast<int>(std::floor((z(n) - minx_k) / dx_k) + kb.s);
          }

          // For testing in this example we make sure the indices are correct
          if (i >= ib.s && i <= ib.e && j >= jb.s && j <= jb.e && k >= kb.s &&
              k <= kb.e) {
            Kokkos::atomic_add(&tracer_dep(k, j, i), 1.0);
          } else {
            PARTHENON_FAIL("Particle outside of active region during deposition.");
          }
        }
      });

  return TaskStatus::complete;
}

TaskStatus CalculateFluxes(MeshBlockData<Real> *mbd) {
  auto pmb = mbd->GetBlockPointer();
  auto pkg = pmb->packages.Get("particles_package");
  const auto &vx = pkg->Param<Real>("vx");
  const auto &vy = pkg->Param<Real>("vy");
  const auto &vz = pkg->Param<Real>("vz");

  const auto ndim = pmb->pmy_mesh->ndim;

  IndexRange ib = pmb->cellbounds.GetBoundsI(IndexDomain::interior);
  IndexRange jb = pmb->cellbounds.GetBoundsJ(IndexDomain::interior);
  IndexRange kb = pmb->cellbounds.GetBoundsK(IndexDomain::interior);

  auto advected = mbd->Get("advected").data;

  auto x1flux = mbd->Get("advected").flux[X1DIR].Get<4>();

  // Spatially first order upwind method
  pmb->par_for(
      "CalculateFluxesX1", kb.s, kb.e, jb.s, jb.e, ib.s, ib.e + 1,
      KOKKOS_LAMBDA(const int k, const int j, const int i) {
        // X1
        if (vx > 0.) {
          x1flux(0, k, j, i) = advected(k, j, i - 1) * vx;
        } else {
          x1flux(0, k, j, i) = advected(k, j, i) * vx;
        }
      });

  if (ndim > 1) {
    auto x2flux = mbd->Get("advected").flux[X2DIR].Get<4>();
    pmb->par_for(
        "CalculateFluxesX2", kb.s, kb.e, jb.s, jb.e + 1, ib.s, ib.e,
        KOKKOS_LAMBDA(const int k, const int j, const int i) {
          // X2
          if (vy > 0.) {
            x2flux(0, k, j, i) = advected(k, j - 1, i) * vy;
          } else {
            x2flux(0, k, j, i) = advected(k, j, i) * vy;
          }
        });
  }

  if (ndim > 2) {
    auto x3flux = mbd->Get("advected").flux[X3DIR].Get<4>();
    pmb->par_for(
        "CalculateFluxesX3", kb.s, kb.e + 1, jb.s, jb.e, ib.s, ib.e,
        KOKKOS_LAMBDA(const int k, const int j, const int i) {
          // X3
          if (vz > 0.) {
            x3flux(0, k, j, i) = advected(k - 1, j, i) * vz;
          } else {
            x3flux(0, k, j, i) = advected(k, j, i) * vz;
          }
        });
  }

  return TaskStatus::complete;
}

TaskStatus Defrag(MeshBlock *pmb) {
  auto s = pmb->swarm_data.Get()->Get("tracers");

  // Only do this if list is getting too sparse. This criterion (whether there
  // are *any* gaps in the list) is very aggressive
  if (s->GetNumActive() <= s->GetMaxActiveIndex()) {
    s->Defrag();
  }

  return TaskStatus::complete;
}

// Mark all MPI requests as NULL / initialize boundary flags.
TaskStatus InitializeCommunicationMesh(const BlockList_t &blocks) {
  // Boundary transfers on same MPI proc are blocking
  for (auto &block : blocks) {
    auto swarm = block->swarm_data.Get()->Get("tracers");
    for (int n = 0; n < block->pbval->nneighbor; n++) {
      NeighborBlock &nb = block->pbval->neighbor[n];
      swarm->vbswarm->bd_var_.req_send[nb.bufid] = MPI_REQUEST_NULL;
    }
  }

  for (auto &block : blocks) {
    auto &pmb = block;
    auto sc = pmb->swarm_data.Get();
    auto swarm = sc->Get("tracers");

    for (int n = 0; n < swarm->vbswarm->bd_var_.nbmax; n++) {
      auto &nb = pmb->pbval->neighbor[n];
      swarm->vbswarm->bd_var_.flag[nb.bufid] = BoundaryStatus::waiting;
    }
  }

  // Reset boundary statuses
  for (auto &block : blocks) {
    auto &pmb = block;
    auto sc = pmb->swarm_data.Get();
    auto swarm = sc->Get("tracers");
    for (int n = 0; n < swarm->vbswarm->bd_var_.nbmax; n++) {
      auto &nb = pmb->pbval->neighbor[n];
      swarm->vbswarm->bd_var_.flag[nb.bufid] = BoundaryStatus::waiting;
    }
  }

  return TaskStatus::complete;
}

} // namespace Particles

// *************************************************//
// define the application driver. in this case,    *//
// that just means defining the MakeTaskList       *//
// function.                                       *//
// *************************************************//

void ProblemGenerator(MeshBlock *pmb, ParameterInput *pin) {
  auto &pkg = pmb->packages.Get("particles_package");
  auto &mbd = pmb->meshblock_data.Get();
  auto &advected = mbd->Get("advected").data;
  auto &swarm = pmb->swarm_data.Get()->Get("tracers");
  const auto num_tracers = pkg->Param<int>("num_tracers");
  auto rng_pool = pkg->Param<RNGPool>("rng_pool");

  const IndexRange &ib = pmb->cellbounds.GetBoundsI(IndexDomain::interior);
  const IndexRange &jb = pmb->cellbounds.GetBoundsJ(IndexDomain::interior);
  const IndexRange &kb = pmb->cellbounds.GetBoundsK(IndexDomain::interior);
  auto coords = pmb->coords;

  const Real advected_mean = 1.0;
  const Real advected_amp = 0.5;
  PARTHENON_REQUIRE(advected_mean > advected_amp, "Cannot have negative densities!");

  pmb->par_for(
      "Init advected profile", kb.s, kb.e, jb.s, jb.e, ib.s, ib.e,
      KOKKOS_LAMBDA(const int k, const int j, const int i) {
        advected(k, j, i) = advected_mean + advected_amp * sin(2. * M_PI * coords.x1v(i));
      });

  const Real &x_min = pmb->coords.x1f(ib.s);
  const Real &y_min = pmb->coords.x2f(jb.s);
  const Real &z_min = pmb->coords.x3f(kb.s);
  const Real &x_max = pmb->coords.x1f(ib.e + 1);
  const Real &y_max = pmb->coords.x2f(jb.e + 1);
  const Real &z_max = pmb->coords.x3f(kb.e + 1);

  const auto mesh_size = pmb->pmy_mesh->mesh_size;
  const Real x_min_mesh = mesh_size.x1min;
  const Real y_min_mesh = mesh_size.x2min;
  const Real z_min_mesh = mesh_size.x3min;
  const Real x_max_mesh = mesh_size.x1max;
  const Real y_max_mesh = mesh_size.x2max;
  const Real z_max_mesh = mesh_size.x3max;

  // Calculate fraction of total tracer particles on this meshblock. Tracer number follows
  // number = advected*volume
  Real number_meshblock =
      advected_mean * (x_max - x_min) -
      advected_amp / (2. * M_PI) * (cos(2. * M_PI * x_max) - cos(2. * M_PI * x_min));
  number_meshblock *= (y_max - y_min) * (z_max - z_min);
  Real number_mesh = advected_mean * (x_max_mesh - x_min_mesh);
  number_mesh -= advected_amp / (2. * M_PI) *
                 (cos(2. * M_PI * x_max_mesh) - cos(2. * M_PI * x_min_mesh));
  number_mesh *= (y_max_mesh - y_min_mesh) * (z_max_mesh - z_min_mesh);

  int num_tracers_meshblock = std::round(num_tracers * number_meshblock / number_mesh);

  ParArrayND<int> new_indices;
  const auto new_particles_mask =
      swarm->AddEmptyParticles(num_tracers_meshblock, new_indices);

  auto &x = swarm->Get<Real>("x").Get();
  auto &y = swarm->Get<Real>("y").Get();
  auto &z = swarm->Get<Real>("z").Get();

  auto swarm_d = swarm->GetDeviceContext();
  // This hardcoded implementation should only used in PGEN and not during runtime
  // addition of particles as indices need to be taken into account.
  pmb->par_for(
      "CreateParticles", 0, num_tracers_meshblock - 1, KOKKOS_LAMBDA(const int n) {
        auto rng_gen = rng_pool.get_state();

        // Rejection sample the x position
        Real val;
        do {
          x(n) = x_min + rng_gen.drand() * (x_max - x_min);
          val = advected_mean + advected_amp * sin(2. * M_PI * x(n));
        } while (val < rng_gen.drand() * (advected_mean + advected_amp));

        y(n) = y_min + rng_gen.drand() * (y_max - y_min);
        z(n) = z_min + rng_gen.drand() * (z_max - z_min);

        rng_pool.free_state(rng_gen);
      });
}

TaskCollection ParticleDriver::MakeTaskCollection(BlockList_t &blocks, int stage) {
  TaskCollection tc;
  TaskID none(0);

  const Real beta = integrator->beta[stage - 1];
  const Real dt = integrator->dt;
  const auto &stage_name = integrator->stage_name;
  const int nstages = integrator->nstages;

  const auto nblocks = blocks.size();
  TaskRegion &async_region0 = tc.AddRegion(nblocks);

  // Staged advection update of advected field

  for (int n = 0; n < nblocks; n++) {
    auto &pmb = blocks[n];
    auto &tl = async_region0[n];

    auto &base = pmb->meshblock_data.Get();
    if (stage == 1) {
      pmb->meshblock_data.Add("dUdt", base);
      for (int m = 1; m < nstages; m++) {
        pmb->meshblock_data.Add(stage_name[m], base);
      }
    }

    auto &sc0 = pmb->meshblock_data.Get(stage_name[stage - 1]);
    auto &dudt = pmb->meshblock_data.Get("dUdt");
    auto &sc1 = pmb->meshblock_data.Get(stage_name[stage]);

    auto start_recv = tl.AddTask(none, &MeshBlockData<Real>::StartReceiving, sc1.get(),
                                 BoundaryCommSubset::all);

    auto advect_flux = tl.AddTask(none, Particles::CalculateFluxes, sc0.get());

    auto send_flux =
        tl.AddTask(advect_flux, &MeshBlockData<Real>::SendFluxCorrection, sc0.get());

    auto recv_flux =
        tl.AddTask(advect_flux, &MeshBlockData<Real>::ReceiveFluxCorrection, sc0.get());

    auto flux_div =
        tl.AddTask(recv_flux, FluxDivergence<MeshBlockData<Real>>, sc0.get(), dudt.get());

    auto avg_data = tl.AddTask(flux_div, AverageIndependentData<MeshBlockData<Real>>,
                               sc0.get(), base.get(), beta);

    auto update = tl.AddTask(avg_data, UpdateIndependentData<MeshBlockData<Real>>,
                             sc0.get(), dudt.get(), beta * dt, sc1.get());

    auto send = tl.AddTask(update, &MeshBlockData<Real>::SendBoundaryBuffers, sc1.get());

    auto recv = tl.AddTask(send, &MeshBlockData<Real>::ReceiveBoundaryBuffers, sc1.get());

    auto fill_from_bufs =
        tl.AddTask(recv, &MeshBlockData<Real>::SetBoundaries, sc1.get());

    auto clear_comm_flags =
        tl.AddTask(fill_from_bufs, &MeshBlockData<Real>::ClearBoundary, sc1.get(),
                   BoundaryCommSubset::all);

    auto prolongBound = tl.AddTask(fill_from_bufs, parthenon::ProlongateBoundaries, sc1);

    auto set_bc = tl.AddTask(prolongBound, parthenon::ApplyBoundaryConditions, sc1);

    if (stage == integrator->nstages) {
      auto new_dt = tl.AddTask(
          set_bc, parthenon::Update::EstimateTimestep<MeshBlockData<Real>>, sc1.get());
    }
  }

  // First-order operator split tracer particle update

  if (stage == integrator->nstages) {
    TaskRegion &sync_region0 = tc.AddRegion(1);
    {
      auto &tl = sync_region0[0];
      auto initialize_comms =
          tl.AddTask(none, Particles::InitializeCommunicationMesh, blocks);
    }

    TaskRegion &async_region1 = tc.AddRegion(nblocks);
    for (int n = 0; n < nblocks; n++) {
      auto &tl = async_region1[n];
      auto &pmb = blocks[n];
      auto &sc = pmb->swarm_data.Get();
      auto tracerAdvect =
          tl.AddTask(none, Particles::AdvectTracers, pmb.get(), integrator.get());

      auto send = tl.AddTask(tracerAdvect, &SwarmContainer::Send, sc.get(),
                             BoundaryCommSubset::all);

      auto receive =
          tl.AddTask(send, &SwarmContainer::Receive, sc.get(), BoundaryCommSubset::all);

      auto deposit = tl.AddTask(receive, Particles::DepositTracers, pmb.get());

      auto defrag = tl.AddTask(deposit, Particles::Defrag, pmb.get());
    }
  }

  return tc;
}

} // namespace particle_tracers
