#!/bin/bash
CUR_DIR=$(dirname $(readlink -f "${BASH_SOURCE[0]}") )

if [ -z ${NEUWARE_HOME} ]; then
    echo "Error: not set NEUWARE_HOME environment variable"
    exit 1
else
    NEUWARE_HOME=${NEUWARE_HOME}
fi

export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:${CUR_DIR}/install/lib:${NEUWARE_HOME}/lib64
