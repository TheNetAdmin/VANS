#ifndef VANS_NVRAM_SYSTEM_H
#define VANS_NVRAM_SYSTEM_H


#include "component.h"
#include "controller.h"
#include "static_memory.h"

namespace vans::nvram_system
{
class nvram_system_controller : public memory_controller<vans::base_request, vans::static_memory>
{
  public:
    nvram_system_controller() = delete;
    explicit nvram_system_controller(const config &cfg) : memory_controller(cfg) {}

    base_response issue_request(base_request &request) override
    {
        auto [next_addr, next_component] = this->get_next_level(request.addr);
        request.addr                     = next_addr;
        return next_component->issue_request(request);
    }

    bool full() override
    {
        return std::any_of(
            this->next_level_components.begin(), this->next_level_components.end(), [](auto &n) { return n->full(); });
    }

    void drain_current() override {}

    bool pending_current() override
    {
        return false;
    }

    void tick(clk_t curr_clk) override {}
};

class nvram_system : public component<nvram_system_controller, static_memory>
{
  public:
    nvram_system() = delete;

    explicit nvram_system(const config &cfg)
    {
        this->ctrl = std::make_shared<nvram_system_controller>(cfg);
    }

    base_response issue_request(base_request &req) override
    {
        return this->ctrl->issue_request(req);
    }
};
} // namespace vans::nvram_system


#endif // VANS_NVRAM_SYSTEM_H
