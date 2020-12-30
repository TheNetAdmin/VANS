#ifndef VANS_CONTROLLER_H
#define VANS_CONTROLLER_H

#include "component.h"
#include "mapping.h"
#include "tick.h"
#include <memory>

namespace vans
{

template <typename RequestType, typename ModelType> class controller : public tick_able
{
  public:
    std::shared_ptr<ModelType> local_memory_model;
    std::shared_ptr<dumper> counter_dumper;
    std::vector<std::shared_ptr<base_component>> next_level_components;

    controller() = default;

    /* issue_request: issue a new request to this controller */
    [[nodiscard]] virtual base_response issue_request(RequestType &request) = 0;

    /* drain: drain this controller to finish all on-going requests*/
    bool is_draining     = false;
    virtual void drain() = 0;

    /* pending: return true if there are requests still pending*/
    virtual bool pending() = 0;

    /* full: return true if this controller is full and cannot accept any request */
    virtual bool full() = 0;

    /* print_counters: print all counters to console */
    virtual void print_counters() {}
};

template <typename... Types> class memory_controller : public controller<Types...>
{
  public:
    component_mapping_f mapping_func;

    memory_controller() = delete;

    explicit memory_controller(const config &cfg)
    {
        this->mapping_func = get_component_mapping_func(cfg["component_mapping_func"]);
    }

    virtual void drain_current() = 0;

    virtual std::tuple<addr_t, std::shared_ptr<base_component>> get_next_level(addr_t addr)
    {
        auto [next_addr, component_index] = this->mapping_func(addr, this->next_level_components.size());
        return {next_addr, this->next_level_components[component_index]};
    }

    void drain() override
    {
        if (this->is_draining) {
            throw std::runtime_error("Internal error, should not call drain() twice.");
        }
        this->is_draining = true;

        this->drain_current();

        for (auto &n : this->next_level_components) {
            n->drain();
        }
    }

    virtual bool pending_current() = 0;

    bool pending() override
    {
        if (pending_current())
            return true;

        return std::any_of(this->next_level_components.begin(), this->next_level_components.end(), [](auto &n) {
            return n->pending();
        });
    }
};

template <typename... Types> class media_controller : public controller<Types...>
{
};

} // namespace vans

#endif // VANS_CONTROLLER_H
