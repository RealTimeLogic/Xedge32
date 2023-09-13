#!/bin/bash

echo "Building Xedge32 resource file main/BAS/examples/xedge/obj/XedgeZip.c"

directories=("main/BAS" "main/BAS-Resources" "main/lua-protobuf")
for dir in "${directories[@]}"; do
    if [ ! -d "$dir" ]; then
        echo "Directory '$dir' does not exist."
        echo "Please make sure you cloned the repository recursively."
        exit 1
    fi
done

mkdir -p main/BAS/examples/xedge/obj

export BIN2C=main/BAS-Resources/tools/linux/bin2c
#Resource build directory
RB=main/BAS-Resources/build
pushd $RB || exit 1
chmod +x Xedge.sh
echo "Running $PWD/Xedge.sh"
export NO_BIN2C=1
./Xedge.sh || exit 1
popd

echo "Adding ESP32 modules"
zip -D -r -9 $RB/Xedge.zip  .lua || exit 1
$BIN2C -z getLspZipReader $RB/Xedge.zip  main/BAS/examples/xedge/obj/XedgeZip.c || exit 1
echo "Created: main/BAS/examples/xedge/obj/XedgeZip.c"



