# Anonymous Variables

## Metadata

Variables can be tagged with a variety of `MetadataFlag` values. These flags
primarily allow an application to tell Parthenon to apply certain behaviors to
each field.

### Variable Topology

Topology essentially specifies on which place on the finite volume
grid the variable is defined. These fields specify what index space
the variable is allocated over. E.g.  `Metadata::Cell` specifies that
the variable is allocated over the cell index space in each block.

The following fields specify the topology of the variable, and are
mutually exclusive:

- `Metadata::None`: no topology specified. The variable could be
  anywhere, or location is not a meaningful concept for this variable.
- `Metadata::Cell`: The variable is defined on cell centers. The
  variable is likely volume-averaged.
- `Metadata::Face`: The variable is defined on cell faces. The
  variable is likely area-averaged.
- `Metadata::Edge`: The variable is defined on cell edges. The
  variable is likely length-averaged.
- `Metadata::Node`: The variable is defined at nodes, i.e.,
  cell-corners. The variable might be volume-averaged, or defined
  pointwise.

### Variable Behaviors

These flags can be used to tell an application code how to treat a
variable in relation to the problem.

- `Metadata::Advected` implies a variable is advected with the flow of
  another variable, e.g., a velocity.
- `Metadata::Conserved` implies a variable obeys a conservation law.
- `Metadata::Intensive` implies that the value of a variable does not
  scale with volume.

### Output

These flags specify how a variable interacts with I/O. Enable them to
enable output properties.

- `Metadata::Restart` implies a variable is required in restart files
- `Metadata::Graphics` implies a varaible should be output for visualization

### Tensor properties and boundaries

For multidimensional variables, these flags specify how to treat the
individual components at boundaries. For concreteness, we will discuss
reflecting boundaries. But this may apply more broadly. A variable
with no flag set is assumed to be a *Scalar*. Scalars obey 
[Dirichlet boundary conditions](https://en.wikipedia.org/wiki/Dirichlet_boundary_condition)
at reflecting boundaries and are set to a constant value.
The following flags are mutually exclusive.

- `Metadata::Vector` implies the variable transforms as a *vector* at
  reflecting boundaries. And so i-th component is flipped for a
  boundary in the i-th direction.
- `Metadata::Tensor` is the generalization of the vector boundary
  condition, but for tensor quantities.

### Independent/Derived

These flags specify to an application code, and the infrastructure,
whether or not a variable is part of independent state. Derived
quantities can be calculated from the set of independent quantities,
while independent quantities cannot. The following flags are mutually
exclusive and required. All variables should be either independent or
derived.

- `Metadata::Independent` implies the variable is part of independent
  state
- `Metadata::Derived` implies the variable can be calculated, given
  the independent state

### Communication

These flags specify both how ghost zones are treated, and whether
variables are copied or not in multiple stages.

- `Metadata::OneCopy` are shared between stages. They are only ever
  allocated once.
- `Metadata::FillGhost` specifies that ghost zones for this variable
  must be filled via communication or boundary conditions. This is not
  always required. `OneCopy` variables, for example, may not need
  this.

- `Metadata::SharedComms` TODO(JMM): not sure this variable is used

### Ghost Zones, Communication, and Fluxes

Depending on a combination of flags, extra communication buffers and
classes may be allocated. The behaviours are the following:

- If `Metadata::FillGhosts` is set, boundary conditions data is set,
  MPI communication buffers are allocated, and buffers for a coarse
  grid are allocated. These buffers are *one-copy*, meaning they are
  shared between all instances of a variable in all `Containers` in a
  `ContainerCollection`.

- If, in addition to `Metadata::FillGhosts`, `Metadata::Independent`
  is set, the flux vector for the variable is allocated. In the
  current design, the flux is fundamental to communication, since flux
  corrections accross meshblocks utilize the flux buffer.

### Application Metadata Flags

Applications can allocate their own flags by calling
`Metadata::AllocateNewFlag("FlagName")`. For example:
```c++
using parthenon::Metadata;
using parthenon::MetadataFlag;

MetadataFlag const my_app_flag = Metadata::AllocateNewFlag("MyAppFlag");
```

These can be used in all the same contexts that the built-in metadata
flags are used. Parthenon will not interpret them in any way - it's up
to the application to interpret them.
