#!/usr/bin/env bash
set -euo pipefail

sudo bash CI/install_debian_deps.sh gcc openmw-deps openmw-deps-dynamic
sudo apt-get update -yq
sudo apt-get install -y --no-install-recommends libluajit-5.1-dev ninja-build
