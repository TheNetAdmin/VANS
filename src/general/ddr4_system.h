#ifndef VANS_DDR4_SYSTEM_H
#define VANS_DDR4_SYSTEM_H

#include "component.h"
#include "controller.h"
#include "static_memory.h"

namespace vans::ddr4_system
{
class ddr4_system_controller : public memory_controller<vans::base_request, vans::dram::ddr::ddr4_memory>
{
  public:
    ddr4_system_controller() = delete;
    explicit ddr4_system_controller(const config &cfg, std::shared_ptr<vans::dram::ddr::ddr4_memory> memory) :
        memory_controller(cfg)
    {
        this->local_memory_model = std::move(memory);
    }

    base_response issue_request(base_request &request) override
    {
        return this->local_memory_model->issue_request(request);
    }

    bool full() override
    {
        return this->local_memory_model->full();
    }

    void drain_current() override
    {
        this->local_memory_model->drain();
    }

    bool pending_current() override
    {
        return this->local_memory_model->pending();
    }

    void tick(clk_t curr_clk) override
    {
        /* No need to tick local_memory_model here, the `component::tick_current()` will do it. */
    }
};

class ddr4_system : public component<ddr4_system_controller, dram::ddr::ddr4_memory>
{
  public:
    ddr4_system() = delete;

    explicit ddr4_system(const config &cfg)
    {
        this->memory_component = std::make_shared<vans::dram::ddr::ddr4_memory>(cfg);
        this->ctrl             = std::make_shared<ddr4_system_controller>(cfg, this->memory_component);
    }

    base_response issue_request(base_request &req) override
    {
        return this->ctrl->issue_request(req);
    }
};
} // namespace vans::ddr4_system

#endif // VANS_DDR4_SYSTEM_H
