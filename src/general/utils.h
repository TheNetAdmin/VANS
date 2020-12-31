
#ifndef VANS_UTILS_H
#define VANS_UTILS_H

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "config.h"

namespace fs = std::filesystem;

namespace vans
{

using clk_t         = uint64_t;
using addr_t        = uint64_t;
using logic_addr_t  = uint64_t;
using addr_offset_t = uint64_t;

/* Compile time constants: https://stackoverflow.com/a/40405554 */
enum : clk_t { clk_invalid = std::numeric_limits<uint64_t>::max() };
enum : size_t { size_invalid = std::numeric_limits<size_t>::max() };
enum : addr_t { addr_invalid = std::numeric_limits<uint64_t>::max() };
enum : size_t { cpu_cl_size = 64, cpu_cl_bitshift = 6, /* Log2(CPU_CL_SIZE) --> Log2(64) */ };

namespace dram
{
using addr_t        = uint64_t *;
using mapped_addr_t = std::vector<uint64_t>;
using addr_type_t   = struct addr_type {
    logic_addr_t logic_addr;
    mapped_addr_t mapped_addr;

    explicit addr_type(mapped_addr_t mapped_addr) : mapped_addr(std::move(mapped_addr)) {}
    explicit addr_type(logic_addr_t logic_addr) : logic_addr(logic_addr) {}
};
} // namespace dram


static inline bool is_pwr_of_2(size_t n)
{
    return n != 0 && !(n & (n - 1));
}

static inline size_t log2(size_t val)
{
    size_t n = 0;
    while ((val >>= 1U))
        n++;
    return n;
}


/* Dumper */
class dumper
{
  private:
    std::ofstream dump_file;
    bool dump_to_file;
    bool dump_to_cli;

  public:
    enum class type { none, cli, file, both } dump_type;

    dumper()               = delete;
    dumper(const dumper &) = delete;
    dumper &operator=(const dumper &) = delete;

    dumper(dumper::type dump_type, const std::string &filename) :
        dump_type(dump_type),
        dump_to_file(dump_type == type::file || dump_type == type::both),
        dump_to_cli(dump_type == type::cli || dump_type == type::both)
    {
        if (0 == filename.compare(0, 4, "none")) {
            dump_to_cli  = false;
            dump_to_file = false;
        }
        if (dump_to_file) {
            fs::path filepath(filename);
            fs::create_directories(filepath.parent_path());
            dump_file.open(filename);
            if (!dump_file.good())
                throw std::runtime_error("cannot open dump file: " + filename);
        }
    }

    virtual ~dumper()
    {
        if (dump_to_file)
            dump_file.close();
    }

    void dump(const char *const msg, bool newline = true)
    {
        if (dump_to_cli) {
            std::cout << msg;
            if (newline)
                std::cout << std::endl;
        }
        if (dump_to_file) {
            dump_file << msg;
            if (newline)
                dump_file << std::endl;
        }
    }

    void dump(const std::string &str, bool newline = true)
    {
        this->dump(str.c_str(), newline);
    }
};

static dumper::type get_dump_type(const root_config &cfg)
{
    auto dump_cfg = cfg["dump"]["type"];
    if (dump_cfg == "file")
        return dumper::type::file;
    else if (dump_cfg == "cli")
        return dumper::type::cli;
    else if (dump_cfg == "both")
        return dumper::type::both;
    else if (dump_cfg == "none")
        return dumper::type::none;
    else
        throw std::runtime_error("[CONFIG ERROR]: dump_file value [" + cfg["dump"].get_string("type")
                                 + "] is illegal, should be [none|file|cli|both]");
}

static std::string get_dump_filename(const root_config &cfg, const std::string &name, unsigned id)
{
    std::string filename = cfg["dump"][name] + "_" + std::to_string(id);
    std::string path     = cfg["dump"]["path"];
    return path + "/" + filename;
}

/* Hardware counters */
class counter
{
  public:
    std::string domain;     /* e.g. RMW or AIT */
    std::string sub_domain; /* e.g. events or duration */
    std::map<std::string, size_t> counters;

    counter() = delete;
    counter(std::string domain, std::string sub_domain, const std::vector<std::string> &counter_names) :
        domain(std::move(domain)), sub_domain(std::move(sub_domain))
    {
        for (const auto &name : counter_names)
            this->counters[name] = 0;
    }

    void print(const std::shared_ptr<dumper> &d)
    {
        std::string prefix = "cnt." + domain + "." + sub_domain + ".";
        for (const auto &cnt : counters) {
            d->dump(prefix + cnt.first + ": " + std::to_string(cnt.second));
        }
    }

    size_t &operator[](const std::string &name)
    {
        return this->counters.at(name);
    }
};


/* RMW related utils, for 256 byte entries */
namespace rmw
{
enum : size_t {
    block_size_byte           = 256,
    block_size_byte_bitshift  = 8,
    block_offset_byte_bitmask = 0xff,
    block_size_cl             = block_size_byte / cpu_cl_size,
    block_hit_cl_bitmask      = 0xf /* block_size_cl==4 --> (1 << 3) | (1 << 2) | (1 << 1) | (1 << 0) */
};

using block_addr_t = addr_t;

static inline block_addr_t translate_to_block_addr(logic_addr_t logic_addr)
{
    return ((logic_addr) >> block_size_byte_bitshift) << block_size_byte_bitshift;
}

static inline addr_offset_t block_offset(logic_addr_t logic_addr)
{
    return (logic_addr & block_offset_byte_bitmask);
}

static inline addr_offset_t block_offset_cl(logic_addr_t logic_addr)
{
    return (block_offset(logic_addr) >> cpu_cl_bitshift);
}

static inline addr_offset_t block_bitshift_cl(logic_addr_t logic_addr)
{
    return (1U << block_offset_cl(logic_addr));
}
} // namespace rmw


/* AIT related utils, for 4096 byte entries */
namespace ait
{
enum : size_t {

    block_size_byte           = 4096,
    block_size_byte_bitshift  = 12,
    block_offset_byte_bitmask = 0xfff,
    block_size_cl             = 64,
    block_size_rmw            = 16,                /* ait::block_size_byte / rmw::block_size_byte */
    block_hit_cl_bitmask      = 0xffffffffffffffff /* block_size_cl==64 --> (1 << 63) | (1 << 62) | ... | (1 << 0) */
};

using block_addr_t = addr_t;

static inline block_addr_t translate_to_block_addr(logic_addr_t logic_addr)
{
    return ((logic_addr) >> block_size_byte_bitshift) << block_size_byte_bitshift;
}

static inline addr_offset_t block_offset(logic_addr_t logic_addr)
{
    return (logic_addr & block_offset_byte_bitmask);
}

static inline addr_offset_t block_offset_cl(logic_addr_t logic_addr)
{
    return (block_offset(logic_addr) >> cpu_cl_bitshift);
}

static inline addr_offset_t block_bitshift_cl(logic_addr_t logic_addr)
{
    return (1U << block_offset_cl(logic_addr));
}

static inline addr_offset_t block_offset_rmw(logic_addr_t logic_addr)
{
    return (block_offset(logic_addr) >> rmw::block_size_byte_bitshift);
}

static inline addr_offset_t block_bitshift_rmw(logic_addr_t logic_addr)
{
    return (1U << block_offset_rmw(logic_addr));
}

} // namespace ait

} // namespace vans

#endif // VANS_UTILS_H
