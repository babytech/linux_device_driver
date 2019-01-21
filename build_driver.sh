#!/bin/sh

# this script targets to build a module in repo: ../linux_device_driver
DRIVER_NAME=$1
BUILD_OPITION=$2

DRIVER_DIR=$(realpath $(dirname $0)/$DRIVER_NAME)

if [ x"$DRIVER_NAME" = "x" -o ! -d "$DRIVER_DIR" ]; then
    echo "Usage: $(basename $0) <driver-name>"
    exit 1
fi

. ./env.conf

export ENV_CC_COMPILE="$CC_COMPILE"
export ENV_KERNEL_DIR="$KERNEL_DIR"
export ENV_DRIVER_NAME="$DRIVER_NAME"
export ENV_DRIVER_DIR="$DRIVER_DIR"

if [ "$BUILD_OPITION" = "clean" ]; then
    make -f ./Makefile.mak clean
    exit 1
fi
if [ "$BUILD_OPITION" = "all" ]; then
    make -f ./Makefile.mak all
    exit 1
fi
