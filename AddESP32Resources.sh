#!/bin/bash

echo "Adding Protobuf and ESP32 resources to BAS/examples/xedge/obj/lsp.zip"

# Check if zip is installed
if ! command -v zip &> /dev/null
then
    # Install zip if it is not installed
    echo "zip is not installed. Installing it now..."
    sudo apt-get install zip -y
    # Check if installation was successful
    if [ $? -ne 0 ]
    then
        echo "Installation failed. Exiting."
        exit 1
    fi
fi

# Check if bin2 exists
if ! [ -e "bin2" ]
then
    # Compile bin2c.c if bin2 does not exist
    echo "Compiling bin2c"
    gcc -o bin2c main/BAS/tools/bin2c.c
    
    # Check if compilation was successful
    if [ $? -ne 0 ]
    then
        echo "Compilation failed. Exiting."
        exit 1
    fi
fi

export BIN2C=$PWD/bin2c

cd Lua-Examples || exit 1
zip -D -r -9 ../main/BAS/examples/xedge/obj/lsp.zip  .lua || exit 1
cd ../main/protobuf || exit 1
mkdir -p .lua
cp protoc.lua serpent.lua .lua/ || exit 1
zip -D -r -9 ../BAS/examples/xedge/obj/lsp.zip  .lua || exit 1
cd ../BAS/examples/xedge/obj/ || exit 1
echo "bin2c -z getLspZipReader lsp.zip LspZip.c"
$BIN2C -z getLspZipReader lsp.zip LspZip.c
