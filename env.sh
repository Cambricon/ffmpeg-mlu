#!/bin/bash
CUR_DIR=$(dirname $(readlink -f "${BASH_SOURCE[0]}") )

if [ -z ${NEUWARE_HOME} ]; then
    echo "Error: not set NEUWARE_HOME environment variable"
    exit
fi

export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:${NEUWARE_HOME}/lib64
