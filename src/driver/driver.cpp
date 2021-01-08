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

#include <algorithm>
#include <iomanip>
#include <limits>

#include "driver/driver.hpp"

#include "interface/update.hpp"
#include "mesh/mesh.hpp"
#include "mesh/meshblock.hpp"
#include "outputs/outputs.hpp"
#include "parameter_input.hpp"
#include "parthenon_mpi.hpp"
#include "utils/utils.hpp"

namespace parthenon {

void Driver::PreExecute() {
  if (Globals::my_rank == 0) {
    std::cout << std::endl << "Setup complete, executing driver...\n" << std::endl;
  }

  timer_main.reset();
}

void Driver::PostExecute() {
  if (Globals::my_rank == 0) {
    SignalHandler::CancelWallTimeAlarm();
    // Calculate and print the zone-cycles/cpu-second and wall-second
    std::uint64_t zonecycles =
        pmesh->mbcnt * static_cast<std::uint64_t>(pmesh->GetNumberOfMeshBlockCells());

    auto wtime = timer_main.seconds();
    std::cout << std::endl << "walltime used = " << wtime << std::endl;
    std::cout << "zone-cycles/wallsecond = " << static_cast<double>(zonecycles) / wtime
              << std::endl;
  }
}

DriverStatus EvolutionDriver::Execute() {
  PreExecute();
  InitializeBlockTimeSteps();
  SetGlobalTimeStep();
  pouts->MakeOutputs(pmesh, pinput, &tm);
  pmesh->mbcnt = 0;
  int perf_cycle_offset =
      pinput->GetOrAddInteger("parthenon/time", "perf_cycle_offset", 0);
  Kokkos::Profiling::pushRegion("Driver_Main");
  while (tm.KeepGoing()) {
    if (Globals::my_rank == 0) OutputCycleDiagnostics();

    pmesh->PreStepUserWorkInLoop(pmesh, pinput, tm);
    pmesh->PreStepUserDiagnosticsInLoop(pmesh, pinput, tm);

    TaskListStatus status = Step();
    if (status != TaskListStatus::complete) {
      std::cerr << "Step failed to complete all tasks." << std::endl;
      return DriverStatus::failed;
    }

    pmesh->PostStepUserWorkInLoop(pmesh, pinput, tm);
    pmesh->PostStepUserDiagnosticsInLoop(pmesh, pinput, tm);

    tm.ncycle++;
    tm.time += tm.dt;
    pmesh->mbcnt += pmesh->nbtotal;
    pmesh->step_since_lb++;

    pmesh->LoadBalancingAndAdaptiveMeshRefinement(pinput, app_input);
    if (pmesh->modified) InitializeBlockTimeSteps();
    SetGlobalTimeStep();
    if (tm.time < tm.tlim) // skip the final output as it happens later
      pouts->MakeOutputs(pmesh, pinput, &tm);

    // check for signals
    if (SignalHandler::CheckSignalFlags() != 0) {
      return DriverStatus::failed;
    }
    if (tm.ncycle == perf_cycle_offset) {
      pmesh->mbcnt = 0;
      timer_main.reset();
    }
  } // END OF MAIN INTEGRATION LOOP ======================================================
  Kokkos::Profiling::popRegion(); // Driver_Main

  pmesh->UserWorkAfterLoop(pmesh, pinput, tm);

  DriverStatus status = DriverStatus::complete;

  pouts->MakeOutputs(pmesh, pinput, &tm);
  PostExecute(status);
  return status;
}

void EvolutionDriver::PostExecute(DriverStatus status) {
  // Print diagnostic messages related to the end of the simulation
  if (Globals::my_rank == 0) {
    OutputCycleDiagnostics();
    SignalHandler::Report();
    if (status == DriverStatus::complete) {
      std::cout << std::endl << "Driver completed." << std::endl;
    } else if (status == DriverStatus::timeout) {
      std::cout << std::endl << "Driver timed out.  Restart to continue." << std::endl;
    } else if (status == DriverStatus::failed) {
      std::cout << std::endl << "Driver failed." << std::endl;
    }

    std::cout << "time=" << tm.time << " cycle=" << tm.ncycle << std::endl;
    std::cout << "tlim=" << tm.tlim << " nlim=" << tm.nlim << std::endl;

    if (pmesh->adaptive) {
      std::cout << std::endl
                << "Number of MeshBlocks = " << pmesh->nbtotal << "; " << pmesh->nbnew
                << "  created, " << pmesh->nbdel << " destroyed during this simulation."
                << std::endl;
    }
  }
  Driver::PostExecute();
}

void EvolutionDriver::InitializeBlockTimeSteps() {
  // calculate the first time step using Block function
  for (auto &pmb : pmesh->block_list) {
    Update::EstimateTimestep(pmb->meshblock_data.Get().get());
  }
  // calculate the first time step using Mesh function
  const int num_partitions = pmesh->DefaultNumPartitions();
  for (int i = 0; i < num_partitions; i++) {
    auto &mbase = pmesh->mesh_data.GetOrAdd("base", i);
    Update::EstimateTimestep(mbase.get());
  }
}

//----------------------------------------------------------------------------------------
// \!fn void EvolutionDriver::SetGlobalTimeStep()
// \brief function that loops over all MeshBlocks and find new timestep

void EvolutionDriver::SetGlobalTimeStep() {
  // don't allow dt to grow by more than 2x
  // consider making this configurable in the input
  tm.dt *= 2.0;
  Real big = std::numeric_limits<Real>::max();
  for (auto const &pmb : pmesh->block_list) {
    tm.dt = std::min(tm.dt, pmb->NewDt());
    pmb->SetAllowedDt(big);
  }

#ifdef MPI_PARALLEL
  PARTHENON_MPI_CHECK(MPI_Allreduce(MPI_IN_PLACE, &tm.dt, 1, MPI_PARTHENON_REAL, MPI_MIN,
                                    MPI_COMM_WORLD));
#endif

  if (tm.time < tm.tlim &&
      (tm.tlim - tm.time) < tm.dt) // timestep would take us past desired endpoint
    tm.dt = tm.tlim - tm.time;
}

void EvolutionDriver::OutputCycleDiagnostics() {
  const int dt_precision = std::numeric_limits<Real>::max_digits10 - 1;
  if (tm.ncycle_out != 0) {
    if (tm.ncycle % tm.ncycle_out == 0) {
      if (Globals::my_rank == 0) {
        std::uint64_t zonecycles =
            (pmesh->mbcnt - mbcnt_prev) *
            static_cast<std::uint64_t>(pmesh->GetNumberOfMeshBlockCells());
        std::cout << "cycle=" << tm.ncycle << std::scientific
                  << std::setprecision(dt_precision) << " time=" << tm.time
                  << " dt=" << tm.dt << std::setprecision(2) << " zone-cycles/wsec = "
                  << static_cast<double>(zonecycles) / timer_cycle.seconds();
        // insert more diagnostics here
        std::cout << std::endl;

        // reset cycle related counters
        timer_cycle.reset();
        // need to cache number of MeshBlocks as AMR/load balance change it
        mbcnt_prev = pmesh->mbcnt;
      }
    }
  }
}

} // namespace parthenon
