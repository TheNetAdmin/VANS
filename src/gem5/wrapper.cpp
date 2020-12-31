#include "wrapper.h"
#include <cstdio>

size_t vans::gem5_wrapper::vans_cnt = 0;

namespace vans
{

gem5_wrapper::gem5_wrapper(const std::string cfg_path)
{
    this->vans_id = vans_cnt;
    vans_cnt++;

    std::string cfg_fname = cfg_path + "/";
    if (vans_id == 0) {
        cfg_fname += "gem5_lo_memory.cfg";
    } else if (vans_id == 1) {
        cfg_fname += "gem5_hi_memory.cfg";
    } else {
        assert(false);
    }

    auto cfg     = vans::root_config(cfg_fname);
    printf("Init VANS id(%d) with cfg(%s)\n", vans_id, cfg_fname);
    this->memory = vans::factory::make(cfg);
    this->tCK    = std::stod(cfg["basic"].get_string("tCK"));
}

void gem5_wrapper::tick()
{
    this->memory->tick(this->curr_clk);
    this->curr_clk++;
}

bool gem5_wrapper::send(vans::base_request req)
{
    auto resp = this->memory->issue_request(req);
    return std::get<0>(resp);
}

void gem5_wrapper::finish()
{
    this->memory->drain();
    this->memory->print_counters();
}

} // namespace vans
