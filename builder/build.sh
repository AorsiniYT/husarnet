#!/bin/bash
source $(dirname "$0")/../util/bash-base.sh

echo "[HUSARNET BS] Building the builder"

pushd ${base_dir}/builder
DOCKER_BUILDKIT=0 docker build -t ghcr.io/husarnet/husarnet:builder .
popd

if [ "$1" = "windows" ]; then
    echo "[HUSARNET BS] Building Windows binaries"
    docker run --rm -it --privileged --user 0:0 --volume ${base_dir}:/app --entrypoint /app/platforms/windows/build.sh ghcr.io/husarnet/husarnet:builder nightly
elif [ "$1" = "linux" ]; then
    echo "[HUSARNET BS] Building Linux binaries"
    docker run --rm -it --privileged --user 0:0 --volume ${base_dir}:/app --entrypoint /app/platforms/linux/build.sh ghcr.io/husarnet/husarnet:builder amd64 nightly
else
    echo "To inspect the image manually run:"
    echo './builder/shell.sh'
    echo "To build Windows: ./builder/build.sh windows"
    echo "To build Linux: ./builder/build.sh linux"
fi
