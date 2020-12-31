# Setup GEM5 integration

0. Clone GEM5 code repo and checkout commit id `dde093b2`.
1. Run script `setup.sh` with GEM5 root path as argument. This script will link the VANS source code into GEM5
   directory.
2. Make the following modifications manually to finish the setup
    1. In `gem5/src/mem/SConscript`, add the following lines near `if env['HAVE_DRAMSIM]`:
    ```Python
    if env['HAVE_VANS']:
        SimObject('vans.py')
        Source('vans.cc')
        DebugFlag('vans')
    ```
    2. In `gem5/configs/common/Options.py`, find the function `addNoISAOptions` and add the following option
    ```Python
    parser.add_option("--vans-config-path", type="string",
                      dest="vans_config_path",
                      help="VANS config dir path")
    ```
    3. In `gem5/configs/common/MemConfig.py`, find the line `for r in system.mem_ranges:`, and then the line that looks
       like `mem_ctrl = create_mem_ctrl(cls, r, i, nbr_mem_ctrls, intlv_bits,` (this line may be different from your
       GEM5 code, depending on the GEM5 version, just look for the line that creates a dram controller or interface),
       and insert the code below:
    ```Python
    if issubclass(cls, m5.objects.VANS):
        opt_vans_config_path = getattr(options, "vans_config_path",
                                       None)
        if opt_vans_config_path is None:
            fatal("--mem-type=vans "
                  "require --vans-config-path option")
        mem_ctrl.config_path = opt_vans_config_path
    ```
    4. In `gem5/SConstruct`, find the line `main.Append(CCFLAGS=['-Werror',`, change it
       to `main.Append(CCFLAGS=['-Wall'])`. As VANS compilation triggers some compiler warnings that may block the
       compilation with `-Werror`. Or you can refer to `vans/src/gem5/patch/SConscript` to add several `-Wno-error=...`
       here.

## NOTE

1. This tutorial is provided based on GEM5 commit id [dde093b2](https://github.com/gem5/gem5/tree/dde093b2), the
   above-mentioned instructions may require some modifications according to your GEM5 version.
2. As of commit id [0d70304](https://github.com/gem5/gem5/tree/0d703041fcd5d119012b62287695723a2955b408), the GEM5
   supports `m5.objects.NVMInterface`, but we do not currently provide such integration for backward compatibility.
3. Some newer version of GEM5 (after [dde093b2](https://github.com/gem5/gem5/tree/dde093b2)) breaks a lot of old Python
   interfaces, making it unable to run with VANS GEM5 wrapper, although compilation is fine.
