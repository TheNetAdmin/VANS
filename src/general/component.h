#ifndef VANS_COMPONENT_H
#define VANS_COMPONENT_H

#include "common.h"
#include "config.h"
#include "request_queue.h"
#include "tick.h"
#include <memory>
#include <vector>

namespace vans
{

class base_component : public tick_able
{
  public:
    std::vector<std::shared_ptr<base_component>> next;
    std::shared_ptr<dumper> stat_dumper = nullptr;
    size_t id                           = 0;

    base_component() = default;

    virtual ~base_component() = default;

    virtual void tick_current(clk_t curr_clk) = 0;

    virtual void tick_next(clk_t curr_clk)
    {
        for (auto &n : next)
            n->tick(curr_clk);
    }

    void tick(clk_t curr_clk) override
    {
        tick_current(curr_clk);
        tick_next(curr_clk);
    }

    void assign_id(size_t new_id)
    {
        this->id = new_id;
    }

    virtual void connect_next(const std::shared_ptr<base_component> &nc) = 0;

    virtual void connect_dumper(std::shared_ptr<dumper> dumper) = 0;

    virtual void print_counters() = 0;

    virtual base_response issue_request(base_request &req) = 0;

    virtual bool full() = 0;

    virtual bool pending() = 0;

    virtual void drain() = 0;
};

template <typename MemoryControllerType, typename MemoryType> class component : public base_component
{
  public:
    std::shared_ptr<MemoryControllerType> ctrl;
    std::shared_ptr<MemoryType> memory_component;

    component() = default;

    explicit component(const config &cfg) {}

    virtual ~component() = default;

    void tick_current(clk_t curr_clk) override
    {
        this->ctrl->tick(curr_clk);
        if (this->memory_component)
            this->memory_component->tick(curr_clk);
    }

    void connect_dumper(std::shared_ptr<dumper> dumper) override
    {
        this->stat_dumper          = dumper;
        this->ctrl->counter_dumper = dumper;
        if (this->stat_dumper != nullptr && !this->next.empty()) {
            if (this->next.size() == 1) {
                this->next[0]->connect_dumper(dumper);
            } else {
                throw std::runtime_error("Internal error, does not support one-to-many stat_dumper connection");
            }
        }
    }

    bool full() override
    {
        return this->ctrl->full();
    }

    bool pending() override
    {
        return this->ctrl->pending();
    }

    void drain() override
    {
        this->ctrl->drain();
    }

    void connect_next(const std::shared_ptr<base_component> &nc) override
    {
        this->next.push_back(nc);
        this->ctrl->next_level_components.push_back(nc);
    }

    void print_counters() override
    {
        this->ctrl->print_counters();
        for (auto &next : this->next) {
            next->print_counters();
        }
    }
};

template <typename... Types> class memory : public component<Types...>
{
  public:
    virtual ~memory() = default;

    explicit memory(const config &cfg) : component<Types...>(cfg) {}

    void connect_next() = delete;

    void tick_next() = delete;
};

} // namespace vans

#endif // VANS_COMPONENT_H
