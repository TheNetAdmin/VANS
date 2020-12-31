#ifndef VANS_IMC_H
#define VANS_IMC_H

#include "controller.h"
#include "request_queue.h"
#include "static_memory.h"
#include "common.h"

namespace vans::imc
{

class imc_controller : public memory_controller<vans::base_request, vans::static_memory>
{
  public:
    base_request_queue wpq;
    base_request_queue rpq;

    clk_t imc_curr_clk = 0;
    clk_t adr_epoch    = 0;

    imc_controller() = delete;

    explicit imc_controller(const vans::config &cfg) :
        memory_controller(cfg),
        wpq(cfg.get_ulong("wpq_entries")),
        rpq(cfg.get_ulong(("rpq_entries"))),
        adr_epoch(cfg.get_ulong("adr_epoch"))
    {
    }

    base_response issue_request(base_request &request) final;

    void drain_current() final{};

    bool pending_current() final
    {
        return wpq.pending() || rpq.pending();
    }

    void flush_wpq();
    void adr();

    /* imc_controller::full()
     *   This function returns true if both wpq and rpq are full.
     *   You should check the first return value of `imc_controller::issue_request` to test if either wpq/rpq is full.
     */
    bool full() final
    {
        return wpq.full() && rpq.full();
    }

    void tick(clk_t curr_clk) final;
};

class imc : public component<imc_controller, static_memory>
{
  public:
    imc() = delete;

    explicit imc(const config &cfg)
    {
        this->ctrl = std::make_shared<imc_controller>(cfg);
    }

    base_response issue_request(base_request &req) override
    {
        return this->ctrl->issue_request(req);
    }
};
} // namespace vans::imc

#endif // VANS_IMC_H
