# Parthenon developer guide

### (Kokkos) programming model

The following list contains a few overall design decision that are useful to keep in mind

- Kokkos never hides copies, i.e., there are no hidden deep copies and all data transfer needs to be explicit.
- Kernel launches (e.g., a parallel region) are non-blocking, i.e., the CPU continues to execute code following a kernel that is executed on a GPU.

### Kokkos wrappers/abstractions

- `par_for` wrappers use inclusive bounds, i.e., the loop will include the last index given
- `ParArrayND` arrays by default allocate on the *device* using default precision configured
- To create an array on the host with identical layout to the device array either use
  - `auto arr_host = Kokkos::create_mirror(arr_dev);` to always create a new array even if the device is associated with the host (e.g., OpenMP) or
  - `auto arr_host = Kokkos::create_mirror_view(arr_dev);` to create an array on the host if the HostSpace != DeviceSpace or get another reference to arr_dev through arr_host if HostSpace == DeviceSpace
- `par_for` and `Kokkos::deep_copy` by default use the standard stream (on Cuda devices) and are discouraged from use. Use `mb->par_for` and `mb->deep_copy` instead where `mb` is a `MeshBlock` (explanation: each `MeshBlock` has an `ExecutionSpace`, which may be changed at runtime, e.g., to a different stream, and the wrapper within a `MeshBlock` offer transparent access to the parallel region/copy where the `MeshBlock`'s `ExecutionSpace` is automatically used).

An arbitrary-dimensional wrapper for `Kokkos::Views` is available as
`ParArrayND`. See documentation [here](parthenon_arrays.md).

The wrappers `par_for_outer` and `par_for_inner` provide a nested parallelism interface that is needed for managing memory cached in tightly nested loops. The wrappers are documented [here](nested_par_for.md).

### The need for reductions within function handling `MeshBlock` data

A task (often a function associated with a `MeshBlock`) in the original Athena++ code was guaranteed to only being executed using a single CPU thread at any time.
Thus, a reduction within a single `MeshBlock`(e.g., finding a minimum value in the `MeshBlock` data or filling a buffer using an incremented `offset` pointer) did not require special care.
This is *not* true in Parthenon any more as a parallel region may be handled by many (GPU/...) threads in parallel.
Therefore, a `parallel_reduce` needs to be used instead of a `parallel_for`.

A strong hint where this is in order are places where a single variable is incremented within a kernel or where another reduction over MPI processes follows the preceding computations.

Currently, Parthenon does not provide wrappers for parallel reductions so the raw Kokkos versions are used.

Examples can be found in the [advection example](../example/advection/advection_package.cpp)
```diff
-  Real vmin = 1.0;
-  Real vmax = 0.0;
-  for (int k = kb.s; k <= kb.e; k++) {
-    for (int j = jb.s; j <= jb.e; j++) {
-      for (int i = ib.s; i <= ib.e; i++) {
-        vmin = (v(k, j, i) < vmin ? v(k, j, i) : vmin);
-        vmax = (v(k, j, i) > vmax ? v(k, j, i) : vmax);
-      }
-    }
-  }
+
+  typename Kokkos::MinMax<Real>::value_type minmax;
+  Kokkos::parallel_reduce(
+      "advection check refinement",
+      par_policy(Kokkos::MDRangePolicy<Kokkos::Rank<3>>(pmb->exec_space, 
+                                             {kb.s, jb.s, ib.s},
+                                             {kb.e + 1, jb.e + 1, ib.e + 1},
+                                             {1, 1, ib.e + 1 - ib.s})),
+      KOKKOS_LAMBDA(int k, int j, int i,
+                    typename Kokkos::MinMax<Real>::value_type &lminmax) {
+        lminmax.min_val = (v(k, j, i) < lminmax.min_val ? v(k, j, i) : lminmax.min_val);
+        lminmax.max_val = (v(k, j, i) > lminmax.max_val ? v(k, j, i) : lminmax.max_val);
+      },
+      Kokkos::MinMax<Real>(minmax));
```
(note the explicit use of `pmb->exec_space` to use to execution space associated with the `MeshBlock`).
Also note the `par_policy` function, which is used to control the default behavior of how
Kokkos launches kernels, see following section.

Another example are the [buffer packing]() functions
```diff
-void PackData(ParArrayND<T> &src, T *buf, int sn, int en, int si, int ei, int sj, int ej,
-              int sk, int ek, int &offset) {
-  for (int n = sn; n <= en; ++n) {
-    for (int k = sk; k <= ek; k++) {
-      for (int j = sj; j <= ej; j++) {
-#pragma omp simd
-        for (int i = si; i <= ei; i++)
-          buf[offset++] = src(n, k, j, i);
-      }
-    }
-  }
+void PackData(ParArray4D<T> &src, ParArray1D<T> &buf, int sn, int en, int si, int ei,
+              int sj, int ej, int sk, int ek, int &offset, MeshBlock *pmb) {
+  int ni = ei + 1 - si;
+  int nj = ej + 1 - sj;
+  int nk = ek + 1 - sk;
+  int nn = en + 1 - sn;
+
+  pmb->par_for(
+      "PackData 4D", sn, en, sk, ek, sj, ej, si, ei,
+      KOKKOS_LAMBDA(const int n, const int k, const int j, const int i) {
+        buf(offset + i - si + ni * (j - sj + nj * (k - sk + nk * (n - sn)))) =
+            src(n, k, j, i);
+      });
+  offset += nn * nk * nj * ni;
+  return;
```
Note the explicit calculation of the offset within the kernel and the explicit increment of the offset by the full extent after the kernel.

### Kernel launch abstraction

In addition to the wrappers to `parallel_for` regions, `Parthenon` also defines a `par_policy` function that
takes an execution policy as argument and an optional argument of `UseLightweightKernel<bool>`.
This controls whether Kokkos decorates the Kernel launch with a "lightweight" property.
Roughly speaking

- without that property (default) kernel launches are effectively blocking as Kokkos optimizes
the kernels
- with the property set to true, kernels are not optimized so that the calls are asynchronous,
which may allow for overlapping kernel execution on a device.
See also discussion [here](https://github.com/kokkos/kokkos/issues/2545).

The default behavior for the `par_for` abstractions in Parthenon is controlled through the
`PARTHENON_USE_LIGHTWEIGHT_HINT` cmake option (default `OFF`).

### FAQ

- What's the difference between `GetDim` and `extent`?

`ParArrayND` offer `GetDim` to access the underlying array dimension.
Here, `GetDim(0)` refers to the "first" dimension (e.g., x-direction).
`ParArray#D`s (with `#` being 1, 2, 3, ...) are direct typedefs to `Kokkos::View`s.
Thus, a call to `extent(0)` returns the dimension along the first index.
Given that `ParArray#D`s are constructed using reverse indices (note the `k,j,i` order in accessing elements), `extent` and `GetDim` using the same number usually have different meaning.

```
auto myarr_nd = ParArrayND<Real>("myarr",nx4,nx3,nx2,nx1); // is logically a 6D array under the hood
ParArray4D<Real> myarr_fd = myarr_nd.Get<4>(); // extracts a 4D View with fixed dimensions

myarr_nd.GetDim(4); // = nx4
myarr_nd.GetDim(1); // = nx1
myarr_fd.extent(0); // = nx4
myarr_fd.extent(3); // = nx1
```

- Where to allocate scratch pad memory (e.g., for temporary arrays that are shared between multiple function calls within a nested parallel region)?

Scratch pad memory is unique to each team can will be reused from a larger pool of memory available for all teams.
However, this allocation tracking only works if the `ScratchPadView`s are constructed within the outer parallel regions.
Therefore, allocating/constructing `ScratchPadView`s within functions that are called in the outer parallel region will lead to an overallocation of memory (and likely result in a segfault or out of memory exceptions).

- Where to use barriers/fences?

As mentioned above, kernel launches are non-blocking and kernel executions are asynchronous (potentially handles by the execution space scheduler).
Thus, barriers are required where the following code requires the successful execution of all kernels scheduled.
There are three obvious places where this applies:
1. Around MPI calls, e.g., sending a buffer should first be done when the kernel filling the buffer has finished. In order for the parallel execution to continue (e.g., multiple `MeshBlocks` in multiple device streams) the `fence` function of the corresponding execution space needs to be used, i.e., `pmb->exec_space.fence();` and *not* the global fence (`Kokkos::fence();`).
2. Within a nested parallel regions when using scratch space. The threads within a team are independent and thus a `member.team_barrier()` is required between filling the scratch space and (re)using it.
3. When collecting the results of a parallel reduction on a `View`. Usually `parallel_reduce` regions are blocking if the result of the reduction is a host variable, e.g., a simple `double` (or here a `Real`). If the result of the reduction is a `View` then the region is non-blocking and other places in the code should ensure that all reductions are finished (e.g., calculating the minimum timestep over all `MeshBlocks` of a single process.


- Why do I need to redefine variables preceding a parallel region?

The `KOKKOS_LAMBDA` macro expands into a capture by value `[=]` (plus host/device annotations).
Thus, class member variables are not captured directly, but rather `this` is, see also a related [issue](https://github.com/kokkos/kokkos/issues/695) on GitHub.
A redefinition, e.g., `auto coarse_coords = this->coarse_coords;` ensures that the desired object is properly captured and available within the kernel(/parallel region).

- What does `"error: The enclosing parent function ("...") for an extended __host__ __device__ lambda cannot have private or protected access within its class"` mean?

This is a current Cuda limitation for extended device lambdas, see [Cuda programming guide](https://docs.nvidia.com/cuda/cuda-c-programming-guide/#extended-lambda-restrictions), and can be "fixed"/addressed by making the function public.

