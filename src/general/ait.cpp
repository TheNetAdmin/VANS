#include "ait.h"
#include <cassert>

namespace vans::ait
{

void ait_controller::init_state_trans_table()
{

    const auto issue_read_next_level =
        [this](const decltype(this->get_next_level(addr_invalid)) &next, buffer_entry &entry, clk_t curr_clk) {
            block_addr_t blk_addr = translate_to_block_addr(entry.pending_request.rmw_block_addr);
            base_request req{vans::base_request_type::read, blk_addr, curr_clk, this->next_level_read_callback};
            auto &next_component = std::get<1>(next);
            return next_component->issue_request(req);
        };
    const auto issue_write_next_level =
        [this](const decltype(this->get_next_level(addr_invalid)) &next, buffer_entry &entry, clk_t curr_clk) {
            block_addr_t blk_addr = translate_to_block_addr(entry.pending_request.rmw_block_addr);
            base_request req{vans::base_request_type::write, blk_addr, curr_clk, this->next_level_read_callback};
            auto &next_component = std::get<1>(next);
            return next_component->issue_request(req);
        };

    const auto issue_lmemq = [this](buffer_entry &entry, base_request_type type, clk_t curr_clk) -> base_response {
        base_request req{type, entry.pending_request.rmw_block_addr, curr_clk, nullptr};
        bool issued = this->lmemq.enqueue(req);
        return {(issued), false, clk_invalid};
    };

    const auto issue_write_local_memory = [this](buffer_entry &entry, clk_t curr_clk) {
        base_request req{base_request_type::write, entry.pending_request.rmw_block_addr, curr_clk, nullptr};
        return this->local_memory_model->issue_request(req);
    };

    const auto issue_read_local_memory = [this](buffer_entry &entry, clk_t curr_clk) {
        base_request req{base_request_type::read, entry.pending_request.rmw_block_addr, curr_clk, nullptr};
        return this->local_memory_model->issue_request(req);
    };

#define trans(curr_request_type, last_state)                                                                           \
    state_trans[int(request_type::curr_request_type)][int(request_state::last_state)] =                                \
        [ this, issue_read_next_level, issue_write_next_level,                                                         \
          issue_lmemq ](const block_addr_t block_addr, buffer_entry &entry, clk_t curr_clk)

#define update_duration_cnt(cnt_name) cnt_duration[#cnt_name] += curr_clk - entry.last_used_clk

    trans(write_miss, init)
    {
        /* Check and issue request to next level */
        auto next                              = this->get_next_level(block_addr);
        auto [issued, deterministic, next_clk] = issue_read_next_level(next, entry, curr_clk);
        if (!issued) {
            cnt_events["next_level_issue_fail"]++;
            return;
        }

        /* Update counters */
        cnt_events["write_miss"]++;

        /* Update states */
        entry.pending                   = true;
        entry.dirty                     = true;
        entry.state                     = request_state::pending_read_media;
        entry.valid_to_read             = false;
        entry.last_used_clk             = curr_clk;
        entry.waiting_action_clk_update = !deterministic;
        entry.next_action_clk           = deterministic ? next_clk : clk_invalid;
    };

    trans(write_miss, pending_read_media)
    {
        /* Issue request to local memory */
        auto [issued, deterministic, next_clk] = issue_lmemq(entry, base_request_type::write, curr_clk);
        if (!issued) {
            cnt_events["local_memory_issue_fail"]++;
            return;
        }

        /* Update counters*/
        update_duration_cnt(w_miss_prm);

        /* Update states */
        entry.state                     = request_state::pending_write_dram;
        entry.last_used_clk             = curr_clk;
        entry.waiting_action_clk_update = !deterministic;
        entry.next_action_clk           = deterministic ? next_clk : clk_invalid;
    };

    trans(write_miss, pending_write_dram)
    {
        auto wear_leveling_delay = this->table.check_wear_leveling(block_addr);

        /* Update counters*/
        update_duration_cnt(w_miss_pwd);
        if (wear_leveling_delay)
            cnt_events["migration"]++;

        /* Update states*/
        entry.state                     = request_state::pending_migration;
        entry.valid_to_read             = true;
        entry.last_used_clk             = curr_clk;
        entry.waiting_action_clk_update = false;
        entry.next_action_clk           = curr_clk + 1 + wear_leveling_delay;
    };

    trans(write_miss, pending_migration)
    {
        /* Check and issue request to next level */
        auto next                              = this->get_next_level(block_addr);
        auto [issued, deterministic, next_clk] = issue_write_next_level(next, entry, curr_clk);
        if (!issued) {
            cnt_events["next_level_issue_fail"]++;
            return;
        }

        /* Update counters*/
        update_duration_cnt(w_miss_pm);

        /* Update states*/
        entry.state                     = request_state::pending_write_media;
        entry.last_used_clk             = curr_clk;
        entry.waiting_action_clk_update = !deterministic;
        entry.next_action_clk           = deterministic ? next_clk : clk_invalid;
    };

    trans(write_miss, pending_write_media)
    {
        /* Update counters*/
        update_duration_cnt(w_miss_pwm);

        /* Update states*/
        entry.state         = request_state::end;
        entry.pending       = false;
        entry.dirty         = false;
        entry.last_used_clk = curr_clk;

        /* Run callback */
        if (entry.cb) {
            entry.cb(entry.pending_request.rmw_block_addr, curr_clk);
        }
    };

    trans(write_hit, init)
    {
        /* Issue request to local memory */
        auto [issued, deterministic, next_clk] = issue_lmemq(entry, base_request_type::write, curr_clk);
        if (!issued) {
            cnt_events["local_memory_issue_fail"]++;
            return;
        }

        /* Update counters */
        cnt_events["write_hit"]++;

        /* Update states */
        entry.state                     = request_state::pending_write_dram;
        entry.pending                   = true;
        entry.dirty                     = true;
        entry.valid_to_read             = false;
        entry.last_used_clk             = curr_clk;
        entry.waiting_action_clk_update = !deterministic;
        entry.next_action_clk           = deterministic ? next_clk : clk_invalid;
    };

    trans(write_hit, pending_write_dram)
    {
        auto wear_leveling_delay = this->table.check_wear_leveling(block_addr);

        /* Update counters*/
        update_duration_cnt(w_hit_pwd);
        if (wear_leveling_delay)
            cnt_events["migration"]++;

        /* Update states*/
        entry.state                     = request_state::pending_migration;
        entry.valid_to_read             = true;
        entry.last_used_clk             = curr_clk;
        entry.waiting_action_clk_update = false;
        entry.next_action_clk           = curr_clk + 1 + wear_leveling_delay;
    };

    trans(write_hit, pending_migration)
    {
        /* Check and issue request to next level */
        auto next                              = this->get_next_level(block_addr);
        auto [issued, deterministic, next_clk] = issue_write_next_level(next, entry, curr_clk);
        if (!issued) {
            cnt_events["next_level_issue_fail"]++;
            return;
        }

        /* Update counters*/
        update_duration_cnt(w_hit_pm);

        /* Update states*/
        entry.state                     = request_state::pending_write_media;
        entry.last_used_clk             = curr_clk;
        entry.waiting_action_clk_update = !deterministic;
        entry.next_action_clk           = deterministic ? next_clk : clk_invalid;
    };

    trans(write_hit, pending_write_media)
    {
        /* Update counters*/
        update_duration_cnt(w_hit_pwm);

        /* Update states*/
        entry.state         = request_state::end;
        entry.pending       = false;
        entry.dirty         = false;
        entry.last_used_clk = curr_clk;

        /* Run callback */
        if (entry.cb) {
            entry.cb(entry.pending_request.rmw_block_addr, curr_clk);
        }
    };

    trans(read_miss, init)
    {
        /* Check and issue request to next level */
        auto next                              = this->get_next_level(block_addr);
        auto [issued, deterministic, next_clk] = issue_read_next_level(next, entry, curr_clk);
        if (!issued) {
            cnt_events["next_level_issue_fail"]++;
            return;
        }

        /* Update counters*/
        cnt_events["read_miss"]++;

        /* Update states*/
        entry.state                     = request_state::pending_read_media;
        entry.pending                   = true;
        entry.dirty                     = false;
        entry.valid_to_read             = false;
        entry.waiting_action_clk_update = !deterministic;
        entry.next_action_clk           = deterministic ? next_clk : clk_invalid;
        entry.last_used_clk             = curr_clk;
    };

    trans(read_miss, pending_read_media)
    {
        /* Issue request to local memory */
        auto [issued, deterministic, next_clk] = issue_lmemq(entry, base_request_type::read, curr_clk);
        if (!issued) {
            cnt_events["local_memory_issue_fail"]++;
            return;
        }

        /* Update counters*/
        update_duration_cnt(r_miss_prm);

        /* Update states*/
        entry.state                     = request_state::pending_read_dram;
        entry.valid_to_read             = true;
        entry.last_used_clk             = curr_clk;
        entry.waiting_action_clk_update = !deterministic;
        entry.next_action_clk           = deterministic ? next_clk : clk_invalid;
    };

    trans(read_miss, pending_read_dram)
    {
        /* Update counters*/
        update_duration_cnt(r_miss_prd);

        /* Update states*/
        entry.state         = request_state::end;
        entry.pending       = false;
        entry.last_used_clk = curr_clk;

        /* Run callback */
        if (entry.cb) {
            entry.cb(entry.pending_request.rmw_block_addr, curr_clk);
        }
    };

    trans(read_hit, init)
    {
        /* Issue request to local memory */
        auto [issued, deterministic, next_clk] = issue_lmemq(entry, base_request_type::read, curr_clk);
        if (!issued) {
            cnt_events["local_memory_issue_fail"]++;
            return;
        }

        /* Update counters*/
        cnt_events["read_hit"]++;

        /* Update states*/
        entry.state                     = request_state::pending_read_dram;
        entry.pending                   = true;
        entry.valid_to_read             = true;
        entry.last_used_clk             = curr_clk;
        entry.waiting_action_clk_update = !deterministic;
        entry.next_action_clk           = deterministic ? next_clk : clk_invalid;
    };

    trans(read_hit, pending_read_dram)
    {
        /* Update counters*/
        update_duration_cnt(r_hit_prd);

        /* Update states*/
        entry.pending       = false;
        entry.state         = request_state::end;
        entry.last_used_clk = curr_clk;

        /* Run callback */
        if (entry.cb) {
            entry.cb(entry.pending_request.rmw_block_addr, curr_clk);
        }
    };

#undef update_duration_cnt
#undef trans
}

void ait_controller::drain_current() {}

void ait_controller::tick(clk_t curr_clk)
{
    tick_lsq(curr_clk);
    tick_lmemq(curr_clk);
    tick_internal_buffer(curr_clk);
}

void ait_controller::tick_lsq(clk_t curr_clk)
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

void ait_controller::tick_lsq_read(clk_t curr_clk)
{
    auto &front_req = lsq.queue.front();
    bool req_served = false;

    auto rmw_addr   = vans::rmw::translate_to_block_addr(front_req.addr);
    auto ait_addr   = vans::ait::translate_to_block_addr(rmw_addr);
    auto rmw_bitmap = vans::ait::block_bitshift_rmw(rmw_addr);

    auto entry_pair = this->buffer.find(ait_addr);
    if (entry_pair == this->buffer.end()) {
        /* Buffer entry not found, need to insert new entry */
        if (this->check_and_evict()) {
            /* Buffer has free space, construct new entry in-place */
            entry_pair = buffer.insert(ait_addr, curr_clk, request_type::read_miss, rmw_addr, rmw_bitmap);
            req_served = true;
        } else {
            /* Full and cannot evict */
            req_served = false;
        }
    } else {
        /* Found existing buffer entry, try fast forward
         * NOTE: there's no read-patch in ait, not like rmw
         */
        auto &entry = entry_pair->second;
        if (entry.valid_to_read && !entry.pending) {
            entry.assign_new_request(curr_clk, request_type::read_hit, rmw_addr, rmw_bitmap);
            req_served = true;
        } else {
            req_served = false;
        }
    }

    if (req_served) {
        entry_pair->second.assign_callback(front_req.callback);
        lsq.queue.pop_front();
        cnt_events["read_access"]++;
    }
}

void ait_controller::tick_lsq_write(clk_t curr_clk)
{
    /* NOTE: ait does not implement write combining, not like rmw */
    auto &front_req = lsq.queue.front();
    auto rmw_addr   = vans::rmw::translate_to_block_addr(front_req.addr);
    auto ait_addr   = vans::ait::translate_to_block_addr(rmw_addr);
    auto rmw_bitmap = vans::ait::block_bitshift_rmw(rmw_addr);

    bool entry_found = false;
    auto entry_pair  = buffer.entry_map.find(ait_addr);
    if (entry_pair != buffer.entry_map.end()) {
        entry_found = true;
    }

    bool write_issued = false;
    if (!entry_found) {
        if (check_and_evict()) {
            entry_pair   = buffer.insert(ait_addr, curr_clk, request_type::write_miss, rmw_addr, rmw_bitmap);
            write_issued = true;
        }
    } else {
        if (!entry_pair->second.pending) {
            entry_pair->second.assign_new_request(curr_clk, request_type::write_hit, rmw_addr, rmw_bitmap);
            write_issued = true;
        }
    }

    if (write_issued) {
        this->table.record_write(rmw_addr);
        entry_pair->second.assign_callback(front_req.callback);
        lsq.queue.pop_front();
        cnt_events["write_access"]++;
    }
}

void ait_controller::tick_internal_buffer(clk_t curr_clk)
{
    for (auto &entry_pair : this->buffer.entry_map) {
        auto curr_block_addr = entry_pair.first;
        auto &entry          = entry_pair.second;

        if (entry.state == request_state::init)
            goto ait_buffer_tick_internal_state_transfer;

        if (!entry.pending)
            continue;

        if (entry.waiting_action_clk_update)
            continue;

        if (entry.next_action_clk == clk_invalid)
            throw std::runtime_error("Internal error, the next_action_clk is invalid ("
                                     + std::to_string(entry.next_action_clk) + ")");

        if (entry.next_action_clk > curr_clk)
            continue;

    ait_buffer_tick_internal_state_transfer:
        auto &func = this->state_trans[int(entry.pending_request.type)][int(entry.state)];
        if (func == nullptr) {
            throw std::runtime_error("Internal error, unknown state transfer.");
        }
        func(curr_block_addr, entry, curr_clk);
    }
}

bool ait_controller::check_and_evict()
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

void ait_controller::tick_lmemq(clk_t curr_clk)
{
    if (lmemq.empty())
        return;

    if (!lmemq_state.pending_front) {
        /* Setup to process a new req */
        if (lmemq_state.subreq_served[0])
            lmemq.queue.pop_front();
        lmemq_state.pending_front        = true;
        lmemq_state.subreq_pending_index = -1; /* Indicating it's at setup stage */
    }

    if (this->local_memory_model->full())
        return;

    auto &front_req = lmemq.queue.front();

    /* Continue process the unfinished front req */
    if (lmemq_state.subreq_pending_index == -1) {
    } else {
        if (!lmemq_state.subreq_served[lmemq_state.subreq_pending_index]) {
            /* Current pending sub request is not finished, wait on it */
            return;
        }
    }

    assert(lmemq_state.subreq_pending_index <= 3);
    assert(lmemq_state.subreq_pending_index >= -1);

    if (lmemq_state.subreq_pending_index == lmemq_state.subreq_cnt - 1) {
        /* The final sub request is finished */

        /* Update ait_buffer entry */
        block_addr_t ait_addr                                   = vans::ait::translate_to_block_addr(front_req.addr);
        buffer.entry_map.at(ait_addr).next_action_clk           = curr_clk + 1;
        buffer.entry_map.at(ait_addr).waiting_action_clk_update = false;

        lmemq_state.pending_front = false;
        lmemq.queue.pop_front();

        for (auto i = 0; i < lmemq_state.subreq_cnt; i++) {
            lmemq_state.subreq_served[i] = false;
        }
    } else {
        /* Start next sub request */
        lmemq_state.subreq_pending_index++;
        block_addr_t ait_addr   = translate_to_block_addr(front_req.addr);
        auto &entry             = this->buffer.entry_map.at(ait_addr);
        logic_addr_t cl_addr = front_req.addr + lmemq_state.subreq_pending_index * cpu_cl_size;
        auto req_type        = front_req.type;
        auto callback        = [this](logic_addr_t logic_addr, clk_t curr_clk) {
            int offset                              = rmw::block_offset_cl(logic_addr);
            this->lmemq_state.subreq_served[offset] = true;
        };

        base_request req(req_type, cl_addr, curr_clk, callback);

        auto [issued, deterministic, next_clk] = this->local_memory_model->issue_request(req);

        if (!issued) {
            throw std::runtime_error(
                "AIT controller feature not implemented: ait do not retry issuing request to local memory");
        } else {
            if (req_type == base_request_type::write) {
                callback(cl_addr, curr_clk);
                cnt_events["lmem_write_access"]++;
            } else {
                cnt_events["lmem_read_access"]++;
            }
        }
    }
}
} // namespace vans::ait
