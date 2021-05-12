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

#include "interface/meshblock_data.hpp"

#include <cstdlib>
#include <memory>
#include <sstream>
#include <utility>
#include <vector>

#include "bvals/cc/bvals_cc.hpp"
#include "mesh/mesh.hpp"
#include "mesh/meshblock.hpp"

namespace parthenon {

template <typename T>
void MeshBlockData<T>::Add(const std::vector<std::string> &labelArray,
                           const Metadata &metadata) {
  // generate the vector and call Add
  for (const auto &label : labelArray) {
    Add(label, metadata);
  }
}

///
/// The internal routine for allocating an array.  This subroutine
/// is topology aware and will allocate accordingly.
///
/// @param label the name of the variable
/// @param metadata the metadata associated with the variable
template <typename T>
void MeshBlockData<T>::Add(const std::string &label, const Metadata &metadata) {
  auto arrDims = calcArrDims_(metadata);

  // branch on kind of variable
  if (metadata.IsSet(Metadata::Sparse)) {
    // add a sparse variable
    // Sparse ids are allocated later as needed
    if (sparseMap_.find(label) == sparseMap_.end()) {
      auto sv = std::make_shared<SparseVariable<T>>(label, metadata, arrDims);
      printf("adding sparse variable with label: %s\n", label.c_str());
      Add(sv);
    }
  } else if (metadata.Where() == Metadata::Edge) {
    // add an edge variable
    std::cerr << "Accessing unliving edge array in stage" << std::endl;
    std::exit(1);
    // s->_edgeVector.push_back(
    //     new EdgeVariable(label, metadata,
    //                      pmy_block->ncells3, pmy_block->ncells2, pmy_block->ncells1));
  } else if (metadata.Where() == Metadata::Face) {
    if (!(metadata.IsSet(Metadata::OneCopy))) {
      std::cerr << "Currently one one-copy face fields are supported" << std::endl;
      std::exit(1);
    }
    if (metadata.IsSet(Metadata::FillGhost)) {
      std::cerr << "Ghost zones not yet supported for face fields" << std::endl;
      std::exit(1);
    }
    // add a face variable
    auto pfv = std::make_shared<FaceVariable<T>>(label, arrDims, metadata);
    Add(pfv);
  } else {
    auto sv = std::make_shared<CellVariable<T>>(label, arrDims, metadata);
    Add(sv);
    if (metadata.IsSet(Metadata::FillGhost)) {
      sv->allocateComms(pmy_block);
    }
  }
}

// Constructor for getting sub-containers
// the variables returned are all shallow copies of the src container.
// Optionally extract only some of the sparse ids of src variable.
template <typename T>
MeshBlockData<T>::MeshBlockData(const MeshBlockData<T> &src,
                                const std::vector<std::string> &names,
                                const std::vector<int> &sparse_ids) {
  auto var_map = src.GetCellVariableMap();
  auto sparse_map = src.GetSparseMap();
  auto face_map = src.GetFaceMap();
  for (std::string name : names) {
    bool found = false;
    auto v = var_map.find(name);
    if (v != var_map.end()) {
      Add(v->second);
      found = true;
    }
    auto sv = sparse_map.find(name);
    if (sv != sparse_map.end()) {
      if (found) {
        std::stringstream msg;
        msg << "MeshBlockData: " << name << " found more than once!" << std::endl;
        PARTHENON_THROW(msg);
      }
      found = true;
      std::shared_ptr<SparseVariable<T>> newvar;
      if (sparse_ids.size() > 0) {
        newvar = std::make_shared<SparseVariable<T>>(sv->second, sparse_ids);
      } else {
        newvar = sv->second;
      }
      Add(newvar);
    }
    auto fv = face_map.find(name);
    if (fv != face_map.end()) {
      if (found) {
        std::stringstream msg;
        msg << "MeshBlockData: " << name << " found more than once!" << std::endl;
        PARTHENON_THROW(msg);
      }
      found = true;
      Add(fv->second);
    }
    if (!found) {
      std::stringstream msg;
      msg << "MeshBlockData: " << name << " found more than once!" << std::endl;
      PARTHENON_THROW(msg);
    }
  }
}

template <typename T>
MeshBlockData<T>::MeshBlockData(const MeshBlockData<T> &src,
                                const std::vector<MetadataFlag> &flags) {
  auto var_map = src.GetCellVariableMap();
  auto sparse_map = src.GetSparseMap();
  auto face_map = src.GetFaceMap();
  for (auto &it : var_map) {
    auto n = it.first;
    auto v = it.second;
    if (v->metadata().AnyFlagsSet(flags)) {
      Add(v);
    }
  }
  for (auto &it : sparse_map) {
    auto n = it.first;
    auto v = it.second;
    if (v->metadata().AnyFlagsSet(flags)) {
      Add(v);
    }
  }
  for (auto &it : face_map) {
    auto n = it.first;
    auto v = it.second;
    if (v->metadata().AnyFlagsSet(flags)) {
      Add(v);
    }
  }
}

// provides a container that has a single sparse slice
template <typename T>
std::shared_ptr<MeshBlockData<T>> MeshBlockData<T>::SparseSlice(int id) {
  auto c = std::make_shared<MeshBlockData<T>>();

  // copy in private data
  c->pmy_block = pmy_block;

  // Note that all standard arrays get added
  // add standard arrays
  for (auto v : varVector_) {
    c->Add(v);
  }
  // for (auto v : s->_edgeVector) {
  //   EdgeVariable *vNew = new EdgeVariable(v->label(), *v);
  //   c.s->_edgeVector.push_back(vNew);
  // }
  for (auto v : faceVector_) {
    c->Add(v);
  }

  // Now copy in the specific arrays
  for (auto v : sparseVector_) {
    int index = v->GetIndex(id);
    if (index >= 0) {
      auto &vmat = v->Get(id);
      c->Add(vmat);
    }
  }

  return c;
}

/// Queries related to variable packs
/// TODO(JMM): Make sure this is thread-safe
/// TODO(JMM): Should the vector of names be sorted to enforce uniqueness?
/// This is a helper function that queries the cache for the given pack.
/// The strings are the keys and the lists are the values.
/// Inputs:
/// var_names = vector of names of variables to pack
/// flx_names = vector of names of flux variables to pack
/// vars = forward list of shared pointers of vars to pack
/// fvars = forward list of shared pointers of fluxes to pack
/// Outputs:
/// vmap = std::map from names to std::pairs of indices
///        indices are the locations in the outer Kokkos::view of the pack
///        indices represent inclusive bounds for, e.g., a sparse or tensor-valued
///        variable.
template <typename T>
PackVariablesAndFluxesResult<T>
MeshBlockData<T>::PackListedVariablesAndFluxes(VariableListResult &&variables,
                                               VariableListResult &&fluxes) {
  PackVariablesAndFluxesResult<T> result;
  result.key = std::make_pair(std::move(variables.expanded_names),
                              std::move(fluxes.expanded_names));
  auto kvpair = varFluxPackMap_.find(result.key);
  if (kvpair == varFluxPackMap_.end()) {
    result.pack = MakeFluxPack(variables.vars, fluxes.vars, &result.vmap);
    FluxPackIndxPair<T> value;
    value.pack = result.pack;
    value.map = result.vmap;
    varFluxPackMap_[result.key] = value;
    return result;
  }

  result.vmap = kvpair->second.map;
  result.pack = kvpair->second.pack;
  return result;
}

template <typename T>
VariableFluxPack<T> MeshBlockData<T>::PackVariablesAndFluxes(
    const std::vector<std::string> &var_names, const std::vector<std::string> &flx_names,
    PackIndexMap &vmap, vpack_types::StringPair &key) {
  // expanded names expands sparse variables to varname_idx, where idx is the sparse index
  // this is required since not all sparse indices of a variable are necessarily
  // included in a pack.
  auto result = PackListedVariablesAndFluxes(GetVariablesByName(var_names),
                                             GetVariablesByName(flx_names));
  key = std::move(result.key);
  vmap = std::move(result.vmap);
  return std::move(result.pack);
}

/* Names interfaces */
template <typename T>
VariableFluxPack<T>
MeshBlockData<T>::PackVariablesAndFluxes(const std::vector<std::string> &var_names,
                                         const std::vector<std::string> &flx_names,
                                         PackIndexMap &vmap) {
  vpack_types::StringPair key;
  return PackVariablesAndFluxes(var_names, flx_names, vmap, key);
}
template <typename T>
VariableFluxPack<T>
MeshBlockData<T>::PackVariablesAndFluxes(const std::vector<std::string> &var_names,
                                         const std::vector<std::string> &flx_names,
                                         vpack_types::StringPair &key) {
  PackIndexMap vmap;
  return PackVariablesAndFluxes(var_names, flx_names, vmap, key);
}
template <typename T>
VariableFluxPack<T>
MeshBlockData<T>::PackVariablesAndFluxes(const std::vector<std::string> &var_names,
                                         const std::vector<std::string> &flx_names) {
  PackIndexMap vmap;
  vpack_types::StringPair key;
  return PackVariablesAndFluxes(var_names, flx_names, vmap, key);
}

/* Metadata interfaces */
template <typename T>
VariableFluxPack<T>
MeshBlockData<T>::PackVariablesAndFluxes(const std::vector<MetadataFlag> &flags,
                                         PackIndexMap &vmap,
                                         vpack_types::StringPair &key) {
  auto variables = GetVariablesByFlag(flags);
  auto result = PackListedVariablesAndFluxes(VariableListResult(variables),
                                             VariableListResult(variables));

  vmap = std::move(result.vmap);
  key = std::move(result.key);
  return std::move(result.pack);
}
template <typename T>
VariableFluxPack<T>
MeshBlockData<T>::PackVariablesAndFluxes(const std::vector<MetadataFlag> &flags,
                                         PackIndexMap &vmap) {
  vpack_types::StringPair key;
  return PackVariablesAndFluxes(flags, vmap, key);
}
template <typename T>
VariableFluxPack<T>
MeshBlockData<T>::PackVariablesAndFluxes(const std::vector<MetadataFlag> &flags,
                                         vpack_types::StringPair &key) {
  PackIndexMap vmap;
  return PackVariablesAndFluxes(flags, vmap, key);
}
template <typename T>
VariableFluxPack<T>
MeshBlockData<T>::PackVariablesAndFluxes(const std::vector<MetadataFlag> &flags) {
  PackIndexMap vmap;
  vpack_types::StringPair key;
  return PackVariablesAndFluxes(flags, vmap, key);
}

/// This is a helper function that queries the cache for the given pack.
/// The strings are the keys and the lists are the values.
/// Inputs:
/// var_names = vector of names of variables to pack
/// vars = forward list of shared pointers of vars to pack
/// Outputs:
/// vmap = std::map from names to std::pairs of indices
///        indices are the locations in the outer Kokkos::view of the pack
///        indices represent inclusive bounds for, e.g., a sparse or tensor-valued
///        variable.
template <typename T>
PackVariablesResult<T>
MeshBlockData<T>::PackListedVariables(VariableListResult &&variables, const bool coarse) {
  PackVariablesResult<T> result;
  result.key = std::move(variables.expanded_names);

  auto &packmap = coarse ? coarseVarPackMap_ : varPackMap_;
  auto kvpair = packmap.find(result.key);
  if (kvpair == packmap.end()) {
    result.pack = MakePack<T>(std::move(variables.vars), &result.vmap, coarse);
    PackIndxPair<T> value;
    value.pack = result.pack;
    value.map = result.vmap;
    packmap[result.key] = value;
    return result;
  }

  result.vmap = (kvpair->second).map;
  result.pack = (kvpair->second).pack;
  return result;
}

/***********************************/
/* Names and sparse ids interfaces */
/***********************************/
template <typename T>
VariablePack<T>
MeshBlockData<T>::PackVariables(const std::vector<std::string> &names,
                                const std::vector<int> &sparse_ids, PackIndexMap &vmap,
                                std::vector<std::string> &key, const bool coarse) {
  auto pack_result = PackListedVariables(GetVariablesByName(names, sparse_ids), coarse);
  key = std::move(pack_result.key);
  vmap = std::move(pack_result.vmap);
  return std::move(pack_result.pack);
}
template <typename T>
VariablePack<T> MeshBlockData<T>::PackVariables(const std::vector<std::string> &names,
                                                const std::vector<int> &sparse_ids,
                                                PackIndexMap &vmap, const bool coarse) {
  std::vector<std::string> key;
  return PackVariables(names, sparse_ids, vmap, key, coarse);
}
template <typename T>
VariablePack<T> MeshBlockData<T>::PackVariables(const std::vector<std::string> &names,
                                                const std::vector<int> &sparse_ids,
                                                std::vector<std::string> &key,
                                                const bool coarse) {
  PackIndexMap vmap;
  return PackVariables(names, sparse_ids, vmap, key, coarse);
}
template <typename T>
VariablePack<T> MeshBlockData<T>::PackVariables(const std::vector<std::string> &names,
                                                const std::vector<int> &sparse_ids,
                                                const bool coarse) {
  PackIndexMap vmap;
  std::vector<std::string> key;
  return PackVariables(names, sparse_ids, vmap, key, coarse);
}

/********************/
/* Names interfaces */
/********************/
template <typename T>
VariablePack<T>
MeshBlockData<T>::PackVariables(const std::vector<std::string> &names, PackIndexMap &vmap,
                                std::vector<std::string> &key, const bool coarse) {
  return PackVariables(names, {}, vmap, key, coarse);
}
template <typename T>
VariablePack<T> MeshBlockData<T>::PackVariables(const std::vector<std::string> &names,
                                                PackIndexMap &vmap, const bool coarse) {
  std::vector<std::string> key;
  return PackVariables(names, {}, vmap, key, coarse);
}
template <typename T>
VariablePack<T> MeshBlockData<T>::PackVariables(const std::vector<std::string> &names,
                                                std::vector<std::string> &key,
                                                const bool coarse) {
  PackIndexMap vmap;
  return PackVariables(names, {}, vmap, key, coarse);
}
template <typename T>
VariablePack<T> MeshBlockData<T>::PackVariables(const std::vector<std::string> &names,
                                                const bool coarse) {
  PackIndexMap vmap;
  std::vector<std::string> key;
  return PackVariables(names, {}, vmap, key, coarse);
}

/*****************************/
/* Metadata flags interfaces */
/*****************************/
template <typename T>
VariablePack<T> MeshBlockData<T>::PackVariables(const std::vector<MetadataFlag> &flags,
                                                PackIndexMap &vmap,
                                                std::vector<std::string> &key,
                                                const bool coarse) {
  auto result = PackListedVariables(GetVariablesByFlag(flags), coarse);
  vmap = std::move(result.vmap);
  key = std::move(result.key);
  return std::move(result.pack);
}
template <typename T>
VariablePack<T> MeshBlockData<T>::PackVariables(const std::vector<MetadataFlag> &flags,
                                                PackIndexMap &vmap, const bool coarse) {
  std::vector<std::string> key;
  return PackVariables(flags, vmap, key, coarse);
}
template <typename T>
VariablePack<T> MeshBlockData<T>::PackVariables(const std::vector<MetadataFlag> &flags,
                                                std::vector<std::string> &key,
                                                const bool coarse) {
  PackIndexMap vmap;
  return PackVariables(flags, vmap, key, coarse);
}
template <typename T>
VariablePack<T> MeshBlockData<T>::PackVariables(const std::vector<MetadataFlag> &flags,
                                                const bool coarse) {
  PackIndexMap vmap;
  std::vector<std::string> key;
  return PackVariables(flags, vmap, key, coarse);
}

/*********************************/
/* Include everything interfaces */
/*********************************/
template <typename T>
VariablePack<T> MeshBlockData<T>::PackVariables(PackIndexMap &vmap,
                                                std::vector<std::string> &key,
                                                const bool coarse) {
  auto result = PackListedVariables(GetAllVariables(), coarse);
  vmap = std::move(result.vmap);
  key = std::move(result.key);
  return std::move(result.pack);
}
template <typename T>
VariablePack<T> MeshBlockData<T>::PackVariables(PackIndexMap &vmap, const bool coarse) {
  std::vector<std::string> key;
  return PackVariables(vmap, key, coarse);
}
template <typename T>
VariablePack<T> MeshBlockData<T>::PackVariables() {
  PackIndexMap vmap;
  std::vector<std::string> key;
  return PackVariables(vmap, key, false);
}

// From a given container, extract all variables and all fields in sparse variables
// into a single linked list of variables. The sparse fields are then named
// variable_index.
// The names of the non-sparse variables and the sparse fields are then
// packed into the std::vector "expanded_names," which is used as the key for
// the pack cache.
template <typename T>
typename MeshBlockData<T>::VariableListResult MeshBlockData<T>::GetAllVariables() {
  VariableListResult result;

  for (auto &v : varVector_) {
    result.vars.push_back(v);
  }
  for (auto &sv : sparseVector_) {
    for (auto &v : sv->GetVector()) {
      result.vars.push_back(v);
    }
  }

  // Copy the expanded names into a flat list to be used as a cache-lookup key later.
  result.expanded_names.reserve(result.vars.size());
  for (auto &v : result.vars) {
    result.expanded_names.push_back(v->label());
  }
  return result;
}
// These versions do the same as above, but instead of adding the full container,
// they add a subset of the container... specified by either variable names
// or by metadata flags. In the case of names, the list can optionally only contain
// some subset of the sparse ids in a sparse variable.
//
// TODO(JL) We probably need a better interface here. Right now, the set of sparse_ids
// will apply to EACH sparse variable, so it wouldn't be possible to just select
// sparse_a_1 and sparse_b_2 if both sparse variables have indices 1 and 2 (we'd get both
// indices of both sparse variables)
template <typename T>
typename MeshBlockData<T>::VariableListResult
MeshBlockData<T>::GetVariablesByName(const std::vector<std::string> &names,
                                     const std::vector<int> &sparse_ids) {
  VariableListResult result;

  for (const auto &name : names) {
    auto it = varMap_.find(name);
    if (it != varMap_.end()) {
      result.vars.push_back(it->second);
      result.expanded_names.push_back(result.vars.back()->label());
      continue;
    }

    auto sit = sparseMap_.find(name);
    if (sit == sparseMap_.end()) {
      continue;
    }

    SparseVariable<T> &sparse = *sit->second;
    if (!sparse_ids.empty()) {
      for (auto &sparse_id : sparse_ids) {
        result.expanded_names.push_back(sparse.label() + "_" + std::to_string(sparse_id));
        if (sparse.HasSparseID(sparse_id)) {
          result.vars.push_back(sparse.Get(sparse_id));
        } else {
          // need to hold its space with something to make it uniform across blocks
          result.vars.push_back(nullptr);
        }
      }
    } else {
      for (auto &sv : sparse.GetVector()) {
        result.vars.push_back(sv);
      }
    }
  }

  return result;
}

template <typename T>
typename MeshBlockData<T>::VariableListResult
MeshBlockData<T>::GetVariablesByFlag(const std::vector<MetadataFlag> &flags) {
  VariableListResult result;

  for (auto &vpair : varMap_) {
    auto &var = vpair.second;
    if (var->metadata().AllFlagsSet(flags)) {
      result.vars.push_back(var);
    }
  }
  for (auto &vpair : sparseMap_) {
    auto &svar = vpair.second;
    if (svar->metadata().AllFlagsSet(flags)) {
      auto &varvec = svar->GetVector();
      for (auto &var : varvec) {
        result.vars.push_back(var);
      }
    }
  }

  for (auto &v : result.vars) {
    result.expanded_names.push_back(v->label());
  }

  return result;
}

template <typename T>
void MeshBlockData<T>::Remove(const std::string &label) {
  throw std::runtime_error("MeshBlockData<T>::Remove not yet implemented");
}

template <typename T>
TaskStatus MeshBlockData<T>::SendFluxCorrection() {
  Kokkos::Profiling::pushRegion("Task_SendFluxCorrection");
  for (auto &v : varVector_) {
    if (v->IsSet(Metadata::Independent)) {
      v->vbvar->SendFluxCorrection();
    }
  }
  for (auto &sv : sparseVector_) {
    if ((sv->IsSet(Metadata::Independent))) {
      CellVariableVector<T> vvec = sv->GetVector();
      for (auto &v : vvec) {
        v->vbvar->SendFluxCorrection();
      }
    }
  }
  Kokkos::Profiling::popRegion(); // Task_SendFluxCorrection
  return TaskStatus::complete;
}

template <typename T>
TaskStatus MeshBlockData<T>::ReceiveFluxCorrection() {
  Kokkos::Profiling::pushRegion("Task_ReceiveFluxCorrection");
  int success = 0, total = 0;
  for (auto &v : varVector_) {
    if (v->IsSet(Metadata::Independent)) {
      if (v->vbvar->ReceiveFluxCorrection()) success++;
      total++;
    }
  }
  for (auto &sv : sparseVector_) {
    if (sv->IsSet(Metadata::Independent)) {
      CellVariableVector<T> vvec = sv->GetVector();
      for (auto &v : vvec) {
        if (v->vbvar->ReceiveFluxCorrection()) success++;
        total++;
      }
    }
  }
  Kokkos::Profiling::popRegion(); // Task_ReceiveFluxCorrection
  if (success == total) return TaskStatus::complete;
  return TaskStatus::incomplete;
}

template <typename T>
TaskStatus MeshBlockData<T>::SendBoundaryBuffers() {
  Kokkos::Profiling::pushRegion("Task_SendBoundaryBuffers_MeshBlockData");
  // sends the boundary
  debug = 0;
  for (auto &v : varVector_) {
    if (v->IsSet(Metadata::FillGhost)) {
      v->resetBoundary();
      v->vbvar->SendBoundaryBuffers();
    }
  }
  for (auto &sv : sparseVector_) {
    if (sv->IsSet(Metadata::FillGhost)) {
      CellVariableVector<T> vvec = sv->GetVector();
      for (auto &v : vvec) {
        v->resetBoundary();
        v->vbvar->SendBoundaryBuffers();
      }
    }
  }

  Kokkos::Profiling::popRegion(); // Task_SendBoundaryBuffers_MeshBlockData
  return TaskStatus::complete;
}

template <typename T>
void MeshBlockData<T>::SetupPersistentMPI() {
  // setup persistent MPI
  for (auto &v : varVector_) {
    if (v->IsSet(Metadata::FillGhost)) {
      v->resetBoundary();
      v->vbvar->SetupPersistentMPI();
    }
  }
  for (auto &sv : sparseVector_) {
    if (sv->IsSet(Metadata::FillGhost)) {
      CellVariableVector<T> vvec = sv->GetVector();
      for (auto &v : vvec) {
        v->resetBoundary();
        v->vbvar->SetupPersistentMPI();
      }
    }
  }
  return;
}

template <typename T>
TaskStatus MeshBlockData<T>::ReceiveBoundaryBuffers() {
  Kokkos::Profiling::pushRegion("Task_ReceiveBoundaryBuffers_MeshBlockData");
  bool ret = true;
  // receives the boundary
  for (auto &v : varVector_) {
    if (!v->mpiStatus) {
      if (v->IsSet(Metadata::FillGhost)) {
        // ret = ret & v->vbvar->ReceiveBoundaryBuffers();
        // In case we have trouble with multiple arrays causing
        // problems with task status, we should comment one line
        // above and uncomment the if block below
        v->resetBoundary();
        v->mpiStatus = v->vbvar->ReceiveBoundaryBuffers();
        ret = (ret & v->mpiStatus);
      }
    }
  }
  for (auto &sv : sparseVector_) {
    if (sv->IsSet(Metadata::FillGhost)) {
      CellVariableVector<T> vvec = sv->GetVector();
      for (auto &v : vvec) {
        if (!v->mpiStatus) {
          v->resetBoundary();
          v->mpiStatus = v->vbvar->ReceiveBoundaryBuffers();
          ret = (ret & v->mpiStatus);
        }
      }
    }
  }

  Kokkos::Profiling::popRegion(); // Task_ReceiveBoundaryBuffers_MeshBlockData
  if (ret) return TaskStatus::complete;
  return TaskStatus::incomplete;
}

template <typename T>
TaskStatus MeshBlockData<T>::ReceiveAndSetBoundariesWithWait() {
  Kokkos::Profiling::pushRegion("Task_ReceiveAndSetBoundariesWithWait");
  for (auto &v : varVector_) {
    if ((!v->mpiStatus) && v->IsSet(Metadata::FillGhost)) {
      v->resetBoundary();
      v->vbvar->ReceiveAndSetBoundariesWithWait();
      v->mpiStatus = true;
    }
  }
  for (auto &sv : sparseVector_) {
    if ((sv->IsSet(Metadata::FillGhost))) {
      CellVariableVector<T> vvec = sv->GetVector();
      for (auto &v : vvec) {
        if (!v->mpiStatus) {
          v->resetBoundary();
          v->vbvar->ReceiveAndSetBoundariesWithWait();
          v->mpiStatus = true;
        }
      }
    }
  }
  Kokkos::Profiling::popRegion(); // Task_ReceiveAndSetBoundariesWithWait
  return TaskStatus::complete;
}
// This really belongs in MeshBlockData.cpp. However if I put it in there,
// the meshblock file refuses to compile.  Don't know what's going on
// there, but for now this is the workaround at the expense of code
// bloat.
template <typename T>
TaskStatus MeshBlockData<T>::SetBoundaries() {
  Kokkos::Profiling::pushRegion("Task_SetBoundaries_MeshBlockData");
  // sets the boundary
  for (auto &v : varVector_) {
    if (v->IsSet(Metadata::FillGhost)) {
      v->resetBoundary();
      v->vbvar->SetBoundaries();
    }
  }
  for (auto &sv : sparseVector_) {
    if (sv->IsSet(Metadata::FillGhost)) {
      CellVariableVector<T> vvec = sv->GetVector();
      for (auto &v : vvec) {
        v->resetBoundary();
        v->vbvar->SetBoundaries();
      }
    }
  }
  Kokkos::Profiling::popRegion(); // Task_SetBoundaries_MeshBlockData
  return TaskStatus::complete;
}

template <typename T>
void MeshBlockData<T>::ResetBoundaryCellVariables() {
  Kokkos::Profiling::pushRegion("ResetBoundaryCellVariables");
  for (auto &v : varVector_) {
    if (v->IsSet(Metadata::FillGhost)) {
      v->vbvar->var_cc = v->data;
    }
  }
  for (auto &sv : sparseVector_) {
    if (sv->IsSet(Metadata::FillGhost)) {
      CellVariableVector<T> vvec = sv->GetVector();
      for (auto &v : vvec) {
        v->vbvar->var_cc = v->data;
      }
    }
  }
  Kokkos::Profiling::popRegion(); // ResetBoundaryCellVariables
}

template <typename T>
TaskStatus MeshBlockData<T>::StartReceiving(BoundaryCommSubset phase) {
  Kokkos::Profiling::pushRegion("Task_StartReceiving");
  for (auto &v : varVector_) {
    if (v->IsSet(Metadata::FillGhost)) {
      v->resetBoundary();
      v->vbvar->StartReceiving(phase);
      v->mpiStatus = false;
    }
  }
  for (auto &sv : sparseVector_) {
    if (sv->IsSet(Metadata::FillGhost)) {
      CellVariableVector<T> vvec = sv->GetVector();
      for (auto &v : vvec) {
        v->resetBoundary();
        v->vbvar->StartReceiving(phase);
        v->mpiStatus = false;
      }
    }
  }
  Kokkos::Profiling::popRegion(); // Task_StartReceiving
  return TaskStatus::complete;
}

template <typename T>
TaskStatus MeshBlockData<T>::ClearBoundary(BoundaryCommSubset phase) {
  Kokkos::Profiling::pushRegion("Task_ClearBoundary");
  for (auto &v : varVector_) {
    if (v->IsSet(Metadata::FillGhost)) {
      v->vbvar->ClearBoundary(phase);
    }
  }
  for (auto &sv : sparseVector_) {
    if (sv->IsSet(Metadata::FillGhost)) {
      CellVariableVector<T> vvec = sv->GetVector();
      for (auto &v : vvec) {
        v->vbvar->ClearBoundary(phase);
      }
    }
  }
  Kokkos::Profiling::popRegion(); // Task_ClearBoundary
  return TaskStatus::complete;
}

template <typename T>
void MeshBlockData<T>::RestrictBoundaries() {
  Kokkos::Profiling::pushRegion("RestrictBoundaries");
  // TODO(JMM): Change this upon refactor of BoundaryValues
  auto pmb = GetBlockPointer();
  pmb->pbval->RestrictBoundaries();
  Kokkos::Profiling::popRegion(); // RestrictBoundaries
}

template <typename T>
void MeshBlockData<T>::ProlongateBoundaries() {
  Kokkos::Profiling::pushRegion("ProlongateBoundaries");
  // TODO(JMM): Change this upon refactor of BoundaryValues
  auto pmb = GetBlockPointer();
  pmb->pbval->ProlongateBoundaries();
  Kokkos::Profiling::popRegion();
}

template <typename T>
void MeshBlockData<T>::Print() {
  std::cout << "Variables are:\n";
  for (auto v : varVector_) {
    std::cout << " cell: " << v->info() << std::endl;
  }
  for (auto v : faceVector_) {
    std::cout << " face: " << v->info() << std::endl;
  }
  for (auto v : sparseVector_) {
    std::cout << " sparse:" << v->info() << std::endl;
  }
}

template <typename T>
std::array<int, 6> MeshBlockData<T>::calcArrDims_(const Metadata &metadata) {
  std::array<int, 6> arrDims;

  const auto &shape = metadata.Shape();
  const int N = shape.size();

  if (metadata.Where() == Metadata::Cell || metadata.Where() == Metadata::Face ||
      metadata.Where() == Metadata::Edge || metadata.Where() == Metadata::Node) {
    // Let the FaceVariable, EdgeVariable, and NodeVariable
    // classes add the +1's where needed.  They all expect
    // these dimensions to be the number of cells in each
    // direction, NOT the size of the arrays
    assert(N >= 1 && N <= 3);
    const IndexDomain entire = IndexDomain::entire;
    std::shared_ptr<MeshBlock> pmb = GetBlockPointer();
    arrDims[0] = pmb->cellbounds.ncellsi(entire);
    arrDims[1] = pmb->cellbounds.ncellsj(entire);
    arrDims[2] = pmb->cellbounds.ncellsk(entire);
    for (int i = 0; i < N; i++)
      arrDims[i + 3] = shape[i];
    for (int i = N; i < 3; i++)
      arrDims[i + 3] = 1;
  } else {
    // This variable is not necessarily tied to any specific
    // mesh element, so dims will be used as the actual array
    // size in each dimension
    assert(N >= 1 && N <= 6);
    for (int i = 0; i < N; i++)
      arrDims[i] = shape[i];
    for (int i = N; i < 6; i++)
      arrDims[i] = 1;
  }

  return arrDims;
}

template class MeshBlockData<double>;

} // namespace parthenon
