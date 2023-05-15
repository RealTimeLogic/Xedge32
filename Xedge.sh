#!/bin/bash

HOMEDIR="$(pwd)"

if [[ -z "${BAS_RESOURCES}" ]]; then
    BAS_RESOURCES="main/BAS-Resources"
fi

BUILDIR="${BAS_RESOURCES}/build"

if [[ ! -f "${BUILDIR}/Xedge.sh" ]]; then
    echo -e "Cannot find the BAS-Resources directory.\nMake sure the directory is in 'main' or set the environment variable BAS_RESOURCES=path"
    exit 1
fi

pushd "${BUILDIR}"
export NO_BIN2C="no"
./Xedge.sh || exit 2
mv Xedge.zip "${HOMEDIR}/" || exit 3
popd

zip -D -q -u -r -9 Xedge.zip .lua || exit 4
bin2c -z getLspZipReader Xedge.zip XedgeZip.c || exit 5
mv XedgeZip.c main/BAS/examples/xedge/obj  || exit 5

echo "XedgeZip.c copied to main/BAS/examples/xedge/obj"

