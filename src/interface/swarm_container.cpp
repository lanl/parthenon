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
#include <cstdlib>
#include <memory>
#include <utility>
#include <vector>

#include "bvals/cc/bvals_cc.hpp"
#include "globals.hpp" // my_rank
#include "mesh/mesh.hpp"
#include "swarm_container.hpp"

namespace parthenon {

void SwarmContainer::Add(const std::vector<std::string> &labelArray,
                         const Metadata &metadata) {
  // generate the vector and call Add
  for (const auto &label : labelArray) {
    Add(label, metadata);
  }
}

///
/// The internal routine for allocating a particle swarm.  This subroutine
/// is topology aware and will allocate accordingly.
///
/// @param label the name of the variable
/// @param metadata the metadata associated with the particle
void SwarmContainer::Add(const std::string &label, const Metadata &metadata) {
  if (swarmMap_.find(label) != swarmMap_.end()) {
    throw std::invalid_argument("swarm " + label + " already enrolled during Add()!");
  }

  auto swarm = std::make_shared<Swarm>(label, metadata);
  swarm->SetBlockPointer(GetBlockPointer());
  swarmVector_.push_back(swarm);
  swarmMap_[label] = swarm;
}

void SwarmContainer::Remove(const std::string label) {
  // Find index of swarm
  int isize = swarmVector_.size();
  int idx = 0;
  for (const auto &s : swarmVector_) {
    if (!label.compare(s->label())) {
      break;
    }
    idx++;
  }
  if (idx >= isize) {
    throw std::invalid_argument("swarm not found in Remove()");
  }

  // first delete the variable
  swarmVector_[idx].reset();

  // Next move the last element into idx and pop last entry
  isize--;
  if (isize >= 0) swarmVector_[idx] = std::move(swarmVector_.back());
  swarmVector_.pop_back();

  // Also remove swarm from map
  swarmMap_.erase(label);
}

void SwarmContainer::SendBoundaryBuffers() {}

void SwarmContainer::SetupPersistentMPI() {}

bool SwarmContainer::ReceiveBoundaryBuffers() { return true; }

void SwarmContainer::ReceiveAndSetBoundariesWithWait() {}

void SwarmContainer::SetBoundaries() {}

void SwarmContainer::StartReceiving(BoundaryCommSubset phase) {}

void SwarmContainer::ClearBoundary(BoundaryCommSubset phase) {}

void SwarmContainer::Print() const {
  std::cout << "Swarms are:\n";
  for (const auto &s : swarmMap_) {
    std::cout << "  " << s.second->info() << std::endl;
  }
}

} // namespace parthenon
