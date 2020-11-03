
#ifndef VANS_RMC_H
#define VANS_RMC_H

#include "component.h"
#include "controller.h"
#include "static_memory.h"

namespace vans::rmc
{
class rmc_controller : public memory_controller<vans::base_request, vans::static_memory>
{
  public:
    rmc_controller() = delete;
    explicit rmc_controller(const config &cfg) : memory_controller(cfg) {}

    base_response issue_request(base_request &request) override
    {
        auto next = this->get_next_level(request.addr);
        return next->issue_request(request);
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
