#ifndef VANS_AIT_H
#define VANS_AIT_H

#include "buffer.h"
#include "ddr4.h"
#include "dram_memory.h"
#include "request_queue.h"
#include "static_memory.h"
#include "utils.h"
#include <bitset>

namespace vans::ait
{

enum class request_type { read_miss, read_hit, write_miss, write_hit, total };

enum class request_state {
    init = 0,
    pending_read_media,
    pending_write_media,
    pending_read_dram,
    pending_write_dram,
    pending_migration,
    end,
    total
};

struct request {
    request_type type;
    vans::rmw::block_addr_t rmw_block_addr;
    clk_t arrive = clk_invalid;

    request() = delete;

    request(request_type type, vans::rmw::block_addr_t rmw_block_addr, clk_t arrive) :
        type(type), rmw_block_addr(rmw_block_addr), arrive(arrive)
    {
    }

    void assign(request_type new_type, vans::rmw::block_addr_t new_addr, clk_t new_arrive)
    {
        this->type           = new_type;
        this->rmw_block_addr = new_addr;
        this->arrive         = new_arrive;
    }
};

struct buffer_entry {
    using cl_bitmap_t  = std::bitset<block_size_cl>;
    using rmw_bitmap_t = std::bitset<block_size_rmw>;

    clk_t last_used_clk   = clk_invalid;
    clk_t next_action_clk = clk_invalid;

    size_t buffer_index = 0;

    /* Default initialization for bitfields is a C++ 20 feature */
    bool pending                   : 1;
    bool waiting_action_clk_update : 1; /* Set by state transfer, reset by cpq request */
    bool valid_to_read             : 1;
    bool dirty                     : 1;

    /* Bitmap for rmw block sized data/requests */
    rmw_bitmap_t rmw_bitmap;

    /* For callback functions */
    using callback_f = vans::base_callback_f;
    callback_f cb    = nullptr;

    /* Pending requests */
    request pending_request;
    request_state state = request_state::init;

    /* Methods */
    buffer_entry() = delete;

    buffer_entry(clk_t curr_clk, request_type type, logic_addr_t logic_addr, unsigned rmw_block_bitmap) :
        last_used_clk(curr_clk),
        pending(true),
        waiting_action_clk_update(true),
        valid_to_read(false),
        dirty(false),
        pending_request(type, logic_addr, curr_clk),
        rmw_bitmap(rmw_block_bitmap),
        state(request_state::init)
    {
        static_assert((sizeof(unsigned) * 8) >= block_size_rmw,
                      "ait::block_size_rmw exceeds sizeof(unsigned), change to a larger type.");
    }

    void assign_new_request(clk_t curr_clk, request_type type, logic_addr_t logic_addr, unsigned rmw_block_bitmap)
    {
        if (this->pending) {
            throw std::runtime_error("Internal error, assigning new request to a pending request.");
        }
        this->pending                   = true;
        this->waiting_action_clk_update = true;
        this->valid_to_read             = false;
        this->rmw_bitmap                = rmw_block_bitmap;
        this->state                     = request_state::init;
        this->pending_request.assign(type, logic_addr, curr_clk);
    }

    void assign_callback(callback_f callback)
    {
        this->cb = std::move(callback);
    }

    void reset_callback()
    {
        this->cb = nullptr;
    }
};

struct table_entry {
    size_t write_cnt;
    table_entry() : write_cnt(0) {}
};

struct indirection_table {
    std::unordered_map<block_addr_t, table_entry> table;

    size_t wear_leveling_threshold;
    size_t migration_block_entries;

    clk_t migration_latency;

    indirection_table() = delete;

    explicit indirection_table(const config &cfg) :
        wear_leveling_threshold(cfg.get_ulong("wear_leveling_threshold")),
        migration_block_entries(cfg.get_ulong("migration_block_entries")),
        migration_latency(cfg.get_ulong("migration_latency"))
    {
        table.reserve(cfg.get_ulong("min_table_entries"));
    }

    void record_write(rmw::block_addr_t rmw_block_addr)
    {
        auto ait_block_addr = translate_to_block_addr(rmw_block_addr);
        table[ait_block_addr].write_cnt += 1;
    }


    /* Return 0 if no need to migrate data
     * Return latency in clk if need migration
     */
    clk_t check_wear_leveling(block_addr_t addr)
    {
        clk_t total_latency = 0;
        if ((table[addr].write_cnt + 1) % wear_leveling_threshold == 0) {
            total_latency = migration_block_entries * migration_latency;
        }
        return total_latency;
    }
};

class ait_controller : public memory_controller<vans::base_request, vans::dram::ddr::ddr4_memory>
{
  public:
    internal_buffer<block_addr_t,
                    buffer_entry,
                    translate_to_block_addr,
                    clk_t,
                    request_type,
                    rmw::block_addr_t,
                    unsigned>
        buffer;
    indirection_table table;

    base_request_queue lsq;   /* lsq: incoming load/store request queue */
    base_request_queue lmemq; /* lmemq: requests for local memory */
    struct lmemq_state_t {
        int subreq_cnt           = 4;
        int subreq_pending_index = -1;
        bool subreq_served[4]    = {false};
        bool pending_front       = false;
    } lmemq_state;

    bool evicting = false;

    vans::counter cnt_events{"ait",
                             "events",
                             {
                                 "read_access",
                                 "write_access",
                                 "eviction",
                                 "migration",
                                 "read_miss",
                                 "read_hit",
                                 "write_miss",
                                 "write_hit",
                                 "lmem_read_access",
                                 "lmem_write_access",
                                 "local_memory_issue_fail",
                             }};

    vans::counter cnt_duration{"ait",
                               "state_duration",
                               {
                                   "w_miss_prm", /* Write Miss Pending Read Media  */
                                   "w_miss_pwd", /* Write Miss Pending Write Dram  */
                                   "w_miss_pm",  /* Write Miss Pending Migration   */
                                   "w_miss_pwm", /* Write Miss Pending Write Media */
                                   "w_hit_pwd",  /* Write Hit Pending Write Dram   */
                                   "w_hit_pm",   /* Write Hit Pending Migration    */
                                   "w_hit_pwm",  /* Write Hit Pending Write Media  */
                                   "r_miss_prm", /* Read Miss Pending Read Media   */
                                   "r_miss_prd", /* Read Miss Pending Read Dram    */
                                   "r_hit_prd",  /* Read Hit Pending Read Dram     */
                               }};

  public:
    using state_trans_f = std::function<void(const block_addr_t block_addr, buffer_entry &entry, clk_t curr_clk)>;
    state_trans_f state_trans[int(request_type::total)][int(request_state::total)];

    buffer_entry::callback_f next_level_read_callback = [this](addr_t addr, clk_t curr_clk) {
        block_addr_t ait_addr                                         = translate_to_block_addr(addr);
        this->buffer.entry_map.at(ait_addr).waiting_action_clk_update = false;
        this->buffer.entry_map.at(ait_addr).next_action_clk           = curr_clk + 1;
    };

  private:
    void init_state_trans_table();

  public:
    ait_controller() = delete;
    explicit ait_controller(const config &cfg, std::shared_ptr<vans::dram::ddr::ddr4_memory> memory) :
        memory_controller(cfg),
        lsq(cfg.get_ulong("lsq_entries")),
        lmemq(cfg.get_ulong("lmemq_entries")),
        buffer(cfg.get_ulong("buffer_entries")),
        table(cfg)
    {
        static_assert(rmw::block_size_byte == 256, "Only support 256B rmw buffer block for now.");
        static_assert(ait::block_size_byte == 4096, "Only support 4096B ait buffer block for now.");

        this->init_state_trans_table();
        this->local_memory_model = std::move(memory);
    }

    base_response issue_request(base_request &request) override
    {
        bool issued = lsq.enqueue(request);
        return {(issued), false, clk_invalid};
    }

    bool check_and_evict();

    void drain_current() final;

    void tick(clk_t curr_clk) override;

    bool pending_current() override
    {
        return lsq.pending() || buffer.pending() || lmemq.pending();
    }

    bool full() override
    {
        return lsq.full();
    }

    void print_counters() final
    {
        this->cnt_events.print(this->counter_dumper);
        this->cnt_duration.print(this->counter_dumper);
    }

  private:
    void tick_lsq(clk_t curr_clk);
    void tick_lsq_read(clk_t curr_clk);
    void tick_lsq_write(clk_t curr_clk);
    void tick_lmemq(clk_t curr_clk);
    void tick_internal_buffer(clk_t curr_clk);
};

class ait : public component<ait_controller, vans::dram::ddr::ddr4_memory>
{
  public:
    ait() = delete;
    explicit ait(const config &cfg) : component(cfg)
    {
        this->memory_component = std::make_shared<vans::dram::ddr::ddr4_memory>(cfg);
        this->ctrl             = std::make_shared<ait_controller>(cfg, this->memory_component);
    }
    base_response issue_request(base_request &req) override
    {
        return this->ctrl->issue_request(req);
    }
};

} // namespace vans::ait

#endif // VANS_AIT_H
