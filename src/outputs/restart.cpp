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
//! \file restart.cpp
//  \brief writes restart files

#include <memory>
#include <string>
#include <utility>

#include "mesh/mesh.hpp"
#include "outputs/outputs.hpp"
#include "outputs/parthenon_hdf5.hpp"
#include "outputs/restart.hpp"

namespace parthenon {

//----------------------------------------------------------------------------------------
//! \fn void RestartReader::RestartReader(const std::string filename)
//  \brief Opens the restart file and stores appropriate file handle in fh_
RestartReader::RestartReader(const char *filename) : filename_(filename) {
#ifndef HDF5OUTPUT
  std::stringstream msg;
  msg << "### FATAL ERROR in Restart (Reader) constructor" << std::endl
      << "Executable not configured for HDF5 outputs, but HDF5 file format "
      << "is required for restarts" << std::endl;
  PARTHENON_FAIL(msg);
#else
  // Open the HDF file in read only mode
  fh_ = H5Fopen(filename, H5F_ACC_RDONLY, H5P_DEFAULT);

  // populate block size from the file
  std::vector<int32_t> blockSize = ReadAttr1D<int32_t>("Mesh", "blockSize");
  nx1_ = static_cast<hsize_t>(blockSize[0]);
  nx2_ = static_cast<hsize_t>(blockSize[1]);
  nx3_ = static_cast<hsize_t>(blockSize[2]);
#endif
}

//! \fn std::vector<T> RestartReader::ReadDataset(const char *name, size_t *count =
//! nullptr)
//  \brief Reads an entire dataset and returns as a 1D vector
template <typename T>
std::vector<T> RestartReader::ReadDataset(const char *name, size_t *count) {
  // Returns entire 1D array.
  // status, never checked.  We should...
#ifdef HDF5OUTPUT
  herr_t status;

  T *typepointer;
  hid_t theHdfType = getHdfType(typepointer);

  hid_t dataset = H5Dopen2(fh_, name, H5P_DEFAULT);
  hid_t dataspace = H5Dget_space(dataset);

  // Allocate array of correct size
  hid_t filespace = H5Dget_space(dataset);
  int rank = H5Sget_simple_extent_ndims(filespace);
  auto dims = new hsize_t[rank];
  status = H5Sget_simple_extent_dims(filespace, dims, NULL);
  hsize_t isize = 1;
  for (int idir = 0; idir < rank; idir++) {
    isize = isize * dims[idir];
  }
  if (count != nullptr) {
    *count = isize;
  }
  std::vector<T> data(isize);

  /** Define memory dataspace **/
  hid_t memspace = H5Screate_simple(rank, dims, NULL);

  // Read data from file
  status = H5Dread(dataset, theHdfType, memspace, dataspace, H5P_DEFAULT,
                   static_cast<void *>(data.data()));

  // CLose the dataspace and data set.
  H5Dclose(filespace);
  H5Dclose(dataspace);
  H5Dclose(dataset);
#else
  std::vector<T> data;
#endif
  return std::forward<std::vector<T>>(data);
}

//! \fn std::vector<T> RestartReader::ReadAttr1D(const char *dataset,
//! const char *name, size_t *count = nullptr)
//  \brief Reads a 1D array attribute for given dataset
template <typename T>
std::vector<T> RestartReader::ReadAttr1D(const char *dataset, const char *name,
                                         size_t *count) {
  // Returns entire 1D array.
  // status, never checked.  We should...
#ifdef HDF5OUTPUT
  herr_t status;

  T *typepointer;
  hid_t theHdfType = getHdfType(typepointer);

  hid_t dset = H5Dopen2(fh_, dataset, H5P_DEFAULT);
  hid_t attr = H5Aopen(dset, name, H5P_DEFAULT);
  hid_t dataspace = H5Aget_space(attr);

  // Allocate array of correct size
  int rank = H5Sget_simple_extent_ndims(dataspace);
  auto dims = new hsize_t[rank];
  status = H5Sget_simple_extent_dims(dataspace, dims, NULL);
  hsize_t isize = 1;
  for (int idir = 0; idir < rank; idir++) {
    isize = isize * dims[idir];
  }
  if (count != nullptr) {
    *count = isize;
  }
  std::vector<T> data(isize);

  // Read data from file
  status = H5Aread(attr, theHdfType, static_cast<void *>(data.data()));

  // CLose the dataspace and data set.
  H5Sclose(dataspace);
  H5Aclose(attr);
  H5Dclose(dset);
#else
  std::vector<T> data;
#endif

  return data;
}

//! \fn std::shared_ptr<std::vector<T>> RestartReader::ReadAttrString(const char *dataset,
//! const char *name, size_t *count = nullptr)
//  \brief Reads a string attribute for given dataset
std::string RestartReader::ReadAttrString(const char *dataset, const char *name,
                                          size_t *count) {
  // Returns entire 1D array.
  // status, never checked.  We should...
  herr_t status;

  hid_t theHdfType = H5T_C_S1;

  hid_t dset = H5Dopen2(fh_, dataset, H5P_DEFAULT);
  hid_t attr = H5Aopen(dset, name, H5P_DEFAULT);
  hid_t dataspace = H5Aget_space(attr);

  // Allocate array of correct size
  hid_t filetype = H5Aget_type(attr);
  hsize_t isize = H5Tget_size(filetype);
  isize++;
  if (count != nullptr) {
    *count = isize;
  }
  printf("dims=%lld\n", isize);

  char *s = static_cast<char *>(calloc(isize + 1, sizeof(char)));
  // Read data from file
  //  status = H5Aread(attr, theHdfType, static_cast<void *>(s));
  hid_t memType = H5Tcopy(H5T_C_S1);
  status = H5Tset_size(memType, isize);
  status = H5Aread(attr, memType, s);
  std::string data(s);
  std::cout << strlen(s) << ":input:" << s << std::endl;
  free(s);

  // CLose the dataspace and data set.
  H5Tclose(memType);
  H5Tclose(filetype);
  H5Sclose(dataspace);
  H5Aclose(attr);
  H5Dclose(dset);

  return data;
}

//! \fn void RestartReader::ReadBlock(const char *name, const int blockID, T *data)
//  \brief Reads data for one block from restart file
template <typename T>
void RestartReader::ReadBlock(const char *name, const int blockID, T *data) {
#ifdef HDF5OUTPUT
  // status, never checked.  We should...
  herr_t status;

  // compute block size, probaby could cache this.
  hid_t theHdfType = getHdfType(data);

  hid_t dataset = H5Dopen2(fh_, name, H5P_DEFAULT);
  hid_t dataspace = H5Dget_space(dataset);

  /** Define hyperslab in dataset **/
  hsize_t offset[5] = {static_cast<hsize_t>(blockID), 0, 0, 0, 0};
  hsize_t count[5] = {1, nx3_, nx2_, nx1_, 1};
  status = H5Sselect_hyperslab(dataspace, H5S_SELECT_SET, offset, NULL, count, NULL);

  /** Define memory dataspace **/
  hid_t memspace = H5Screate_simple(5, count, NULL);

  // Read data from file
  status = H5Dread(dataset, H5T_NATIVE_DOUBLE, memspace, dataspace, H5P_DEFAULT, data);

  // CLose the dataspace and data set.
  H5Dclose(dataspace);
  H5Dclose(dataset);
#endif
}

//----------------------------------------------------------------------------------------
//! \fn void RestartOutput::WriteOutputFile(Mesh *pm, ParameterInput *pin, bool flag)
//  \brief Cycles over all MeshBlocks and writes data to a single restart file.
void RestartOutput::WriteOutputFile(Mesh *pm, ParameterInput *pin, SimTime *tm) {
  // Restart output is currently only HDF5, so no HDF5 means no restart files
#ifdef HDF5OUTPUT
  // Writes a restart file in 'rhdf' format
  // This format has:
  //   /Input: Current input parameter key-value pairs
  //   /Info: information about simulation
  //   /Mesh: Information on mesh
  //   /Blocks: Metadata for blocks
  //   /var1: variable data
  //   /var2: variable data
  //   ....
  //   /varN: variable data
  //
  // It is expected that on restart global block ID will determine which data set is
  // read.
  //
  MeshBlock *pmb = pm->pblock;
  hsize_t max_blocks_global = pm->nbtotal;
  hsize_t num_blocks_local = 0;

  const IndexDomain interior = IndexDomain::interior;

  // shooting a blank just for getting the variable names
  const IndexRange out_ib = pmb->cellbounds.GetBoundsI(interior);
  const IndexRange out_jb = pmb->cellbounds.GetBoundsJ(interior);
  const IndexRange out_kb = pmb->cellbounds.GetBoundsK(interior);

  while (pmb != nullptr) {
    num_blocks_local++;
    pmb = pmb->next;
  }
  pmb = pm->pblock;
  // set output size

  // open HDF5 file
  // Define output filename
  auto filename = std::string(output_params.file_basename);
  filename.append(".");
  filename.append(output_params.file_id);
  filename.append(".");
  std::stringstream file_number;
  file_number << std::setw(5) << std::setfill('0') << output_params.file_number;
  filename.append(file_number.str());
  filename.append(".rhdf");

  hid_t file;
  hid_t acc_file = H5P_DEFAULT;

#ifdef MPI_PARALLEL
  /* set the file access template for parallel IO access */
  acc_file = H5Pcreate(H5P_FILE_ACCESS);

  /* ---------------------------------------------------------------------
     platform dependent code goes here -- the access template must be
     tuned for a particular filesystem blocksize.  some of these
     numbers are guesses / experiments, others come from the file system
     documentation.

     The sieve_buf_size should be equal a multiple of the disk block size
     ---------------------------------------------------------------------- */

  /* create an MPI_INFO object -- on some platforms it is useful to
     pass some information onto the underlying MPI_File_open call */
  MPI_Info FILE_INFO_TEMPLATE;
  int ierr;
  MPI_Status stat;
  ierr = MPI_Info_create(&FILE_INFO_TEMPLATE);
  ierr = H5Pset_sieve_buf_size(acc_file, 262144);
  ierr = H5Pset_alignment(acc_file, 524288, 262144);

  ierr = MPI_Info_set(FILE_INFO_TEMPLATE, "access_style", "write_once");
  ierr = MPI_Info_set(FILE_INFO_TEMPLATE, "collective_buffering", "true");
  ierr = MPI_Info_set(FILE_INFO_TEMPLATE, "cb_block_size", "1048576");
  ierr = MPI_Info_set(FILE_INFO_TEMPLATE, "cb_buffer_size", "4194304");

  /* tell the HDF5 library that we want to use MPI-IO to do the writing */
  ierr = H5Pset_fapl_mpio(acc_file, MPI_COMM_WORLD, FILE_INFO_TEMPLATE);
  ierr = H5Pset_fapl_mpio(acc_file, MPI_COMM_WORLD, MPI_INFO_NULL);
#endif

  // now open the file
  file = H5Fcreate(filename.c_str(), H5F_ACC_TRUNC, H5P_DEFAULT, acc_file);

  // attributes written here:
  // All ranks write attributes

  // write timestep relevant attributes
  hid_t localDSpace, localnDSpace, myDSet;
  herr_t status;
  hsize_t nLen;

  { // write input key-value pairs
    std::ostringstream oss;
    pin->ParameterDump(oss);

    // Mesh information
    localDSpace = H5Screate(H5S_SCALAR);
    myDSet = H5Dcreate(file, "/Input", PREDINT32, localDSpace, H5P_DEFAULT, H5P_DEFAULT,
                       H5P_DEFAULT);

    status = writeH5ASTRING("File", oss.str(), file, localDSpace, myDSet);

    // close space and set
    status = H5Sclose(localDSpace);
    status = H5Dclose(myDSet);
  }

  localDSpace = H5Screate(H5S_SCALAR);
  myDSet = H5Dcreate(file, "/Info", PREDINT32, localDSpace, H5P_DEFAULT, H5P_DEFAULT,
                     H5P_DEFAULT);

  int rootLevel = pm->GetRootLevel();
  int max_level = pm->GetCurrentLevel() - rootLevel;
  if (tm != nullptr) {
    status = writeH5AI32("NCycle", &(tm->ncycle), file, localDSpace, myDSet);
    status = writeH5AF64("Time", &(tm->time), file, localDSpace, myDSet);
  }
  status = writeH5ASTRING("Coordinates", std::string(pmb->coords.Name()), file,
                          localDSpace, myDSet);

  status = writeH5AI32("NumDims", &pm->ndim, file, localDSpace, myDSet);

  status = H5Sclose(localDSpace);

  hsize_t nPE = Globals::nranks;
  localDSpace = H5Screate_simple(1, &nPE, NULL);
  auto nblist = pm->GetNbList();
  status = writeH5AI32("BlocksPerPE", nblist.data(), file, localDSpace, myDSet);
  status = H5Sclose(localDSpace);
  status = H5Dclose(myDSet);

  // Mesh information
  localDSpace = H5Screate(H5S_SCALAR);
  myDSet = H5Dcreate(file, "/Mesh", PREDINT32, localDSpace, H5P_DEFAULT, H5P_DEFAULT,
                     H5P_DEFAULT);

  auto &nx1 = pmb->block_size.nx1;
  auto &nx2 = pmb->block_size.nx2;
  auto &nx3 = pmb->block_size.nx3;
  int bsize[3] = {nx1, nx2, nx3};
  nLen = 3;
  localnDSpace = H5Screate_simple(1, &nLen, NULL);
  status = writeH5AI32("blockSize", bsize, file, localnDSpace, myDSet);
  status = H5Sclose(localnDSpace);

  status = writeH5AI32("nbtotal", &pm->nbtotal, file, localDSpace, myDSet);
  status = writeH5AI32("nbnew", &pm->nbnew, file, localDSpace, myDSet);
  status = writeH5AI32("nbdel", &pm->nbdel, file, localDSpace, myDSet);
  status = writeH5AI32("rootLevel", &rootLevel, file, localDSpace, myDSet);
  status = writeH5AI32("MaxLevel", &max_level, file, localDSpace, myDSet);

  { // refinement flag
    int refine = (pm->adaptive ? 1 : 0);
    status = writeH5AI32("refine", &refine, file, localDSpace, myDSet);

    int multilevel = (pm->multilevel ? 1 : 0);
    status = writeH5AI32("multilevel", &multilevel, file, localDSpace, myDSet);
  }

  { // mesh bounds
    const auto &rs = pm->mesh_size;
    const Real limits[6] = {rs.x1min, rs.x2min, rs.x3min, rs.x1max, rs.x2max, rs.x3max};
    const Real ratios[3] = {rs.x1rat, rs.x2rat, rs.x3rat};
    nLen = 6;
    localnDSpace = H5Screate_simple(1, &nLen, NULL);
    status = writeH5AF64("bounds", limits, file, localnDSpace, myDSet);
    status = H5Sclose(localnDSpace);

    nLen = 3;
    localnDSpace = H5Screate_simple(1, &nLen, NULL);
    status = writeH5AF64("ratios", limits, file, localnDSpace, myDSet);
    status = H5Sclose(localnDSpace);
  }

  { // boundary conditions
    nLen = 6;
    localnDSpace = H5Screate_simple(1, &nLen, NULL);
    int bcsi[6];
    for (int ib = 0; ib < 6; ib++) {
      bcsi[ib] = static_cast<int>(pm->mesh_bcs[ib]);
    }
    status = writeH5AI32("bc", bcsi, file, localnDSpace, myDSet);
    status = H5Sclose(localnDSpace);
  }

  // close space and set
  status = H5Sclose(localDSpace);
  status = H5Dclose(myDSet);

  // end mesh section

  // write blocks
  // MeshBlock information
  // Write mesh coordinates to file
  hsize_t local_start[5], global_count[5], local_count[5];
  hid_t gLocations;

  local_start[0] = 0;
  local_start[1] = 0;
  local_start[2] = 0;
  local_start[3] = 0;
  local_start[4] = 0;
  for (int i = 0; i < Globals::my_rank; i++) {
    local_start[0] += nblist[i];
  }
  hid_t property_list = H5Pcreate(H5P_DATASET_XFER);
#ifdef MPI_PARALLEL
  H5Pset_dxpl_mpio(property_list, H5FD_MPIO_COLLECTIVE);
#endif

  // set starting poing in hyperslab for our blocks and
  // number of blocks on our PE

  // open blocks tab
  hid_t gBlocks = H5Gcreate(file, "/Blocks", H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);

  // write Xmin[ndim] for blocks
  {
    Real *tmpData = new Real[num_blocks_local * 3];
    local_count[0] = num_blocks_local;
    global_count[0] = max_blocks_global;
    pmb = pm->pblock;
    int i = 0;
    while (pmb != nullptr) {
      auto xmin = pmb->coords.GetXmin();
      tmpData[i] = xmin[0];
      i++;
      if (pm->ndim > 1) {
        tmpData[i] = xmin[1];
        i++;
      }
      if (pm->ndim > 2) {
        tmpData[i] = xmin[2];
        i++;
      }
      pmb = pmb->next;
    }
    local_count[1] = global_count[1] = pm->ndim;
    WRITEH5SLABDOUBLE("xmin", tmpData, gBlocks, local_start, local_count, global_count,
                      property_list);
    delete[] tmpData;
  }

  // write Block ID
  {
    // LOC.lx1,2,3
    hsize_t n;
    int i;

    n = 3;
    auto *tmpLoc = new int64_t[num_blocks_local * n];
    local_count[1] = global_count[1] = n;
    local_count[0] = num_blocks_local;
    global_count[0] = max_blocks_global;
    pmb = pm->pblock;
    i = 0;
    while (pmb != nullptr) {
      tmpLoc[i++] = pmb->loc.lx1;
      tmpLoc[i++] = pmb->loc.lx2;
      tmpLoc[i++] = pmb->loc.lx3;
      pmb = pmb->next;
    }
    WRITEH5SLABI64("loc.lx123", tmpLoc, gBlocks, local_start, local_count, global_count,
                   property_list);
    delete[] tmpLoc;

    // (LOC.)level, GID, LID, cnghost, gflag
    n = 5;
    auto *tmpID = new int[num_blocks_local * n];
    local_count[1] = global_count[1] = n;
    local_count[0] = num_blocks_local;
    global_count[0] = max_blocks_global;
    pmb = pm->pblock;
    i = 0;
    while (pmb != nullptr) {
      tmpID[i++] = pmb->loc.level;
      tmpID[i++] = pmb->gid;
      tmpID[i++] = pmb->lid;
      tmpID[i++] = pmb->cnghost;
      tmpID[i++] = pmb->gflag;
      pmb = pmb->next;
    }
    WRITEH5SLABI32("loc.level-gid-lid-cnghost-gflag", tmpID, gBlocks, local_start,
                   local_count, global_count, property_list);
    delete[] tmpID;
  }

  // close locations tab
  H5Gclose(gBlocks);

  // write variables

  // write variables
  // create persistent spaces
  local_count[1] = nx3;
  local_count[2] = nx2;
  local_count[3] = nx1;
  local_count[4] = 1;

  global_count[1] = nx3;
  global_count[2] = nx2;
  global_count[3] = nx1;
  global_count[4] = 1;

  hid_t local_DSpace = H5Screate_simple(5, local_count, NULL);
  hid_t global_DSpace = H5Screate_simple(5, global_count, NULL);

  // while we could do this as n variables and load all variables for
  // a block at one time, this is memory-expensive.  I think it is
  // well worth the multiple iterations through the blocks to load up
  // one variable at a time.  Besides most of the time will be spent
  // writing the HDF5 file to disk anyway...
  // If I'm wrong about this, we can always rewrite this later.
  // Sriram

  const hsize_t varSize = nx3 * nx2 * nx1;

  // Get an iterator on block 0 for variable listing
  auto ciX = ContainerIterator<Real>(
      pm->pblock->real_containers.Get(),
      {parthenon::Metadata::Independent, parthenon::Metadata::Restart}, true);
  for (auto &vwrite : ciX.vars) { // for each variable we write
    const std::string vWriteName = vwrite->label();
    hid_t vLocalSpace, vGlobalSpace;
    pmb = pm->pblock;
    const hsize_t vlen = vwrite->GetDim(4);
    local_count[4] = global_count[4] = vlen;
    Real *tmpData = new Real[varSize * vlen * num_blocks_local];

    // create spaces if required
    if (vlen == 1) {
      vLocalSpace = local_DSpace;
      vGlobalSpace = global_DSpace;
    } else {
      vLocalSpace = H5Screate_simple(5, local_count, NULL);
      vGlobalSpace = H5Screate_simple(5, global_count, NULL);
    }

    // load up data
    while (pmb != nullptr) { // for every block
      auto ci = ContainerIterator<Real>(
          pmb->real_containers.Get(),
          {parthenon::Metadata::Independent, parthenon::Metadata::Restart}, true);
      for (auto &v : ci.vars) {
        std::string name = v->label();
        if (name.compare(vWriteName) != 0) {
          // skip, not interested in this variable
          continue;
        }
        // Note index 4 transposed to interior
        auto v_h = (*v).data.GetHostMirrorAndCopy();
        hsize_t index = pmb->lid * varSize * vlen;
        LOADVARIABLEONE(index, tmpData, v_h, out_ib.s, out_ib.e, out_jb.s, out_jb.e,
                        out_kb.s, out_kb.e, vlen)
      }
      pmb = pmb->next;
    }
    // write dataset to file
    WRITEH5SLAB2(vWriteName.c_str(), tmpData, file, local_start, local_count, vLocalSpace,
                 vGlobalSpace, property_list);
    //    WRITEH5SLAB(vWriteName.c_str(), tmpData, file, local_start, local_count,
    //    vLocalSpace,
    //           vGlobalSpace, property_list);
    if (vlen > 1) {
      H5Sclose(vLocalSpace);
      H5Sclose(vGlobalSpace);
    }
    delete[] tmpData;
  }
  // close persistent data spaces
  H5Sclose(local_DSpace);
  H5Sclose(global_DSpace);

#ifdef MPI_PARALLEL
  /* release the file access template */
  ierr = H5Pclose(acc_file);
  ierr = MPI_Info_free(&FILE_INFO_TEMPLATE);
#endif

  H5Pclose(property_list);
  H5Fclose(file);

  // advance output parameters
  output_params.file_number++;
  output_params.next_time += output_params.dt;
  pin->SetInteger(output_params.block_name, "file_number", output_params.file_number);
  pin->SetReal(output_params.block_name, "next_time", output_params.next_time);
  return;
#endif
}
} // namespace parthenon
