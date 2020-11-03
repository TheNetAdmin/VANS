/*
 * VANS DRAM and DDR4 design is inspired by and developed based on Ramulator.
 * We modified the original Ramulator and developed VANS DRAM component.
 * You may find more details and more DRAM models in the original Ramulator code base.
 * Thanks to their hard work.
 * */

#ifndef VANS_DRAM_H
#define VANS_DRAM_H

#include "controller.h"
#include "utils.h"
#include <deque>
#include <functional>
#include <iostream>
#include <map>
#include <vector>

namespace vans::dram
{

template <typename T> class DRAM : public tick_able
{
  public:
    using state               = typename T::state;
    using level               = typename T::level;
    using command             = typename T::command;
    using timing_table_t      = typename T::timing_table_t;
    using state_trans_table_t = typename T::state_trans_table_t;
    using prereq_table_t      = typename T::prereq_table_t;

    std::shared_ptr<T> spec;
    state curr_state;
    level curr_level;
    std::map<int, state> row_state;

    size_t id;
    uint64_t size;
    DRAM *parent;
    std::vector<DRAM *> children;

  private:
    clk_t curr_clk = 0;
    clk_t next[T::total_commands];
    std::deque<clk_t> prev[T::total_commands];

    const timing_table_t *timing_table;
    const state_trans_table_t *state_trans_table;
    const prereq_table_t *prereq_table;

  public:
    DRAM()             = delete;
    DRAM(const DRAM &) = delete;
    DRAM(std::shared_ptr<T> spec, level l) :
        spec(spec),
        curr_level(l),
        id(0),
        parent(nullptr),
        curr_state(spec->init_state[int(l)]),
        timing_table(spec->timing_table[int(l)]),
        state_trans_table(spec->state_trans_table[int(l)]),
        prereq_table(spec->prereq_table[int(l)])
    {
        for (int i = 0; i < T::total_commands; i++) {
            next[i]  = 0;
            int dist = 0;
            for (auto &t : timing_table[i])
                dist = std::max(dist, t.dist);
            if (dist)
                prev[i].resize(dist, -1);
        }

        int child_level = int(l) + 1;
        if (child_level >= T::total_inst_levels)
            return;

        size_t child_count = spec->count[child_level];
        if (0 == child_count)
            return;

        for (size_t i = 0; i < child_count; ++i) {
            auto child    = new DRAM<T>(spec, level(child_level));
            child->parent = this;
            child->id     = i;
            this->children.push_back(child);
        }
    }

    void tick(clk_t clk) final {}

    virtual ~DRAM()
    {
        for (auto child : children)
            delete child;
    }

    command decode(command cmd, const long unsigned int *addr)
    {
        auto child_id = addr[int(curr_level) + 1];
        if (prereq_table[int(cmd)]) {
            auto pcmd = prereq_table[int(cmd)](this, cmd, child_id);
            if (pcmd != command::undefined) {
                return pcmd;
            }
        }

        if (child_id < 0 || children.size() == 0)
            return cmd;


        return children[child_id]->decode(cmd, addr);
    }

    bool check(command cmd, addr_t addr, clk_t clk)
    {
        if (next[int(cmd)] != clk_invalid && clk < next[int(cmd)]) {
            return false; // Busy, not ready for next command
        }

        /* Not busy*/
        auto child_id = addr[int(curr_level) + 1];
        if (child_id < 0 || curr_level == spec->scope[int(cmd)] || !children.size())
            return true;

        return children[child_id]->check(cmd, addr, clk);
    }

    clk_t get_next(command cmd, const addr_t addr)
    {
        clk_t next_clk = max(curr_clk, next[int(cmd)]);
        auto node      = this;
        for (int l = int(curr_level); l < int(spec->scope[int(cmd)]) && node->children.size() && addr[l + 1] >= 0;
             l++) {
            node     = node->children[addr[l + 1]];
            next_clk = max(next_clk, node->next[int(cmd)]);
        }
        return next_clk;
    }

    void update(command cmd, addr_t addr, clk_t clk)
    {
        this->curr_clk = clk;
        update_state(cmd, addr);
        update_timing(cmd, addr, clk);
    }

    void update_state(command cmd, addr_t addr)
    {
        auto child_id = addr[int(curr_level) + 1];
        if (state_trans_table[int(cmd)])
            state_trans_table[int(cmd)](this, child_id);

        if (curr_level == spec->scope[int(cmd)] || !children.size())
            return;

        children[child_id]->update_state(cmd, addr);
    }
    void update_timing(command cmd, addr_t addr, clk_t clk)
    {

        if (this->id != addr[int(curr_level)]) {
            for (auto &t : timing_table[int(cmd)]) {
                if (false == t.has_sibling)
                    continue;

                clk_t next_clk   = clk + t.delay;
                next[int(t.cmd)] = std::max(next[int(t.cmd)], next_clk);
            }
        } else {
            if (prev[int(cmd)].size()) {
                prev[int(cmd)].pop_back();
                prev[int(cmd)].push_front(clk);
            }
            for (auto &t : timing_table[int(cmd)]) {
                if (true == t.has_sibling)
                    continue;

                clk_t past_clk = prev[int(cmd)][t.dist - 1];
                if (past_clk < 0)
                    continue;

                clk_t next_clk   = past_clk + t.delay;
                next[int(t.cmd)] = std::max(next[int(t.cmd)], next_clk);
            }

            if (!children.size())
                return;

            for (auto c : children)
                c->update_timing(cmd, addr, clk);
        }
    }

    void update_serving_requests(addr_t addr, int delta, clk_t clk) {}
};


} // namespace vans::dram

#endif // VANS_DRAM_H
