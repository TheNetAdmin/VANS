
#ifndef VANS_RMW_H
#define VANS_RMW_H

#include "buffer.h"
#include "component.h"
#include "config.h"
#include "controller.h"
#include "memory.h"
#include "request_queue.h"
#include "static_memory.h"
#include "utils.h"

#include <bitset>
#include <cassert>
#include <deque>
#include <functional>
#include <stdexcept>
#include <utility>

namespace vans::rmw
{

enum class request_type { write_rmw, write_comb, write_patch, read_cold, read_ff, flush_back, total };

enum class request_state {
    init = 0,
    pending_read,
    pending_modify,
    pending_write,
    pending_readout,
    pending_ait_r,
    pending_ait_w,
    end,
    total
};

struct request {
    request_type type;
    logic_addr_t logic_addr;
    clk_t arrive = clk_invalid;

    request() = delete;

    request(request_type type, logic_addr_t logic_addr, clk_t arrive) :
        type(type), logic_addr(logic_addr), arrive(arrive)
    {
    }

    void assign(request_type new_type, logic_addr_t new_addr, clk_t new_arrive)
    {
        this->type       = new_type;
        this->logic_addr = new_addr;
        this->arrive     = new_arrive;
    }
};

struct buffer_entry {
    using bitmap_t = std::bitset<block_size_cl>;

    clk_t last_used_clk   = clk_invalid;
    clk_t next_action_clk = clk_invalid;

    size_t buffer_index = 0;

    /* Default initialization for bitfields is a C++ 20 feature */
    bool pending                   : 1;
    bool waiting_action_clk_update : 1; /* Set by state transfer, reset by cpq request */
    bool valid_to_read             : 1;
    bool dirty                     : 1;

    /* Bitmap for cpu cache line sized data/requests */
    bitmap_t cl_bitmap;

    /* For callback functions */
    using callback_f = vans::base_callback_f;
    std::array<callback_f, block_size_cl> callbacks{nullptr};
    bitmap_t cb_bitmap;

    /* Pending requests */
    request pending_request;
    request_state state = request_state::init;
    std::deque<unsigned> pending_request_cl_index;

    /* Methods */
    buffer_entry() = delete;

    buffer_entry(clk_t curr_clk, request_type type, logic_addr_t logic_addr, unsigned cacheline_bitmap) :
        last_used_clk(curr_clk),
        pending(true),
        waiting_action_clk_update(true),
        valid_to_read(false),
        dirty(false),
        pending_request(type, logic_addr, curr_clk),
        cl_bitmap(cacheline_bitmap),
        state(request_state::init)
    {
    }

    void assign_new_request(clk_t curr_clk, request_type type, logic_addr_t logic_addr, unsigned cacheline_bitmap)
    {
        this->pending                   = true;
        this->waiting_action_clk_update = true;
        this->valid_to_read             = false;
        this->cl_bitmap                 = cacheline_bitmap;
        this->state                     = request_state::init;
        this->pending_request.assign(type, logic_addr, curr_clk);
    }

    void assign_callback(unsigned cl_index, callback_f callback)
    {
        this->callbacks.at(cl_index) = std::move(callback);
        this->pending_request_cl_index.push_back(cl_index);
        if (this->pending_request_cl_index.size() > block_size_cl) {
            throw std::runtime_error(
                "Internal error: the `pending_request_cl_index` queue overflows, maybe there's a bug in your "
                "controller that issues more than RMW_BLK_SIZE_CL requests to the same rmw rmw entry, or "
                "`pending_request_cl_index` is not reset properly");
        }
    }

    void reset_callback()
    {
        this->cb_bitmap = 0;
        for (auto &cb : this->callbacks) {
            cb = nullptr;
        }
        if (!pending_request_cl_index.empty()) {
            throw std::runtime_error("Internal error: reset rmw entry while there are requests waiting to be served");
        }
    }

    [[maybe_unused]] [[nodiscard]] std::string to_string() const
    {
        std::string str;
        str += "addr: " + std::to_string(pending_request.logic_addr) + "\n";
        str += "type: " + std::to_string(int(pending_request.type)) + "\n";
        str += "next_action_clk: " + std::to_string(next_action_clk) + "\n";
        return str;
    }
};

class rmw_controller : public memory_controller<vans::base_request, static_memory>
{
  public:
    internal_buffer<block_addr_t, buffer_entry, translate_to_block_addr, clk_t, request_type, logic_addr_t, unsigned>
        buffer;

    struct {
        clk_t readout_latency    = 0;
        clk_t modify_latency     = 0;
        clk_t ait_to_rmw_latency = 0;
        clk_t rmw_to_ait_latency = 0;
        clk_t wpq_ff_latency     = 0;
    } timing;

    base_request_queue lsq; /* lsq: Load/Store queue*/
    base_request_queue roq; /* roq: Read out queue  */

    logic_addr_t start_addr = 0;

    bool evicting = false;

    vans::counter cnt_events{"rmw",
                             "events",
                             {
                                 "read_access",
                                 "write_access",
                                 "eviction",
                                 "write_rmw",
                                 "write_comb",
                                 "write_patch",
                                 "flush_back",
                                 "read_patch",
                                 "read_fast_forward",
                                 "read_cold",
                                 "patch_rmw",
                                 "patch_rmw_comb",
                                 "next_level_full",
                                 "roq_full",
                                 "next_level_issue_fail",
                                 "local_memory_issue_fail",
                             }};

    vans::counter cnt_duration{"rmw",
                               "state_duration",
                               {
                                   "w_rmw_par",
                                   "w_rmw_pr",
                                   "w_rmw_paw",
                                   "w_rmw_pm",
                                   "w_rmw_pw",
                                   "w_comb_paw",
                                   "w_comb_pm",
                                   "w_comb_pw",
                                   "w_patch_paw",
                                   "w_patch_pm",
                                   "w_patch_pw",
                                   "w_flush_paw",
                                   "w_flush_pw",
                                   "r_cold_par",
                                   "r_cold_pr",
                                   "r_cold_pro",
                                   "r_ff_pro",
                               }};

  public:
    using state_trans_f = std::function<void(const block_addr_t block_addr, buffer_entry &entry, clk_t curr_clk)>;
    state_trans_f state_trans[int(request_type::total)][int(request_state::total)];

    buffer_entry::callback_f next_level_read_callback = [this](addr_t addr, clk_t curr_clk) {
        block_addr_t rmw_addr                                         = translate_to_block_addr(addr);
        this->buffer.entry_map.at(rmw_addr).waiting_action_clk_update = false;
        this->buffer.entry_map.at(rmw_addr).next_action_clk           = curr_clk + 1;
    };

  private:
    void init_state_trans_table();

  public:
    rmw_controller() = delete;
    explicit rmw_controller(const vans::config &cfg, std::shared_ptr<static_memory> memory) :
        memory_controller(cfg),
        buffer(cfg.get_value("buffer_entries")),
        lsq(cfg.get_value("lsq_entries")),
        roq(cfg.get_value("roq_entries"))
    {
        this->init_state_trans_table();
        this->local_memory_model = std::move(memory);

        this->timing.ait_to_rmw_latency = cfg.get_value("ait_to_rmw_latency");
        this->timing.rmw_to_ait_latency = cfg.get_value("rmw_to_ait_latency");
    }

    bool check_and_evict();

    base_response issue_request(base_request &req) final;

    /* rmw::rmw_controller::drain()
     *   Call once and then tick. This function mark all the dirty rmw entries to be flushed. */
    void drain_current() final;

    void tick(clk_t curr_clk) final;

    bool pending_current() final
    {
        return roq.pending() || buffer.pending();
    }

    bool full() final
    {
        return lsq.full();
    }

    void print_counters() final
    {
        this->cnt_events.print(this->counter_dumper);
        this->cnt_duration.print(this->counter_dumper);
    }

  private:
    void tick_roq(clk_t curr_clk);
    void tick_lsq(clk_t curr_clk);
    void tick_lsq_read(clk_t curr_clk);
    void tick_lsq_write(clk_t curr_clk);
    void tick_internal_buffer(clk_t curr_clk);
};

class rmw : public component<rmw_controller, vans::static_memory>
{
  public:
    rmw() = delete;

    explicit rmw(const config &cfg) : component(cfg)
    {
        this->memory_component = std::make_shared<static_memory>(cfg);
        this->ctrl             = std::make_shared<rmw_controller>(cfg, this->memory_component);
    }

    base_response issue_request(base_request &req) final
    {
        return this->ctrl->issue_request(req);
    }
};
} // namespace vans::rmw


#endif // VANS_RMW_H
