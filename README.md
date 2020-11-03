# VANS: Validated cycle-Accurate Non-volatile memory Simulator

VANS is a cycle-level NVRAM simulator. Its performance is initially calibrated to match the Intel Optane Persistent
Memory. But you can reconfigure VANS to model other NVRAM systems.

VANS is part of our [MICRO 2020 paper](https://cseweb.ucsd.edu/~ziw002/files/micro20-lens-vans.pdf).

## Usage

VANS is written in C++ 17, so it requires gcc 7.0 / clang 8.0 or newer version.

To configure and build VANS:

```shell
$ mkdir build
$ cd build
$ cmake ..
$ make -j
```

This will compile VANS and generate the binaries in `bin` dir.

To run VANS with some sample traces:

```shell
$ cd bin
# Create a new directory for VANS output
# VANS binary does not generate this dir automatically
$ mkdir vans_dump
# Read config file and execute a trace
$ ./vans -c ../config/vans.cfg -t ../tests/sample_traces/read.trace
```

We also provide a set of automated tests (please read `tests/precision/README.md` to setup the environments before you
run these tests):

```shell
$ python3 tests/precision/precision_test.py tests/precision
```

## Issues

We heavily refactor and rewrite the entire VANS code. These features are currently missing/not-tested, and we will add them soon:

1. GEM5 integration
2. Multi-DIMM interleaving
3. Memory mode

## License

![](https://img.shields.io/github/license/TheNetAdmin/VANS)
