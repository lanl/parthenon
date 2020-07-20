//========================================================================================
// Athena++ astrophysical MHD code
// Copyright(C) 2014 James M. Stone <jmstone@princeton.edu> and other code contributors
// Licensed under the 3-clause BSD License, see LICENSE file for details
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
#include <array>
#include <cmath>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include <catch2/catch.hpp>

#include "basic_types.hpp"
#include "defs.hpp"
#include "interface/container.hpp"
#include "interface/container_iterator.hpp"
#include "interface/metadata.hpp"
#include "interface/variable.hpp"
#include "interface/variable_pack.hpp"
#include "kokkos_abstraction.hpp"
#include "parthenon_arrays.hpp"

using parthenon::CellVariable;
using parthenon::CellVariableVector;
using parthenon::Container;
using parthenon::ContainerIterator;
using parthenon::DevExecSpace;
using parthenon::loop_pattern_mdrange_tag;
using parthenon::Metadata;
using parthenon::MetadataFlag;
using parthenon::PackIndexMap;
using parthenon::par_for;
using parthenon::ParArray4D;
using parthenon::ParArrayND;
using parthenon::Real;
using parthenon::X1DIR;
using parthenon::X2DIR;
using parthenon::X3DIR;

bool indx_between_bounds(int indx, const std::pair<int, int> &bnds) {
  if (indx < bnds.first) return false;
  if (indx > bnds.second) return false;
  return true;
}
bool intervals_intersect(const std::pair<int, int> &i1, const std::pair<int, int> &i2) {
  if (indx_between_bounds(i1.first, i2)) return true;
  if (indx_between_bounds(i1.second, i2)) return true;
  if (indx_between_bounds(i2.first, i1)) return true;
  if (indx_between_bounds(i2.second, i1)) return true;
  return false;
}

TEST_CASE("Can pull variables from containers based on Metadata",
          "[ContainerIterator][coverage]") {
  GIVEN("A Container with a set of variables initialized to zero") {
    Container<Real> rc;
    Metadata m_in({Metadata::Independent, Metadata::FillGhost});
    Metadata m_out;
    std::vector<int> scalar_block_size{16, 16, 16};
    std::vector<int> vector_block_size{16, 16, 16, 3};
    // Make some variables
    rc.Add("v1", m_in, scalar_block_size);
    rc.Add("v2", m_out, scalar_block_size);
    rc.Add("v3", m_in, vector_block_size);
    rc.Add("v4", m_out, vector_block_size);
    rc.Add("v5", m_in, scalar_block_size);
    rc.Add("v6", m_out, scalar_block_size);

    WHEN("We extract a subcontainer") {
      auto subcontainer = Container<Real>(rc, {"v1", "v3", "v5"});
      THEN("The container has the names in the right order") {
        auto vars = subcontainer.GetCellVariableVector();
        REQUIRE(vars[0]->label() == "v1");
        REQUIRE(vars[1]->label() == "v3");
        REQUIRE(vars[2]->label() == "v5");
      }
    }

    auto v = rc.PackVariables();
    par_for(
        "Initialize variables", DevExecSpace(), 0, v.GetDim(4) - 1, 0, v.GetDim(3) - 1, 0,
        v.GetDim(2) - 1, 0, v.GetDim(1) - 1,
        KOKKOS_LAMBDA(const int l, const int k, const int j, const int i) {
          v(l, k, j, i) = 0.0;
        });

    WHEN("we check them") {
      // set them all to zero
      const CellVariableVector<Real> &cv = rc.GetCellVariableVector();
      for (int n = 0; n < cv.size(); n++) {
        ParArrayND<Real> v = cv[n]->data;
        par_for(
            "Initialize variables", DevExecSpace(), 0, v.GetDim(4) - 1, 0,
            v.GetDim(3) - 1, 0, v.GetDim(2) - 1, 0, v.GetDim(1) - 1,
            KOKKOS_LAMBDA(const int l, const int k, const int j, const int i) {
              v(l, k, j, i) = 0.0;
            });
      }
      THEN("they should sum to zero") {
        using policy4D = Kokkos::MDRangePolicy<Kokkos::Rank<4>>;
        Real total = 0.0;
        Real sum = 1.0;
        Kokkos::parallel_reduce(
            policy4D({0, 0, 0, 0}, {v.GetDim(4), v.GetDim(3), v.GetDim(2), v.GetDim(1)}),
            KOKKOS_LAMBDA(const int l, const int k, const int j, const int i,
                          Real &vsum) { vsum += v(l, k, j, i); },
            sum);
        total += sum;
        REQUIRE(total == 0.0);
      }

      AND_THEN("we touch the right number of elements") {
        using policy4D = Kokkos::MDRangePolicy<Kokkos::Rank<4>>;
        int total = 0;
        int sum = 1;
        Kokkos::parallel_reduce(
            policy4D({0, 0, 0, 0}, {v.GetDim(4), v.GetDim(3), v.GetDim(2), v.GetDim(1)}),
            KOKKOS_LAMBDA(const int l, const int k, const int j, const int i, int &cnt) {
              cnt++;
            },
            sum);
        total += sum;
        REQUIRE(total == 40960);
      }
    }

    WHEN("we set Independent variables to one") {
      // set "Independent" variables to one
      auto v = rc.PackVariables({Metadata::Independent});
      par_for(
          "Set independent variables", DevExecSpace(), 0, v.GetDim(4) - 1, 0,
          v.GetDim(3) - 1, 0, v.GetDim(2) - 1, 0, v.GetDim(1) - 1,
          KOKKOS_LAMBDA(const int l, const int k, const int j, const int i) {
            v(l, k, j, i) = 1.0;
          });

      THEN("they should sum appropriately") {
        using policy4D = Kokkos::MDRangePolicy<Kokkos::Rank<4>>;
        Real total = 0.0;
        Real sum = 1.0;
        Kokkos::parallel_reduce(
            policy4D({0, 0, 0, 0}, {v.GetDim(4), v.GetDim(3), v.GetDim(2), v.GetDim(1)}),
            KOKKOS_LAMBDA(const int l, const int k, const int j, const int i,
                          Real &vsum) { vsum += v(l, k, j, i); },
            sum);
        total += sum;
        REQUIRE(std::abs(total - 20480.0) < 1.e-14);
      }
      AND_THEN("pulling out a subset by name should work") {
        using policy4D = Kokkos::MDRangePolicy<Kokkos::Rank<4>>;
        auto v = rc.PackVariables({"v2", "v3", "v5"});
        Real total = 0.0;
        Real sum = 1.0;
        Kokkos::parallel_reduce(
            policy4D({0, 0, 0, 0}, {v.GetDim(4), v.GetDim(3), v.GetDim(2), v.GetDim(1)}),
            KOKKOS_LAMBDA(const int l, const int k, const int j, const int i,
                          Real &vsum) { vsum += v(l, k, j, i); },
            sum);
        total += sum;
        REQUIRE(std::abs(total - 16384.0) < 1.e-14);
      }
    }

    WHEN("we set individual fields by index") {
      PackIndexMap vmap;
      auto v = rc.PackVariables(std::vector<std::string>({"v3", "v6"}), vmap);
      const int iv3lo = vmap["v3"].first;
      const int iv3hi = vmap["v3"].second;
      const int iv6 = vmap["v6"].first;
      THEN("The pack indices are all different") {
        REQUIRE(iv3hi > iv3lo);
        REQUIRE(iv3hi != iv6);
        REQUIRE(iv3lo != iv6);
        if (iv6 > iv3lo) REQUIRE(iv6 > iv3hi);
      }
      par_for(
          "Initialize variables", DevExecSpace(), 0, v.GetDim(3) - 1, 0, v.GetDim(2) - 1,
          0, v.GetDim(1) - 1, KOKKOS_LAMBDA(const int k, const int j, const int i) {
            v(iv3lo + 1, k, j, i) = 1.0;
            v(iv6, k, j, i) = 3.0;
          });
      THEN("the values should as we expect") {
        PackIndexMap vmap; // recompute the pack
        auto v = rc.PackVariables(std::vector<std::string>({"v3", "v6"}), vmap);
        const int iv3lo = vmap["v3"].first;
        const int iv3hi = vmap["v3"].second;
        const int iv6 = vmap["v6"].first;
        using policy4D = Kokkos::MDRangePolicy<Kokkos::Rank<4>>;
        Real total = 0.0;
        Real sum = 1.0;
        Kokkos::parallel_reduce(
            policy4D({0, 0, 0, 0}, {v.GetDim(4), v.GetDim(3), v.GetDim(2), v.GetDim(1)}),
            KOKKOS_LAMBDA(const int l, const int k, const int j, const int i,
                          Real &vsum) {
              bool check3 = (l == iv3lo + 1);
              bool check6 = (l == iv6);
              vsum += (check3 && v(l, k, j, i) != 1.0);
              vsum += (check6 && v(l, k, j, i) != 3.0);
            },
            sum);
        total += sum;
        REQUIRE(total == 0.0);
      }
      AND_THEN("summing up everything should still work") {
        using policy4D = Kokkos::MDRangePolicy<Kokkos::Rank<4>>;
        auto v = rc.PackVariables();
        Real total = 0.0;
        Real sum = 1.0;
        Kokkos::parallel_reduce(
            policy4D({0, 0, 0, 0}, {v.GetDim(4), v.GetDim(3), v.GetDim(2), v.GetDim(1)}),
            KOKKOS_LAMBDA(const int l, const int k, const int j, const int i,
                          Real &vsum) { vsum += v(l, k, j, i); },
            sum);
        total += sum;
        REQUIRE(std::abs(total - 16384.0) < 1.e-14);
      }
    }

    WHEN("we set fluxes of independent variables") {
      auto vf = rc.PackVariablesAndFluxes({Metadata::Independent, Metadata::FillGhost});
      par_for(
          "Set fluxes", DevExecSpace(), 0, vf.GetDim(4) - 1, 0, vf.GetDim(3) - 1, 0,
          vf.GetDim(2) - 1, 0, vf.GetDim(1) - 1,
          KOKKOS_LAMBDA(const int l, const int k, const int j, const int i) {
            vf(l, k, j, i) = 0.0;
            vf.flux(X1DIR, l, k, j, i) = 16.0 - i;
            vf.flux(X2DIR, l, k, j, i) = 16.0 - j;
            vf.flux(X3DIR, l, k, j, i) = 16.0 - k;
          });
      THEN("adding in the fluxes should change the values appropriately") {
        par_for(
            "Update vars", DevExecSpace(), 0, vf.GetDim(4) - 1, 0, vf.GetDim(3) - 2, 0,
            vf.GetDim(2) - 2, 0, vf.GetDim(1) - 2,
            KOKKOS_LAMBDA(const int l, const int k, const int j, const int i) {
              vf(l, k, j, i) -=
                  ((vf.flux(X1DIR, l, k, j, i + 1) - vf.flux(X1DIR, l, k, j, i)) +
                   (vf.flux(X2DIR, l, k, j + 1, i) - vf.flux(X2DIR, l, k, j, i)) +
                   (vf.flux(X3DIR, l, k + 1, j, i) - vf.flux(X3DIR, l, k, j, i)));
            });

        using policy4D = Kokkos::MDRangePolicy<Kokkos::Rank<4>>;
        Real total = 0.0;
        Real sum = 1.0;
        Kokkos::parallel_reduce(
            policy4D({0, 0, 0, 0},
                     {v.GetDim(4), v.GetDim(3) - 1, v.GetDim(2) - 1, v.GetDim(1) - 1}),
            KOKKOS_LAMBDA(const int l, const int k, const int j, const int i,
                          Real &vsum) { vsum += v(l, k, j, i); },
            sum);
        total += sum;
        REQUIRE(std::abs(total - 50625.0) < 1.e-14);
      }
    }

    WHEN("we add sparse fields") {
      Metadata m_sparse;
      m_sparse = Metadata({Metadata::Sparse}, 1);
      rc.Add("vsparse", m_sparse, scalar_block_size);
      m_sparse = Metadata({Metadata::Sparse}, 13);
      rc.Add("vsparse", m_sparse, scalar_block_size);
      m_sparse = Metadata({Metadata::Sparse}, 42);
      rc.Add("vsparse", m_sparse, scalar_block_size);
      THEN("the low and high index bounds are correct as returned by PackVariables") {
        PackIndexMap imap;
        auto v = rc.PackVariables({"v3", "v6", "vsparse"}, imap);
        REQUIRE(imap["vsparse"].second == imap["vsparse"].first + 2);
        REQUIRE(imap["v6"].second == imap["v6"].first);
        REQUIRE(imap["v3"].second == imap["v3"].first + 2);
        REQUIRE(!indx_between_bounds(imap["v6"].first, imap["v3"]));
        REQUIRE(!indx_between_bounds(imap["v6"].first, imap["vsparse"]));
        REQUIRE(!intervals_intersect(imap["v3"], imap["vsparse"]));
      }
      AND_THEN("bounds are still correct if I get just a subset of the sparse fields") {
        PackIndexMap imap;
        auto v = rc.PackVariables({"v3", "vsparse"}, {1, 42}, imap);
        REQUIRE(imap["vsparse"].second == imap["vsparse"].first + 1);
        REQUIRE(std::abs(imap["vsparse_42"].first - imap["vsparse_1"].first) == 1);
        REQUIRE(!intervals_intersect(imap["v3"], imap["vsparse"]));
      }
      AND_THEN("the association with sparse ids is captured") {
        PackIndexMap imap;
        auto v = rc.PackVariables({"v3", "v6", "vsparse"}, imap);
        int correct = 0;
        const int v3first = imap["v3"].first;
        const int v6first = imap["v6"].first;
        const int vsfirst = imap["vsparse"].first;
        const int vssecnd = imap["vsparse"].second;
        Kokkos::parallel_reduce(
            "add correct checks", 1,
            KOKKOS_LAMBDA(const int i, int &sum) {
              sum = (v.GetSparse(v3first) == -1);
              sum += (v.GetSparse(v6first) == -1);
              sum += (v.GetSparse(vsfirst) == 1);
              sum += (v.GetSparse(vsfirst + 1) == 13);
              sum += (v.GetSparse(vssecnd) == 42);
            },
            correct);
        REQUIRE(correct == 5);
      }
    }

    WHEN("we add a 2d variable") {
      std::vector<int> twod_block_size{16, 16, 1};
      rc.Add("v2d", m_in, twod_block_size);
      auto packw2d = rc.PackVariablesAndFluxes({"v2d"}, {"v2d"});
      THEN("The pack knows it is 2d") { REQUIRE(packw2d.GetNdim() == 2); }
    }
  }
}
