#include "rmw.h"
namespace vans::rmw
{

bool rmw_controller::check_and_evict()
{
    if (!buffer.full())
        return true;

    if (this->evicting)
        return false;

    /* LRU eviction */
    block_addr_t oldest_addr = addr_invalid;
    clk_t oldest_clk         = clk_invalid;
    for (auto &entry : buffer.entry_map) {
        if (entry.second.state == request_state::end && entry.second.last_used_clk < oldest_clk) {
            oldest_clk  = entry.second.last_used_clk;
            oldest_addr = entry.first;
        }
    }

    if (oldest_addr == addr_invalid) {
        /* All busy, cannot evict */
        return false;
    } else {
        buffer.erase(oldest_addr);
        cnt_events["eviction"]++;
        return true;
    }
}

base_response rmw_controller::issue_request(base_request &req)
{
    auto success = lsq.enqueue(req);
    return {(success), false, clk_invalid};
}

void rmw_controller::drain_current()
{
    for (auto &entry_pair : this->buffer.entry_map) {
        auto &entry = entry_pair.second;

        if (entry.dirty && entry.state == request_state::end) {
            entry.pending_request.type = request_type::flush_back;
            entry.state                = request_state::init;
        }
    }
}

void rmw_controller::init_state_trans_table()
{

    const auto issue_read_next_level =
        [this](const decltype(this->get_next_level(addr_invalid)) &next, buffer_entry &entry, clk_t curr_clk) {
            block_addr_t rmw_addr = translate_to_block_addr(entry.pending_request.logic_addr);
            base_request req{vans::base_request_type::read, rmw_addr, curr_clk, this->next_level_read_callback};
            auto &next_component = std::get<1>(next);
            return next_component->issue_request(req);
        };
    const auto issue_write_next_level =
        [this](const decltype(this->get_next_level(addr_invalid)) &next, buffer_entry &entry, clk_t curr_clk) {
            block_addr_t rmw_addr = translate_to_block_addr(entry.pending_request.logic_addr);
            base_request req{vans::base_request_type::write, rmw_addr, curr_clk, nullptr};
            auto &next_component = std::get<1>(next);
            return next_component->issue_request(req);
        };

    const auto issue_write_local_memory = [this](buffer_entry &entry, clk_t curr_clk) {
        base_request req{base_request_type::write, entry.pending_request.logic_addr, curr_clk, nullptr};
        return this->local_memory_model->issue_request(req);
    };

    const auto issue_read_local_memory = [this](buffer_entry &entry, clk_t curr_clk) {
        base_request req{base_request_type::read, entry.pending_request.logic_addr, curr_clk, nullptr};
        return this->local_memory_model->issue_request(req);
    };

    const auto issue_roq = [this](buffer_entry &entry) {
        auto cl_index = entry.pending_request_cl_index.front();
        entry.pending_request_cl_index.pop_front();
        if (cl_index == -1) {
            throw std::runtime_error(
                "Internal error: trying to serve read request from an entry which does not contain any read callback function.");
        }

        if (roq.full()) {
            throw std::runtime_error("Internal error: trying to issue request to a full `roq` in rmw rmw.");
        }

        auto addr = translate_to_block_addr(entry.pending_request.logic_addr) + cl_index * cpu_cl_size;
        auto &req = this->roq.queue.emplace_back(
            base_request_type::read, addr, entry.pending_request.arrive, entry.callbacks[cl_index]);
        req.depart                = entry.next_action_clk;
        entry.cb_bitmap[cl_index] = false;
    };

#define trans(curr_request_type, last_state)                                                                           \
    state_trans[int(request_type::curr_request_type)][int(request_state::last_state)] = [                              \
        this,                                                                                                          \
        issue_read_next_level,                                                                                         \
        issue_write_next_level,                                                                                        \
        issue_write_local_memory,                                                                                      \
        issue_read_local_memory,                                                                                       \
        issue_roq                                                                                                      \
    ](const block_addr_t block_addr, buffer_entry &entry, clk_t curr_clk)

#define update_duration_cnt(cnt_name) cnt_duration[#cnt_name] += curr_clk - entry.last_used_clk

    trans(write_rmw, init)
    {
        /* Check and issue request to next level */
        auto next                              = this->get_next_level(block_addr);
        auto [issued, deterministic, next_clk] = issue_read_next_level(next, entry, curr_clk);
        if (!issued) {
            cnt_events["next_level_issue_fail"]++;
            return;
        }

        /* Update counters */
        cnt_events["write_rmw"]++;

        /* Update states */
        entry.pending                   = true;
        entry.dirty                     = true;
        entry.state                     = request_state::pending_ait_r;
        entry.valid_to_read             = false;
        entry.last_used_clk             = curr_clk;
        entry.waiting_action_clk_update = !deterministic;
        entry.next_action_clk           = deterministic ? next_clk : clk_invalid;
    };

    trans(write_rmw, pending_ait_r)
    {
        /* Update counters*/
        update_duration_cnt(w_rmw_par);

        /* Update states*/
        entry.state           = request_state::pending_read;
        entry.last_used_clk   = curr_clk;
        entry.next_action_clk = curr_clk + timing.ait_to_rmw_latency;
    };

    trans(write_rmw, pending_read)
    {
        /* Issue request to local memory */
        auto [issued, deterministic, next_clk] = issue_write_local_memory(entry, curr_clk);
        if (!issued) {
            cnt_events["local_memory_issue_fail"]++;
            return;
        }

        /* Update counters*/
        update_duration_cnt(w_rmw_pr);

        /* Update states*/
        entry.state                     = request_state::pending_ait_w;
        entry.last_used_clk             = curr_clk;
        entry.waiting_action_clk_update = !deterministic;
        entry.next_action_clk           = deterministic ? next_clk : clk_invalid;
    };

    trans(write_rmw, pending_ait_w)
    {
        /* Update counters*/
        update_duration_cnt(w_rmw_paw);

        /* Update states*/
        entry.state           = request_state::pending_modify;
        entry.last_used_clk   = curr_clk;
        entry.next_action_clk = curr_clk + timing.rmw_to_ait_latency;
    };


    trans(write_rmw, pending_modify)
    {
        /* Check and issue request to next level */
        auto next                              = this->get_next_level(block_addr);
        auto [issued, deterministic, next_clk] = issue_write_next_level(next, entry, curr_clk);
        if (!issued) {
            cnt_events["next_level_issue_fail"]++;
            return;
        }

        /* Update counters*/
        update_duration_cnt(w_rmw_pm);

        /* Update states*/
        entry.state         = request_state::pending_write;
        entry.last_used_clk = curr_clk;
        entry.pending       = true;
        entry.valid_to_read = true;

        /* Once issue finished, CPU is not stalled */
        entry.waiting_action_clk_update = false;
        entry.next_action_clk           = curr_clk + 1;
    };


    trans(write_rmw, pending_write)
    {
        /* Update counters*/
        update_duration_cnt(w_rmw_pw);

        /* Update states*/
        entry.pending       = false;
        entry.dirty         = false;
        entry.state         = request_state::end;
        entry.last_used_clk = curr_clk;
    };

    trans(write_comb, init)
    {
        /* Issue request to local memory */
        auto [issued, deterministic, next_clk] = issue_write_local_memory(entry, curr_clk);
        if (!issued) {
            cnt_events["local_memory_issue_fail"]++;
            return;
        }

        /* Update counters */
        cnt_events["write_comb"]++;

        /* Update states */
        entry.state                     = request_state::pending_ait_w;
        entry.pending                   = true;
        entry.dirty                     = true;
        entry.valid_to_read             = false;
        entry.last_used_clk             = curr_clk;
        entry.waiting_action_clk_update = !deterministic;
        entry.next_action_clk           = deterministic ? next_clk : clk_invalid;
    };

    trans(write_comb, pending_ait_w)
    {
        /* Update counters*/
        update_duration_cnt(w_comb_paw);

        /* Update states*/
        entry.state           = request_state::pending_modify;
        entry.last_used_clk   = curr_clk;
        entry.next_action_clk = curr_clk + timing.rmw_to_ait_latency;
    };

    trans(write_comb, pending_modify)
    {
        /* Check and issue request to next level */
        auto next                              = this->get_next_level(block_addr);
        auto [issued, deterministic, next_clk] = issue_write_next_level(next, entry, curr_clk);
        if (!issued) {
            cnt_events["next_level_issue_fail"]++;
            return;
        }

        /* Update counters*/
        update_duration_cnt(w_comb_pm);

        /* Update states*/
        entry.state         = request_state::pending_write;
        entry.last_used_clk = curr_clk;
        entry.pending       = true;
        entry.valid_to_read = true;

        /* Once issue finished, CPU is not stalled */
        entry.waiting_action_clk_update = false;
        entry.next_action_clk           = curr_clk + 1;
    };

    trans(write_comb, pending_write)
    {
        /* Update counters*/
        update_duration_cnt(w_comb_pw);

        /* Update states*/
        entry.state         = request_state::end;
        entry.pending       = false;
        entry.dirty         = false;
        entry.last_used_clk = curr_clk;
    };

    trans(write_patch, init)
    {
        /* Issue request to local memory */
        auto [issued, deterministic, next_clk] = issue_write_local_memory(entry, curr_clk);
        if (!issued) {
            cnt_events["local_memory_issue_fail"]++;
            return;
        }

        /* Update counters*/
        cnt_events["write_patch"]++;

        /* Update states*/
        entry.state                     = request_state::pending_ait_w;
        entry.pending                   = true;
        entry.dirty                     = true;
        entry.valid_to_read             = false;
        entry.last_used_clk             = curr_clk;
        entry.waiting_action_clk_update = !deterministic;
        entry.next_action_clk           = deterministic ? next_clk : clk_invalid;
    };

    trans(write_patch, pending_ait_w)
    {
        /* Update counters*/
        update_duration_cnt(w_patch_paw);

        /* Update states*/
        entry.state           = request_state::pending_modify;
        entry.last_used_clk   = curr_clk;
        entry.next_action_clk = curr_clk + timing.rmw_to_ait_latency;
    };

    trans(write_patch, pending_modify)
    {
        /* Check and issue request to next level */
        auto next                              = this->get_next_level(block_addr);
        auto [issued, deterministic, next_clk] = issue_write_next_level(next, entry, curr_clk);
        if (!issued) {
            cnt_events["next_level_issue_fail"]++;
            return;
        }

        /* Update counters*/
        update_duration_cnt(w_patch_pm);

        /* Update states*/
        entry.state         = request_state::pending_write;
        entry.last_used_clk = curr_clk;
        entry.pending       = true;
        entry.valid_to_read = true;

        /* Once issue finished, CPU is not stalled */
        entry.waiting_action_clk_update = false;
        entry.next_action_clk           = curr_clk + 1;
    };

    trans(write_patch, pending_write)
    {
        /* Update counters*/
        update_duration_cnt(w_patch_pw);
        /* Update states*/
        entry.state         = request_state::end;
        entry.pending       = false;
        entry.dirty         = false;
        entry.last_used_clk = curr_clk;
    };

    trans(flush_back, init)
    {
        /* Check and issue request to next level */
        auto next                              = this->get_next_level(block_addr);
        auto [issued, deterministic, next_clk] = issue_write_next_level(next, entry, curr_clk);
        if (!issued) {
            cnt_events["next_level_issue_fail"]++;
            return;
        }

        /* Update counters*/
        cnt_events["flush_back"]++;

        /* Update states*/
        entry.state         = request_state::pending_ait_w;
        entry.last_used_clk = curr_clk;
        entry.pending       = true;
        entry.dirty         = true;

        /* Once issue finished, CPU is not stalled */
        entry.waiting_action_clk_update = false;
        entry.next_action_clk           = curr_clk + 1;
    };

    trans(flush_back, pending_ait_w)
    {
        /* Update counters*/
        update_duration_cnt(w_flush_paw);

        /* Update states*/
        entry.state           = request_state::pending_write;
        entry.last_used_clk   = curr_clk;
        entry.next_action_clk = curr_clk + timing.rmw_to_ait_latency;
    };

    trans(flush_back, pending_write)
    {
        /* Update counters*/
        update_duration_cnt(w_flush_pw);

        /* Update states*/
        entry.state                     = request_state::end;
        entry.pending                   = false;
        entry.dirty                     = false;
        entry.waiting_action_clk_update = false;
        entry.last_used_clk             = curr_clk;

        this->evicting = false;
        cnt_events["eviction"]++;
    };

    trans(read_cold, init)
    {
        /* Check and issue request to next level */
        auto next                              = this->get_next_level(block_addr);
        auto [issued, deterministic, next_clk] = issue_read_next_level(next, entry, curr_clk);
        if (!issued) {
            cnt_events["next_level_issue_fail"]++;
            return;
        }

        /* Update counters*/
        cnt_events["read_cold"]++;

        /* Update states*/
        entry.state                     = request_state::pending_ait_r;
        entry.pending                   = true;
        entry.dirty                     = false;
        entry.valid_to_read             = false;
        entry.last_used_clk             = curr_clk;
        entry.waiting_action_clk_update = !deterministic;
        entry.next_action_clk           = deterministic ? next_clk : clk_invalid;
    };

    trans(read_cold, pending_ait_r)
    {
        /* Update counters*/
        update_duration_cnt(r_cold_par);

        /* Update states*/
        entry.state           = request_state::pending_read;
        entry.last_used_clk   = curr_clk;
        entry.next_action_clk = curr_clk + timing.ait_to_rmw_latency;
    };

    trans(read_cold, pending_read)
    {
        /* Issue request to local memory */
        auto [issued, deterministic, next_clk] = issue_read_local_memory(entry, curr_clk);
        if (!issued) {
            cnt_events["local_memory_issue_fail"]++;
            return;
        }

        /* Update counters*/
        update_duration_cnt(r_cold_pr);

        /* Update states*/
        entry.state                     = request_state::pending_readout;
        entry.valid_to_read             = true;
        entry.last_used_clk             = curr_clk;
        entry.waiting_action_clk_update = !deterministic;
        entry.next_action_clk           = deterministic ? next_clk : clk_invalid;
    };

    trans(read_cold, pending_readout)
    {
        /* Issue request to roq (read-out-queue)*/
        if (roq.full()) {
            cnt_events["roq_full"]++;
            return;
        }
        issue_roq(entry);

        /* Update counters*/
        update_duration_cnt(r_cold_pro);

        /* Update states*/
        entry.last_used_clk = curr_clk;
        if (!entry.pending_request_cl_index.empty()) {
            /* Go to pending_read state if there are pending requests. */
            entry.state = request_state::pending_read;
        } else {
            entry.state   = request_state::end;
            entry.pending = false;
        }
    };


    trans(read_ff, init)
    {
        /* Issue request to local memory */
        auto [issued, deterministic, next_clk] = issue_read_local_memory(entry, curr_clk);
        if (!issued) {
            cnt_events["local_memory_issue_fail"]++;
            return;
        }

        /* Update counters*/
        cnt_events["read_fast_forward"]++;

        /* Update states*/
        entry.state                     = request_state::pending_readout;
        entry.pending                   = true;
        entry.dirty                     = false;
        entry.valid_to_read             = true;
        entry.last_used_clk             = curr_clk;
        entry.waiting_action_clk_update = !deterministic;
        entry.next_action_clk           = deterministic ? next_clk : clk_invalid;
    };

    trans(read_ff, pending_readout)
    {
        /* Issue request to roq (read-out-queue)*/
        if (roq.full()) {
            cnt_events["roq_full"]++;
            return;
        }
        issue_roq(entry);

        /* Update counters*/
        update_duration_cnt(r_ff_pro);

        /* Update states*/
        entry.last_used_clk = curr_clk;
        if (!entry.pending_request_cl_index.empty()) {
            /* Go to pending_read state if there are pending requests. */
            entry.state = request_state::init;
        } else {
            entry.state   = request_state::end;
            entry.pending = false;
        }
    };
#undef update_duration_cnt
#undef trans
}

void rmw_controller::tick(clk_t curr_clk)
{
    tick_roq(curr_clk);
    tick_lsq(curr_clk);
    tick_internal_buffer(curr_clk);
}

void rmw_controller::tick_roq(clk_t curr_clk)
{
    if (roq.empty())
        return;

    auto &front_req = roq.queue.front();
    if (front_req.depart <= curr_clk) {
        if (front_req.callback != nullptr) {
            auto addr = front_req.addr;
            front_req.callback(addr + this->start_addr, curr_clk);
        }
        roq.queue.pop_front();
    }
}

void rmw_controller::tick_lsq(clk_t curr_clk)
{
    if (lsq.empty())
        return;

    auto type = lsq.queue.front().type;
    switch (type) {
    case base_request_type::read:
        tick_lsq_read(curr_clk);
        break;
    case base_request_type::write:
        tick_lsq_write(curr_clk);
        break;
    default:
        throw std::runtime_error("Internal error, lsq_tick error type " + std::to_string(int(type)));
        break;
    }
}

void rmw_controller::tick_lsq_read(clk_t curr_clk)
{
    auto &front_req = lsq.queue.front();
    bool req_served = false;
    bool req_patch  = true;

    auto addr      = front_req.addr;
    auto cl_index  = block_offset_cl(addr);
    auto cl_bitmap = 1U << cl_index;

    auto entry_pair = this->buffer.find(addr);
    if (entry_pair == this->buffer.end()) {
        /* Buffer entry not found, need to insert new entry */
        if (this->check_and_evict()) {
            /* Buffer has free space, construct new entry in-place */
            entry_pair = buffer.insert(addr, curr_clk, request_type::read_cold, addr, cl_bitmap);
            req_served = true;
            req_patch  = false;
        } else {
            /* Internal rmw full and cannot evict, nothing to do in this cycle */
            req_served = false;
        }
    } else {
        /* Buffer entry found, try to 1. patch the read request or 2. fast-forward */
        auto &entry = entry_pair->second;
        if (entry.pending
            && (entry.pending_request.type == request_type::read_cold
                || entry.pending_request.type == request_type::read_ff)) {
            /* Patch read request */
            if (entry.pending_request_cl_index.empty()) {
                throw std::runtime_error(
                    "Internal error, "
                    "the read request to patch does not have any pending read request, maybe a code bug.");
            }
            if ((entry.pending_request_cl_index.size() < block_size_cl) && (!entry.cb_bitmap[cl_index])) {
                req_served = true;
                req_patch  = true;
                cnt_events["read_patch"]++;
            } else {
                req_served = false;
            }
        } else {
            if (entry.valid_to_read) {
                /* Fast forward */
                entry.assign_new_request(curr_clk, request_type::read_ff, addr, cl_bitmap);
                req_served = true;
                req_patch  = false;
            }
        }
    }

    if (req_served) {
        if (!req_patch)
            entry_pair->second.reset_callback();
        entry_pair->second.assign_callback(cl_index, front_req.callback);
        lsq.queue.pop_front();
        cnt_events["read_access"]++;
    }
}

void rmw_controller::tick_lsq_write(clk_t curr_clk)
{
    auto &front_req      = lsq.queue.front();
    auto curr_logic_addr = front_req.addr;
    auto curr_block_addr = translate_to_block_addr(curr_logic_addr);
    bool entry_found     = false;
    bool patch_rmw       = false;

    auto entry_pair = buffer.find(curr_block_addr);
    if (entry_pair != buffer.end()) {
        entry_found = true;
        auto &entry = entry_pair->second;
        if (entry.pending_request.type == request_type::write_rmw) {
            if (entry.state == request_state::pending_read || entry.state == request_state::pending_modify) {
                patch_rmw = true;
            }
        }
    }

    if (!entry_found) {
        /* Need to construct new rmw entry, check and evict before constructing new entry */
        if (!check_and_evict()) {
            return;
        }
    } else {
        if ((!patch_rmw) && entry_pair->second.pending) {
            /* This request is pending, wait for it to complete */
            return;
        }
    }

    unsigned num_write_req_served = 0;
    buffer_entry::bitmap_t cl_hit = 0;

    /* Write combining, stop at a read request to the current block */
    for (auto req = lsq.queue.begin(); req != lsq.queue.end();) {
        if (curr_block_addr != translate_to_block_addr(req->addr)) {
            req++;
            continue;
        } else {
            if (req->type == base_request_type::read) {
                /* A read to this block stops the write combining */
                break;
            } else if (req->type == base_request_type::write) {
                /* Combine the current write request */
                cl_hit[block_offset_cl(req->addr)] = true;
                req                                = lsq.queue.erase(req);
                num_write_req_served++;
            } else {
                throw std::runtime_error("Internal error, unknown request type in lsq.");
            }
        }
    }

    if (patch_rmw) {
        cl_hit |= entry_pair->second.cl_bitmap;
    }

    request_type type = request_type::write_rmw;
    if (cl_hit == block_hit_cl_bitmask)
        type = request_type::write_comb;

    if (!entry_found) {
        entry_pair =
            buffer.insert(curr_block_addr, curr_clk, type, curr_logic_addr, static_cast<unsigned>(cl_hit.to_ulong()));
    } else {
        if (patch_rmw) {
            if (type == request_type::write_comb) {
                entry_pair->second.assign_new_request(
                    curr_clk, type, curr_logic_addr, static_cast<unsigned>(cl_hit.to_ulong()));
                cnt_events["patch_rmw_comb"]++;
            } else {
                entry_pair->second.cl_bitmap = cl_hit;
                cnt_events["patch_rmw"]++;
            }
        } else {
            type = request_type::write_patch;
            entry_pair->second.assign_new_request(
                curr_clk, type, curr_logic_addr, static_cast<unsigned>(cl_hit.to_ulong()));
        }
    }

    /* NOTE: a combined write request counts as one request in this counter */
    cnt_events["write_access"]++;
}

void rmw_controller::tick_internal_buffer(clk_t curr_clk)
{
    for (auto &entry_pair : this->buffer.entry_map) {
        auto curr_block_addr = entry_pair.first;
        auto &entry          = entry_pair.second;

        if (entry.state == request_state::init)
            goto rmw_buffer_tick_internal_state_transfer;

        if (!entry.pending)
            continue;

        if (entry.waiting_action_clk_update)
            continue;

        if (entry.next_action_clk == clk_invalid)
            throw std::runtime_error("Internal error, the next_action_clk is invalid ("
                                     + std::to_string(entry.next_action_clk) + ")");

        if (entry.next_action_clk > curr_clk)
            continue;

    rmw_buffer_tick_internal_state_transfer:
        auto &func = this->state_trans[int(entry.pending_request.type)][int(entry.state)];
        if (func == nullptr) {
            throw std::runtime_error("Internal error, unknown state transfer.");
        }
        func(curr_block_addr, entry, curr_clk);
    }
}
} // namespace vans::rmw
