# VANS: A Validated Heterogeneous Memory Simulator

VANS is a cycle-level Heterogeneous Memory simulator. Its performance is initially calibrated to match the Intel Optane Persistent
Memory. But you can reconfigure VANS to model other systems.

VANS is part of our [MICRO 2020 paper](https://github.com/TheNetAdmin/LENS-VANS).

## Usage

VANS is written in C++ 17, so it requires gcc 7.0 / clang 8.0 or newer version. VANS is developed and tested with gcc 10.2.0.

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
# Create a new directory for VANS output, or VANS will create it for you
# See configuration file 'dump' section 'path' value
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

1. Memory mode


## Bibliography

```bibtex
@inproceedings{LENS-VANS,
  author={Zixuan Wang and Xiao Liu and Jian Yang and Theodore Michailidis and Steven Swanson and Jishen Zhao},
  booktitle={2020 53rd Annual IEEE/ACM International Symposium on Microarchitecture (MICRO)},
  title={Characterizing and Modeling Non-Volatile Memory Systems},
  year={2020},
  pages={496-508},
  doi={10.1109/MICRO50266.2020.00049}
}
```

## License

[![](https://img.shields.io/github/license/TheNetAdmin/VANS)](LICENSE)
