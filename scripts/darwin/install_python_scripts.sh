#!/bin/bash
#=========================================================================================
# (C) (or copyright) 2020-2021. Triad National Security, LLC. All rights reserved.
#
# This program was produced under U.S. Government contract 89233218CNA000001 for Los
# Alamos National Laboratory (LANL), which is operated by Triad National Security, LLC
# for the U.S. Department of Energy/National Nuclear Security Administration. All rights
# in the program are reserved by Triad National Security, LLC, and the U.S. Department
# of Energy/National Nuclear Security Administration. The Government is granted for
# itself and others acting on its behalf a nonexclusive, paid-up, irrevocable worldwide
# license in this material to reproduce, prepare derivative works, distribute copies to
# the public, perform publicly and display publicly, and to permit others to do so.
#=========================================================================================

# Load system env only
SCRIPT=$(realpath "$0")
SOURCE=$(dirname "$SCRIPT")
source "${SOURCE}/base_setup.sh"

# Remove old installed directory if it exists
if [ -d "${PYTHON_SCRIPTS_DIR}" ]
then
  rm -rf "${PYTHON_SCRIPTS_DIR}"
fi

cd "$PARTHENON_DIR/scripts/python/packages/parthenon_performance_app"
# Dependencies should be handled by spack
pip install . --no-dependencies --target="${PYTHON_SCRIPTS_DIR}"

cd "$PARTHENON_DIR/scripts/python/packages/parthenon_tools"
# Dependencies should be handled by spack
pip install . --no-dependencies --target="${PYTHON_SCRIPTS_DIR}"

"${PYTHON_SCRIPTS_DIR}/bin/parthenon_metrics_app.py" --status "pending" --status-context "Parthenon Metrics App" --status-description "Caching parthenon repo path." --status-url "${CI_JOB_URL}"


# Cache repository path, so that the app knows where the repo is
"${PYTHON_SCRIPTS_DIR}/bin/parthenon_metrics_app.py" --repository-path "$PARTHENON_DIR"
