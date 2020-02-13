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
#ifndef INTERFACE_VARIABLE_HPP_
#define INTERFACE_VARIABLE_HPP_

///
/// A Variable type for Placebo-K.
/// Builds on AthenaArrays
/// Date: August 21, 2019
///
///
/// The variable class typically contains state data for the
/// simulation but can also hold non-mesh-based data such as physics
/// parameters, etc.  It inherits the AthenaArray class, which is used
/// for actural data storage and generation

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "athena.hpp"
#include "athena_arrays.hpp"
#include "bvals/cc/bvals_cc.hpp"
#include "Metadata.hpp"
#define DATASTATUS AthenaArray<Real>::DataStatus
class MeshBlock;

template <typename T>
class Variable : public AthenaArray<T> {
 public:
  /// Initialize a blank slate that will be set later
  Variable<T>(const std::string label, Metadata &metadata) :
    AthenaArray<T>(),
    _label(label),
    _m(metadata),
    mpiStatus(true) {
    //    std::cout << "_____CREATED VAR: " << _label << ":" << this << std::endl;
  }

  /// Initialize with a slice from another Variable
  Variable<T>(const std::string label,
              Variable<T> &src,
              const int dim,
              const int index,
              const int nvar) :
    AthenaArray<T>(),
    _label(label),
    _m(src.metadata()),
    mpiStatus(true)  {
    this->InitWithShallowSlice(src, dim, index, nvar);
    if ( _m.isSet(_m.fillGhost) ) {
      _m.set(_m.sharedComms);
    }
    //    std::cout << "_____CREATED VAR SLICE: " << _label << ":" << this << std::endl;
  }

  /// Create an alias for the variable by making a shallow slice with max dim
  Variable<T>(const std::string label, Variable<T> &src) :
    AthenaArray<T>(),
    _label(label),
    _m(src.metadata()),
    mpiStatus(true) {
    int dim = 6;
    int start = 0;
    int nvar = src.GetDim6();
    this->InitWithShallowSlice(src, dim, start, nvar);
    _m.set(_m.sharedComms);
    //    std::cout << "_____CREATED VAR SLICE: " << _label << ":" << this << std::endl;
  }


  /// Initialize a 6D variable
  Variable<T>(const std::string label,
              const std::array<int,6> dims,
              const Metadata &metadata) :
    AthenaArray<T>(dims[5], dims[4], dims[3], dims[2], dims[1], dims[0]),
    _label(label),
    _m(metadata),
    mpiStatus(true) {
    //    std::cout << "_____CREATED 6D VAR: " << _label << ":" << this << std::endl;
  }

  /// copy constructor
  Variable<T>(const Variable<T>& src,
              const bool allocComms=false,
              MeshBlock *pmb=nullptr);

  /// Return a new array with dimension 4 as dimension 1
  Variable<T>* createShuffle4() {
    // shuffles dim 4 to dim1

    // first construct new athena array
    const std::array<int,6> dims = {this->GetDim4(), this->GetDim1(), this->GetDim2(),
                                    this->GetDim3(), this->GetDim5(), this->GetDim6()};
    size_t stride = this->GetDim1()*this->GetDim2()*this->GetDim3();
    Variable<T> *vNew = new Variable<T>(_label, dims, _m);

    auto oldData = this->data();
    auto newData = vNew->data();
    // now fill in the data
    size_t index = 0;
    const size_t n4 = this->GetDim4();
    for (size_t i=0; i<stride; i++) {
      for (size_t j=0; j<n4; j++, newData++) {
        *newData = oldData[i+j*stride];
      }
    }
    return vNew;
  }


  ///< Assign label for variable
  void setLabel(const std::string label) { _label = label; }

  ///< retrieve label for variable
  std::string label() const { return _label; }

  ///< retrieve metadata for variable
  const Metadata metadata() const { return _m; }

  std::string getAssociated() { return _m.getAssociated(); }

  ///  T *Raw(); ///< returns raw data for variable

  /// return information string
  std::string info();

  /// allocate communication space based on info in MeshBlock
  void allocateComms(MeshBlock *pmb);

  /// Repoint vbvar's var_cc array at the current variable
  void resetBoundary();

  AthenaArray<Real> flux[3];    // used for boundary calculation
  AthenaArray<Real> *coarse_s;  // used for sending coarse boundary calculation
  AthenaArray<Real> *coarse_r;  // used for sending coarse boundary calculation
  CellCenteredBoundaryVariable *vbvar; // used in case of cell boundary communication
  bool mpiStatus;

 private:
  Metadata _m;
  std::string _label;
};



///
/// FaceVariable extends the FaceField struct to include the metadata
/// and label so that we can refer to variables by name.  Since Athena
/// currently only has scalar Face fields, we also only allow scalar
/// face fields
struct FaceVariable : FaceField {
 public:
  /// Initialize a face variable
  FaceVariable(const std::string label, const Metadata &metadata,
               const int ncells3, const int ncells2, const int ncells1,
               const DATASTATUS init=DATASTATUS::allocated) :
    FaceField(ncells3, ncells2, ncells1, init),
    _label(label),
    _m(metadata) {
    if ( metadata.hasMaterials() ) {
      throw std::invalid_argument ("Materials not yet implemented for FaceVariable");
    }
  }

  /// Create an alias for the variable by making a shallow slice with max dim
  FaceVariable(std::string label, FaceVariable &src) :
    FaceField(0,0,0,DATASTATUS::allocated),
    _label(label),
    _m(src.metadata()) {
    this->x1f.InitWithShallowSlice(src.x1f, 3, 0, src.x1f.GetDim3());
    this->x2f.InitWithShallowSlice(src.x2f, 3, 0, src.x2f.GetDim3());
    this->x3f.InitWithShallowSlice(src.x3f, 3, 0, src.x3f.GetDim3());
  }

  ///< retrieve label for variable
  std::string label() { return _label; }

  ///< retrieve metadata for variable
  Metadata metadata() { return _m; }

  /// return information string
  std::string info();

 private:
  Metadata _m;
  std::string _label;
};

///
/// EdgeVariable extends the EdgeField struct to include the metadata
/// and label so that we can refer to variables by name.  Since Athena
/// currently only has scalar Edge fields, we also only allow scalar
/// edge fields
struct EdgeVariable : EdgeField {
 public:
  ///< retrieve metadata for variable
  Metadata metadata() const { return _m; }

  /// Initialize a edge variable
  EdgeVariable(const std::string label, const Metadata &metadata,
               const int ncells3, const int ncells2, const int ncells1,
               const DATASTATUS init=DATASTATUS::allocated) :
    EdgeField(ncells3, ncells2, ncells1, init),
    _label(label),
    _m(metadata) {
    if ( metadata.hasMaterials() ) {
      throw std::invalid_argument ("Materials not yet implemented for FaceVariable");
    }
  }

  /// Create an alias for the variable by making a shallow slice with max dim
  EdgeVariable(const std::string label, const EdgeVariable &src) :
    EdgeField(0,0,0,DATASTATUS::allocated),
    _label(label),
    _m(src.metadata()) {
    this->x1e.InitWithShallowSlice(src.x1e, 3, 0, src.x1e.GetDim3());
    this->x2e.InitWithShallowSlice(src.x2e, 3, 0, src.x2e.GetDim3());
    this->x3e.InitWithShallowSlice(src.x3e, 3, 0, src.x3e.GetDim3());
  }

  ///< retrieve label for variable
  std::string label() { return _label; }

  /// return information string
  std::string info();

 private:
  Metadata _m;
  std::string _label;
};

template<typename T>
class VariableVector : public std::vector<std::shared_ptr<Variable<T>>> {
 public:
  Variable<T>& operator()(int m) {
    return *(*this)[m];
  }
  T& operator()(int m, int i) {
    return (*(*this)[m])(i);
  }
  T& operator()(int m, int j, int i) {
    return (*(*this)[m])(j,i);
  }
  T& operator()(int m, int k, int j, int i) {
    return (*(*this)[m])(k,j,i);
  }
  T& operator()(int m, int l, int k, int j, int i) {
    return (*(*this)[m])(l,k,j,i);
  }
  T& operator()(int m, int n, int l, int k, int j, int i) {
    return (*(*this)[m])(n,l,k,j,i);
  }
  T& operator()(int m, int g, int n, int l, int k, int j, int i) {
    return (*(*this)[m])(g,n,l,k,j,i);
  }
  Metadata& metadata() const { return this->begin().second->metadata();}
};


#endif // INTERFACE_VARIABLE_HPP_
