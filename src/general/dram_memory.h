#ifndef VANS_DRAM_MEMORY_H
#define VANS_DRAM_MEMORY_H

#include "controller.h"
#include "dram.h"
#include "memory.h"

namespace vans::dram
{

class dram_media_request
{
  public:
    bool is_first_cmd;
    addr_type_t addr;
    int coreid = 0;

    enum : unsigned int { total_req_types = 5 };
    enum class req_type {
        read,
        write,
        refresh,
        power_down,
        self_refresh,
    } type;

    long arrive = -1;
    long depart = -1;

    using callback_f = std::function<void(logic_addr_t, clk_t)>;
    callback_f callback;

    dram_media_request() = delete;

    explicit dram_media_request(base_request &req) :
        is_first_cmd(true), addr(req.addr), callback(req.callback), arrive(req.arrive), depart(req.depart)
    {
        if (req.type == base_request_type::read)
            type = req_type::read;
        else if (req.type == base_request_type::write)
            type = req_type::write;
        else
            throw std::runtime_error("Unknown base_request type: " + std::to_string(int(req.type)));
    }

    dram_media_request(mapped_addr_t &mapped_addr, req_type type, int coreid = 0) :
        is_first_cmd(true), addr(mapped_addr), coreid(coreid), type(type)
    {
    }

    dram_media_request(logic_addr_t addr, req_type type, callback_f callback, int coreid = 0) :
        callback(std::move(callback)), is_first_cmd(true), addr(addr), coreid(coreid), type(type)
    {
    }

    dram_media_request(addr_type_t addr, req_type type, int coreid = 0) :
        addr(std::move(addr)), is_first_cmd(true), type(type), coreid(coreid)
    {
    }
    explicit dram_media_request(long addr, req_type type, callback_f callback = nullptr, int coreid = 0) :
        callback(std::move(callback)), addr(logic_addr_t(addr)), is_first_cmd(true), type(type), coreid(coreid)
    {
    }

    [[maybe_unused]] [[nodiscard]] std::string to_string() const
    {
        char str_buf[128];
        snprintf(
            str_buf, sizeof(str_buf), "Request: logic_addr[%016lx] type[%d]", this->addr.logic_addr, (int)this->type);
        return std::string(str_buf);
    }

    virtual ~dram_media_request() = default;
};

template <typename StandardType>
class dram_media_controller : public media_controller<dram_media_request, DRAM<StandardType>>
{
  private:
    using command  = typename StandardType::command;
    using level    = typename StandardType::level;
    using state    = typename StandardType::state;
    using req_type = dram_media_request::req_type;
    using request  = dram_media_request;

    clk_t last_refreshed_clk = 0;
    bool write_prior_mode    = false;

  public:
    clk_t curr_clk     = 0;
    clk_t report_epoch = 0;
    size_t report_cnt  = 0;
    size_t queue_size  = 32;
    size_t id          = 0;

    std::shared_ptr<DRAM<StandardType>> channel;

    using dram_request_queue = request_queue<dram_media_request>;
    dram_request_queue act_queue;
    dram_request_queue misc_queue;
    dram_request_queue read_queue;
    dram_request_queue write_queue;

    std::deque<dram_media_request> pending_queue;

    logic_addr_t start_addr;

  public:
    dram_media_controller() = delete;

    explicit dram_media_controller(const config &cfg,
                                   std::shared_ptr<DRAM<StandardType>> channel,
                                   const std::shared_ptr<const dram_mapping<StandardType>> &mapper,
                                   logic_addr_t dram_start_addr) :
        channel(channel),
        start_addr(dram_start_addr),
        report_epoch(cfg.get_ulong("report_epoch")),
        queue_size(cfg.get_ulong("queue_size")),
        act_queue(cfg.get_ulong("queue_size")),
        misc_queue(cfg.get_ulong("queue_size")),
        read_queue(cfg.get_ulong("queue_size")),
        write_queue(cfg.get_ulong("queue_size"))
    {
    }

    virtual ~dram_media_controller() = default;

    dram_request_queue &get_queue(req_type type)
    {
        switch (type) {
        case req_type::read:
            return read_queue;
        case req_type::write:
            return write_queue;
        case req_type::refresh:
        case req_type::power_down:
        case req_type::self_refresh:
            return misc_queue;
        default:
            throw std::runtime_error("Internal error, state unknown in current implementation.");
        }
    }

    base_response issue_request(dram_media_request &request) override
    {
        request.arrive = curr_clk;

        auto &queue = get_queue(request.type);
        auto issued = queue.enqueue(request);
        if (!issued)
            return {false, false, clk_invalid};

        if (this->report_epoch != 0) {
            if (this->report_cnt % this->report_epoch == 0) {
                printf("DRAM: request No. %lu arrived at clock %lu\n", this->report_cnt, curr_clk);
            }
            this->report_cnt++;
        }

        /* Fast forward from write queue */
        if (request.type == req_type::read) {
            for (auto &wr : write_queue.queue) {
                /* The current read request is youngest request compared to all other requests in all queues,
                 * so all write requests are older than this read,
                 * thus no need to check arrive clk
                 */
                if (request.addr.logic_addr == wr.addr.logic_addr) {
                    request.depart = curr_clk + 1;
                    pending_queue.push_back(request);
                    read_queue.queue.pop_back();
                    break;
                }
            }
        }

        return {true, false, clk_invalid};
    }

    void tick(clk_t new_clk) override
    {
        this->curr_clk = new_clk;

        if (!pending_queue.empty()) {
            auto &request = pending_queue[0];
            if (request.depart <= curr_clk) {
                if (request.depart - request.arrive > 1) {
                    channel->update_serving_requests(request.addr.mapped_addr.data(), -1, curr_clk);
                }
                if (request.callback) {
                    request.callback(request.addr.logic_addr + this->start_addr, curr_clk);
                    pending_queue.pop_front();
                }
            }
        }

        auto refresh_interval = channel->spec->timing.nREFI;
        if (curr_clk - last_refreshed_clk >= refresh_interval) {
            std::vector<uint64_t> addr_vec(channel->spec->total_levels - 1);
            addr_vec[0] = channel->id;
            for (auto rank : channel->children) {
                addr_vec[1] = rank->id;
                /* No bank-level refresh */
                // addr_vec[2] = -1;
                // addr_vec[3] = -1;
                request req(addr_vec, req_type::refresh);
                auto [res, deterministic, next_clk] = issue_request(req);
                if (!res) {
                    printf("DRAM::misc_size %lu\n", misc_queue.queue.size());
                    printf("DRAM::act_size %lu\n", act_queue.queue.size());
                    printf("DRAM::read_size %lu\n", read_queue.queue.size());
                    printf("DRAM::write_size %lu\n", write_queue.queue.size());
                    throw std::runtime_error("DRAM: Queue full, cannot issue refresh request.");
                }
            }
            last_refreshed_clk = curr_clk;
        }


        if (write_queue.size() != 0) {
            if (read_queue.size() == 0) {
                write_prior_mode = true;
            } else {
                request &wreq    = write_queue.queue.front();
                request &rreq    = read_queue.queue.front();
                write_prior_mode = wreq.arrive < rreq.arrive;
            }
        } else {
            if (read_queue.size() != 0) {
                write_prior_mode = false;
            } else {
                /* cerr << "no read/write request to handle" << endl; */
            }
        }

        dram_request_queue *q;
        if (act_queue.size() != 0)
            q = &act_queue;
        else if (misc_queue.size() != 0)
            q = &misc_queue;
        else if (write_prior_mode)
            q = &write_queue;
        else
            q = &read_queue;

        schedule(q);
    }

    void drain() override {}

    bool pending() override
    {
        return !pending_queue.empty();
    }

    bool full() override
    {
        return read_queue.full() || write_queue.full();
    }

  private:
    command get_first_cmd(request &req)
    {
        command cmd = channel->spec->req_to_cmd.find(req.type)->second;
        return channel->decode(cmd, req.addr.mapped_addr.data());
    }

    bool is_ready(request &req)
    {
        command cmd = get_first_cmd(req);
        return channel->check(cmd, req.addr.mapped_addr.data(), curr_clk);
    }

    void schedule(dram_request_queue *curr_queue)
    {
        if (curr_queue->queue.empty())
            return;

        /* Front of queue */
        auto req = curr_queue->queue.begin();
        if (!is_ready(*req))
            return;

        if (req->is_first_cmd) {
            req->is_first_cmd = false;
            if (req->type == req_type::read || req->type == req_type::write) {
                channel->update_serving_requests(req->addr.mapped_addr.data(), 1, curr_clk);
            }
        }

        auto cmd = get_first_cmd(*req);
        issue_cmd(cmd, req->addr.mapped_addr.data());

        if (!(channel->spec->is_accessing(cmd) || channel->spec->is_refreshing(cmd))) {
            if (channel->spec->is_opening(cmd)) {
                act_queue.queue.push_back(*req);
                curr_queue->queue.erase(req);
            }
            return;
        }

        if (req->type == req_type::read) {
            req->depart = curr_clk + channel->spec->read_latency;
            pending_queue.push_back(*req);
        } else if (req->type == req_type::write) {
            /* Write request's callback is served in upper level component once request issue is finished */
            channel->update_serving_requests(req->addr.mapped_addr.data(), -1, curr_clk);
        }

        curr_queue->queue.erase(req);
    }

    void issue_cmd(command cmd, addr_t addr_vec, bool print_trace = false)
    {
        channel->update(cmd, addr_vec, curr_clk);

        if (print_trace) {
            std::cout << channel->spec->command_name.find(cmd)->second << '\t' << curr_clk << '\t';
            for (int i = 0; i < channel->spec->total_levels; i++)
                std::cout << addr_vec[i] << '\t';
            std::cout << std::endl;
        }
    }
};

template <typename StandardType>
class dram_memory : public memory<dram_media_controller<StandardType>, DRAM<StandardType>>
{
  public:
    std::shared_ptr<StandardType> ddr;
    std::shared_ptr<dram_mapping<StandardType>> mapper;
    logic_addr_t max_addr = 0;

    dram_memory() = delete;

    explicit dram_memory(const config &cfg) : memory<dram_media_controller<StandardType>, DRAM<StandardType>>(cfg)
    {
        typename StandardType::timing_type t(cfg);

        ddr                            = std::make_shared<StandardType>(t);
        using l                        = typename StandardType::level;
        ddr->size                      = cfg.get_ulong("size");
        ddr->data_width                = cfg.get_ulong("data_width");
        ddr->count[int(l::channel)]    = cfg.get_ulong("channel");
        ddr->count[int(l::rank)]       = cfg.get_ulong("rank");
        ddr->count[int(l::bank_group)] = cfg.get_ulong("bank_group");
        ddr->count[int(l::bank)]       = cfg.get_ulong("bank");
        ddr->count[int(l::row)]        = cfg.get_ulong("row");
        ddr->count[int(l::col)]        = cfg.get_ulong("col");

        mapper                 = std::make_shared<dram_mapping<StandardType>>(ddr, cfg);
        this->memory_component = std::make_shared<DRAM<StandardType>>(ddr, l::channel);
        if (!is_pwr_of_2(ddr->count[0]) || !is_pwr_of_2(ddr->count[1]))
            throw std::runtime_error("Count of channel and rank not supported (not power of 2)");

        max_addr = ddr->channel_width / 8;
        for (auto i = 0; i < StandardType::total_levels; i++) {
            max_addr *= ddr->count[i];
        }

        this->memory_component->id = 0;
        this->ctrl                 = std::make_shared<dram_media_controller<StandardType>>(
            cfg, this->memory_component, mapper, cfg.get_ulong("start_addr"));
    }

    void tick(clk_t curr_clk) final
    {
        this->ctrl->tick(curr_clk);
    }

    double clk_ns()
    {
        return ddr->timing.tCK;
    }

    base_response issue_request(base_request &req) final
    {
        dram_media_request inner_req(req);
        mapper->map(inner_req.addr);
        return this->ctrl->issue_request(inner_req);
    }
};

} // namespace vans::dram

#endif // VANS_DRAM_MEMORY_H
