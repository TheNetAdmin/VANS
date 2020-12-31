# Setup GEM5 integration

1. Run script `setup.sh` with GEM5 root path as argument. This script will link the VANS source code into GEM5 directory.
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
    3. In `gem5/configs/common/MemConfig.py`, find the line `for r in system.mem_ranges:`, and then the line that looks like `mem_ctrl = create_mem_ctrl(cls, r, i, nbr_mem_ctrls, intlv_bits,` (this line may be different from your GEM5 code, depending on the GEM5 version, just look for the line that creates a dram controller or interface), and insert the code below:
    ```Python
    if issubclass(cls, m5.objects.VANS):
        opt_vans_config_path = getattr(options, "vans_config_path",
                                       None)
        if opt_vans_config_path is None:
            fatal("--mem-type=vans "
                  "require --vans-config-path option")
        mem_ctrl.config_path = opt_vans_config_path
    ```
    4. In `gem5/SConstruct`, find the line `main.Append(CXXFLAGS=['-std=c++11'])`, change it to `main.Append(CXXFLAGS=['-std=c++17'])` as VANS use C++ 17 features.
    5. In `gem5/SConstruct`, find the line `main.Append(CCFLAGS=['-Werror',`, change it to `main.Append(CCFLAGS=['-Wall'])` to NOT treat warnings as error. As VANS compilation triggers some warnings that may block the compilation with `-Werror`. Or you can refer to `vans/src/gem5/patch/SConscript` to add several `-Wno-error=...` here.
3. Compile GEM5 **WITHOUT** Ruby as follows:
    ```shell
    # Disable Ruby with PROTOCOL='None'
    $ scons PROTOCOL='None' build/X86/gem5.opt -j8
    ```
    This is because Ruby source code uses operator `<<` defined in `src/base/stl_helpers.hh`, which is not compilable with C++17 and causes errors like:
    ```
    In file included from build/X86/base/cprintf.hh:38,
                    from build/X86/base/trace.hh:37,
                    from build/X86/mem/ruby/network/MessageBuffer.hh:57,
                    from build/X86/mem/ruby/network/MessageBuffer.cc:41:
    build/X86/base/cprintf_formats.hh:227:17: error: ambiguous overload for 'operator<<' (operand types are 'std::ostream' {aka 'std::basic_ostream<char>'} and 'const std::__cxx11::basic_string<char>')
    227 |             out << data;
        |             ~~~~^~~~~~~
    In file included from /usr/include/c++/10.2.0/string:55,
                    from /usr/include/c++/10.2.0/bits/locale_classes.h:40,
                    from /usr/include/c++/10.2.0/bits/ios_base.h:41,
                    from /usr/include/c++/10.2.0/ios:42,
                    from /usr/include/c++/10.2.0/ostream:38,
                    from /usr/include/c++/10.2.0/iostream:39,
                    from build/X86/mem/ruby/network/MessageBuffer.hh:52,
                    from build/X86/mem/ruby/network/MessageBuffer.cc:41:
    /usr/include/c++/10.2.0/bits/basic_string.h:6458:5: note: candidate: 'std::basic_ostream<_CharT, _Traits>& std::operator<<(std::basic_ostream<_CharT, _Traits>&, const std::__cxx11::basic_string<_CharT, _Traits, _Allocator>&) [with _CharT = char; _Traits = std::char_traits<char>; _Alloc = std::allocator<char>]'
    6458 |     operator<<(basic_ostream<_CharT, _Traits>& __os,
        |     ^~~~~~~~
    In file included from build/X86/mem/ruby/network/MessageBuffer.cc:48:
    build/X86/base/stl_helpers.hh:77:1: note: candidate: 'std::ostream& m5::stl_helpers::operator<<(std::ostream&, const C<T, A>&) [with C = std::__cxx11::basic_string; T = char; A = std::char_traits<char>; std::ostream = std::basic_ostream<char>]'
    77 | operator<<(std::ostream& out, const C<T,A> &vec)
        | ^~~~~~~~
    ```
## NOTE

1. This tutorial is provided based on GEM5 commit id [dde093b2](https://github.com/gem5/gem5/tree/dde093b2), the above-mentioned instructions may require some modifications according to your GEM5 version.
2. As of commit id [0d70304](https://github.com/gem5/gem5/tree/0d703041fcd5d119012b62287695723a2955b408), the GEM5 supports `m5.objects.NVMInterface`, but we do not currently provide such integration for backward compatibility.
3. Some newer version of GEM5 breaks a lot of old Python interfaces, making it unable to run with VANS GEM5 wrapper, although compiling is fine.
