#ifndef VANS_STATIC_MEMORY_H
#define VANS_STATIC_MEMORY_H


#include "config.h"
#include "controller.h"
#include "memory.h"
#include <assert.h>
#include <stdexcept>

namespace vans
{

class static_media
{
  public:
    void tick(clk_t curr_clk) {}
};

class static_media_controller : public media_controller<base_request, static_media>
{
  public:
    clk_t read_latency  = clk_invalid;
    clk_t write_latency = clk_invalid;

    static_media_controller() = delete;

    explicit static_media_controller(const config &cfg) :
        media_controller(), read_latency(cfg.get_ulong("read_latency")), write_latency(cfg.get_ulong("write_latency"))
    {
    }

    base_response issue_request(base_request &request) final
    {
        if (request.arrive == clk_invalid) {
            throw std::runtime_error("Internal error, this request's arrive clk is `clk_invalid`.");
        }
        switch (request.type) {
        case base_request_type::read:
            return {true, true, request.arrive + read_latency};
            break;
        case base_request_type::write:
            return {true, true, request.arrive + write_latency};
            break;
        }

        return {false, false, clk_invalid};
    }

    void drain() final {}

    bool pending() final
    {
        return false;
    }

    bool full() final
    {
        return false;
    }

    void tick(clk_t curr_clk) final {}
};

class static_memory : public memory<static_media_controller, static_media>
{
  public:
    static_memory() = delete;

    explicit static_memory(const config &cfg) : memory(cfg)
    {
        this->ctrl = std::make_shared<static_media_controller>(cfg);
    }

    void tick(clk_t curr_clk) final
    {
        this->ctrl->tick(curr_clk);
    }

    base_response issue_request(base_request &req) override
    {
        return this->ctrl->issue_request(req);
    }
};
} // namespace vans

#endif // VANS_STATIC_MEMORY_H
