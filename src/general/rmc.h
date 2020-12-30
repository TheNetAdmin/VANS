
#ifndef VANS_RMC_H
#define VANS_RMC_H

#include "component.h"
#include "controller.h"
#include "static_memory.h"

namespace vans::rmc
{
class rmc_controller : public memory_controller<vans::base_request, vans::static_memory>
{
    addr_t start_addr;

  public:
    rmc_controller() = delete;
    explicit rmc_controller(const config &cfg) :
        memory_controller(cfg)
    {
        if(cfg.check("start_addr")) {
            this->start_addr = cfg.get_ulong("start_addr");
        }
    }

    base_response issue_request(base_request &request) override
    {
        if (request.addr < this->start_addr) {
            throw std::runtime_error("Internal error, incoming address " + std::to_string(request.addr)
                                     + " is lower than rmc's start address " + std::to_string(this->start_addr));
        }
        request.addr -= this->start_addr;
        auto [next_addr, next_component] = this->get_next_level(request.addr);
        request.addr                     = next_addr;
        return next_component->issue_request(request);
    }

    bool full() override
    {
        throw std::runtime_error("Internal error, function not supposed to be invoked.");
    }

    void drain_current() override {}

    bool pending_current() override
    {
        return false;
    }

    void tick(clk_t curr_clk) override {}
};

class rmc : public component<rmc_controller, static_memory>
{
  public:
    rmc() = delete;

    explicit rmc(const config &cfg)
    {
        this->ctrl = std::make_shared<rmc_controller>(cfg);
    }

    base_response issue_request(base_request &req) override
    {
        return this->ctrl->issue_request(req);
    }
};
} // namespace vans::rmc

#endif // VANS_RMC_H
