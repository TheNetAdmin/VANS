#ifndef VANS_MAPPING_H
#define VANS_MAPPING_H

#include "utils.h"
#include <functional>

namespace vans
{

/* Map an address to a tuple of:
 *   1. address inside the component
 *   2. index of the component
 */
using component_mapping_f = std::function<std::tuple<addr_t, size_t>(addr_t, size_t)>;

static auto none_mapping = [](addr_t in_addr, size_t total_components) constexpr -> std::tuple<addr_t, size_t>
{
    return {in_addr, 0};
};

static auto stride_mapping_4096 = [](addr_t in_addr, size_t total_components) -> std::tuple<addr_t, size_t> {
    addr_t next_addr         = (((in_addr >> 12U) / total_components) << 12U) | (in_addr & 0xfff);
    size_t next_component_id = (in_addr >> 12U) % total_components;
    return {next_addr, next_component_id};
};

static component_mapping_f get_component_mapping_func(const std::string &mapping_func_name)
{
    if (mapping_func_name == "none_mapping") {
        return none_mapping;
    } else if (mapping_func_name == "stride_mapping(4096)") {
        return stride_mapping_4096;
    } else {
        throw std::runtime_error("Unknown dram_mapping function: " + mapping_func_name);
    }
}

/* Map address for DRAM media */
template <typename StandardType> class dram_mapping
{
  private:
    size_t size         = 0;
    size_t data_width   = 0;
    size_t channel      = 0;
    size_t rank         = 0;
    size_t bank_group   = 0;
    size_t bank         = 0;
    size_t row          = 0;
    size_t col          = 0;
    size_t total_levels = 0;

    size_t channel_width;
    size_t prefetch_size;

    size_t tx_bit_width = 0;
    std::vector<size_t> addr_bit_width;
    uint64_t code = 0;

  public:
    dram_mapping()                     = delete;
    dram_mapping(const dram_mapping &) = delete;
    explicit dram_mapping(const std::shared_ptr<StandardType> spec, const config &cfg) :
        total_levels(StandardType::total_levels),
        channel_width(spec->channel_width),
        prefetch_size(spec->prefetch_size),
        addr_bit_width(StandardType::total_levels)
    {
#define LOAD_FROM_CONFIG(key) this->key = cfg.get_ulong(#key);
        LOAD_FROM_CONFIG(size)
        LOAD_FROM_CONFIG(data_width)
        LOAD_FROM_CONFIG(channel)
        LOAD_FROM_CONFIG(rank)
        LOAD_FROM_CONFIG(bank_group)
        LOAD_FROM_CONFIG(bank)
        LOAD_FROM_CONFIG(row)
        LOAD_FROM_CONFIG(col)
#undef LOAD_FROM_CONFIG

        size_t tx_size = channel_width * prefetch_size / 8;
        if (!is_pwr_of_2(tx_size))
            throw std::runtime_error("Transaction size not supported (not power of 2)");
        tx_bit_width = log2(tx_size);

        const size_t *count = spec->count;
        for (int i = 0; i < total_levels; ++i) {
            addr_bit_width[i] = log2(count[i]);
        }
        addr_bit_width[total_levels - 1] -= log2(prefetch_size);

        // encode
        std::string mapping_func = cfg["media_mapping_func"];
        for (unsigned i = 0; i < total_levels; i++) {
            std::string this_level = mapping_func.substr(i * 2, 2);
            uint64_t level_num     = 0;
            if (this_level == "Ch")
                level_num = 0;
            else if (this_level == "Ra")
                level_num = 1;
            else if (this_level == "Bg")
                level_num = 2;
            else if (this_level == "Ba")
                level_num = 3;
            else if (this_level == "Ro")
                level_num = 4;
            else if (this_level == "Co")
                level_num = 5;
            this->code |= (level_num & 0xfU) << (i * 4);
        }
    }

    void map(dram::addr_type_t &addr) const
    {
        addr.mapped_addr.resize(total_levels);
        logic_addr_t logic_addr = addr.logic_addr;

        logic_addr >>= tx_bit_width;
        for (int i = int(total_levels) - 1; i >= 0; --i) {
            uint64_t level          = (this->code >> ((unsigned)i * 4)) & 0xfU;
            addr.mapped_addr[level] = logic_addr & ((1U << addr_bit_width[level]) - 1);
            logic_addr >>= addr_bit_width[level];
        }
    }

    void rev_map(dram::addr_type_t &addr)
    {
        throw std::runtime_error("dram_mapping: reverse dram_mapping not implemented yet");
    }
};

} // namespace vans

#endif // VANS_MAPPING_H
