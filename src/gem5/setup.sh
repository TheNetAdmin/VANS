#!/bin/bash

gem5_root_path=$1

if ! [ -d $gem5_root_path ]; then
    echo "$0 gem5_root_path"
fi

curr_path=$(dirname $(realpath $0))
vans_root=$(realpath $curr_path/../../)

echo "[Step 1] Linking VANS source code into gem5"
mkdir -p $gem5_root_path/ext/vans
ln -s $vans_root                           $gem5_root_path/ext/vans/vans
ln -s $vans_root/src/gem5/patch/SConscript $gem5_root_path/ext/vans/SConscript
ln -s $vans_root/src/gem5/patch/vans.cc    $gem5_root_path/src/mem/vans.cc
ln -s $vans_root/src/gem5/patch/vans.hh    $gem5_root_path/src/mem/vans.hh
ln -s $vans_root/src/gem5/patch/vans.py    $gem5_root_path/src/mem/vans.py
echo "[Step 1] Done"
echo "[Step 2] Read the README.md and finish the gem5 code modification yourself"
echo "[Step 2] Need user's manual modifications"
