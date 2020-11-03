#include "ddr4.h"
#include "dram.h"

namespace vans::dram::ddr
{

DDR4::DDR4(struct timing t) : timing(t), read_latency(t.nCL + t.nBL)
{
    init_timing_table();
    init_state_trans_table();
    init_prereq_table();
}

void DDR4::init_state_trans_table()
{
    using s = state;
    using l = level;
    using c = command;

    state_trans_table[int(l::bank)][int(c::ACT)] = [](DRAM<DDR4> *d, int id) {
        d->curr_state    = s::opened;
        d->row_state[id] = s::opened;
    };
    state_trans_table[int(l::bank)][int(c::PRE)] = [](DRAM<DDR4> *d, int id) {
        d->curr_state = s::closed;
        d->row_state.clear();
    };
    state_trans_table[int(l::rank)][int(c::PREA)] = [](DRAM<DDR4> *d, int id) {
        for (auto group : d->children) {
            for (auto bank : group->children) {
                bank->curr_state = s::closed;
                bank->row_state.clear();
            }
        }
    };
    state_trans_table[int(l::rank)][int(c::REF)] = [](DRAM<DDR4> *d, int id) {};
    state_trans_table[int(l::bank)][int(c::RD)]  = [](DRAM<DDR4> *d, int id) {};
    state_trans_table[int(l::bank)][int(c::WR)]  = [](DRAM<DDR4> *d, int id) {};
    state_trans_table[int(l::bank)][int(c::RDA)] = [](DRAM<DDR4> *d, int id) {
        d->curr_state = s::closed;
        d->row_state.clear();
    };
    state_trans_table[int(l::bank)][int(c::WRA)] = [](DRAM<DDR4> *d, int id) {
        d->curr_state = s::closed;
        d->row_state.clear();
    };
    state_trans_table[int(l::rank)][int(c::PDE)] = [](DRAM<DDR4> *d, int id) {
        for (auto group : d->children) {
            for (auto bank : group->children) {
                if (bank->curr_state == s::closed)
                    continue;
                d->curr_state = s::act_pwr_down;
                return;
            }
        }
        d->curr_state = s::pre_pwr_down;
    };
    state_trans_table[int(l::rank)][int(c::PDX)] = [](DRAM<DDR4> *d, int id) { d->curr_state = s::pwr_up; };
    state_trans_table[int(l::rank)][int(c::SRE)] = [](DRAM<DDR4> *d, int id) { d->curr_state = s::self_refresh; };
    state_trans_table[int(l::rank)][int(c::SRX)] = [](DRAM<DDR4> *d, int id) { d->curr_state = s::pwr_up; };
}

void DDR4::at(level lev, command prev, struct timing_entry t)
{
    this->timing_table[int(lev)][int(prev)].push_back(t);
}

void DDR4::init_timing_table()
{
    const struct timing &t = this->timing;
    using l                = level;
    using c                = command;

    /* Channel */
    // CAS <-> CAS
    at(l::channel, c::RD, {c::RD, t.nBL});
    at(l::channel, c::RD, {c::RDA, t.nBL});
    at(l::channel, c::RDA, {c::RD, t.nBL});
    at(l::channel, c::RDA, {c::RDA, t.nBL});
    at(l::channel, c::WR, {c::WR, t.nBL});
    at(l::channel, c::WR, {c::WRA, t.nBL});
    at(l::channel, c::WRA, {c::WR, t.nBL});
    at(l::channel, c::WRA, {c::WRA, t.nBL});

    /* Rank */
    // CAS <-> CAS
    at(l::rank, c::RD, {c::RD, t.nCCDS});
    at(l::rank, c::RD, {c::RDA, t.nCCDS});
    at(l::rank, c::RDA, {c::RD, t.nCCDS});
    at(l::rank, c::RDA, {c::RDA, t.nCCDS});
    at(l::rank, c::WR, {c::WR, t.nCCDS});
    at(l::rank, c::WR, {c::WRA, t.nCCDS});
    at(l::rank, c::WRA, {c::WR, t.nCCDS});
    at(l::rank, c::WRA, {c::WRA, t.nCCDS});
    at(l::rank, c::RD, {c::WR, t.nCL + t.nBL + 2 - t.nCWL});
    at(l::rank, c::RD, {c::WRA, t.nCL + t.nBL + 2 - t.nCWL});
    at(l::rank, c::RDA, {c::WR, t.nCL + t.nBL + 2 - t.nCWL});
    at(l::rank, c::RDA, {c::WRA, t.nCL + t.nBL + 2 - t.nCWL});
    at(l::rank, c::WR, {c::RD, t.nCWL + t.nBL + t.nWTRS});
    at(l::rank, c::WR, {c::RDA, t.nCWL + t.nBL + t.nWTRS});
    at(l::rank, c::WRA, {c::RD, t.nCWL + t.nBL + t.nWTRS});
    at(l::rank, c::WRA, {c::RDA, t.nCWL + t.nBL + t.nWTRS});

    // CAS <-> CAS (between sibling ranks)
    at(l::rank, c::RD, {c::RD, t.nBL + t.nRTRS, true});
    at(l::rank, c::RD, {c::RDA, t.nBL + t.nRTRS, true});
    at(l::rank, c::RDA, {c::RD, t.nBL + t.nRTRS, true});
    at(l::rank, c::RDA, {c::RDA, t.nBL + t.nRTRS, true});
    at(l::rank, c::RD, {c::WR, t.nBL + t.nRTRS, true});
    at(l::rank, c::RD, {c::WRA, t.nBL + t.nRTRS, true});
    at(l::rank, c::RDA, {c::WR, t.nBL + t.nRTRS, true});
    at(l::rank, c::RDA, {c::WRA, t.nBL + t.nRTRS, true});
    at(l::rank, c::RD, {c::WR, t.nCL + t.nBL + t.nRTRS - t.nCWL, true});
    at(l::rank, c::RD, {c::WRA, t.nCL + t.nBL + t.nRTRS - t.nCWL, true});
    at(l::rank, c::RDA, {c::WR, t.nCL + t.nBL + t.nRTRS - t.nCWL, true});
    at(l::rank, c::RDA, {c::WRA, t.nCL + t.nBL + t.nRTRS - t.nCWL, true});
    at(l::rank, c::WR, {c::RD, t.nCWL + t.nBL + t.nRTRS - t.nCL, true});
    at(l::rank, c::WR, {c::RDA, t.nCWL + t.nBL + t.nRTRS - t.nCL, true});
    at(l::rank, c::WRA, {c::RD, t.nCWL + t.nBL + t.nRTRS - t.nCL, true});
    at(l::rank, c::WRA, {c::RDA, t.nCWL + t.nBL + t.nRTRS - t.nCL, true});

    at(l::rank, c::RD, {c::PREA, t.nRTP});
    at(l::rank, c::WR, {c::PREA, t.nCWL + t.nBL + t.nWR});

    // CAS <-> PD
    at(l::rank, c::RD, {c::PDE, t.nCL + t.nBL + 1});
    at(l::rank, c::RDA, {c::PDE, t.nCL + t.nBL + 1});
    at(l::rank, c::WR, {c::PDE, t.nCWL + t.nBL + t.nWR});
    at(l::rank, c::WRA, {c::PDE, t.nCWL + t.nBL + t.nWR + 1}); // +1 for pre
    at(l::rank, c::PDX, {c::RD, t.nXP});
    at(l::rank, c::PDX, {c::RDA, t.nXP});
    at(l::rank, c::PDX, {c::WR, t.nXP});
    at(l::rank, c::PDX, {c::WRA, t.nXP});

    // CAS <-> SR: undefined (all banks have to be precharged)

    // RAS <-> RAS
    at(l::rank, c::ACT, {c::ACT, t.nRRDS});
    at(l::rank, c::ACT, {c::ACT, t.nFAW, false, 4});
    at(l::rank, c::ACT, {c::PREA, t.nRAS});
    at(l::rank, c::PREA, {c::ACT, t.nRP});

    // RAS <-> REF
    at(l::rank, c::PRE, {c::REF, t.nRP});
    at(l::rank, c::PREA, {c::REF, t.nRP});
    at(l::rank, c::RDA, {c::REF, t.nRTP + t.nRP});
    at(l::rank, c::WRA, {c::REF, t.nCWL + t.nBL + t.nWR + t.nRP});
    at(l::rank, c::REF, {c::ACT, t.nRFC});

    // RAS <-> PD
    at(l::rank, c::ACT, {c::PDE, 1});
    at(l::rank, c::PDX, {c::ACT, t.nXP});
    at(l::rank, c::PDX, {c::PRE, t.nXP});
    at(l::rank, c::PDX, {c::PREA, t.nXP});

    // RAS <-> SR
    at(l::rank, c::PRE, {c::SRE, t.nRP});
    at(l::rank, c::PREA, {c::SRE, t.nRP});
    at(l::rank, c::SRX, {c::ACT, t.nXS});

    // REF <-> REF
    at(l::rank, c::REF, {c::REF, t.nRFC});

    // REF <-> PD
    at(l::rank, c::REF, {c::PDE, 1});
    at(l::rank, c::PDX, {c::REF, t.nXP});

    // REF <-> SR
    at(l::rank, c::SRX, {c::REF, t.nXS});

    // PD <-> PD
    at(l::rank, c::PDE, {c::PDX, t.nPD});
    at(l::rank, c::PDX, {c::PDE, t.nXP});

    // PD <-> SR
    at(l::rank, c::PDX, {c::SRE, t.nXP});
    at(l::rank, c::SRX, {c::PDE, t.nXS});

    // SR <-> SR
    at(l::rank, c::SRE, {c::SRX, t.nCKESR});
    at(l::rank, c::SRX, {c::SRE, t.nXS});

    /* Bank Group */
    // CAS <-> CAS
    at(l::bank_group, c::RD, {c::RD, t.nCCDL});
    at(l::bank_group, c::RD, {c::RDA, t.nCCDL});
    at(l::bank_group, c::RDA, {c::RD, t.nCCDL});
    at(l::bank_group, c::RDA, {c::RDA, t.nCCDL});
    at(l::bank_group, c::WR, {c::WR, t.nCCDL});
    at(l::bank_group, c::WR, {c::WRA, t.nCCDL});
    at(l::bank_group, c::WRA, {c::WR, t.nCCDL});
    at(l::bank_group, c::WRA, {c::WRA, t.nCCDL});
    at(l::bank_group, c::WR, {c::RD, t.nCWL + t.nBL + t.nWTRL});
    at(l::bank_group, c::WR, {c::RDA, t.nCWL + t.nBL + t.nWTRL});
    at(l::bank_group, c::WRA, {c::RD, t.nCWL + t.nBL + t.nWTRL});
    at(l::bank_group, c::WRA, {c::RDA, t.nCWL + t.nBL + t.nWTRL});

    // RAS <-> RAS
    at(l::bank_group, c::ACT, {c::ACT, t.nRRDL});

    /* Bank */
    // CAS <-> RAS
    at(l::bank, c::ACT, {c::RD, t.nRCD});
    at(l::bank, c::ACT, {c::RDA, t.nRCD});
    at(l::bank, c::ACT, {c::WR, t.nRCD});
    at(l::bank, c::ACT, {c::WRA, t.nRCD});

    at(l::bank, c::RD, {c::PRE, t.nRTP});
    at(l::bank, c::WR, {c::PRE, t.nCWL + t.nBL + t.nWR});

    at(l::bank, c::RDA, {c::ACT, t.nRTP + t.nRP});
    at(l::bank, c::WRA, {c::ACT, t.nCWL + t.nBL + t.nWR + t.nRP});

    // RAS <-> RAS
    at(l::bank, c::ACT, {c::ACT, t.nRC});
    at(l::bank, c::ACT, {c::PRE, t.nRAS});
    at(l::bank, c::PRE, {c::ACT, t.nRP});
}

void DDR4::init_prereq_table()
{
    auto &t = this->prereq_table;
    using l = level;
    using c = command;
    using s = state;

    t[int(l::rank)][int(c::RD)] = [](DRAM<DDR4> *d, command cmd, int id) {
        switch (d->curr_state) {
        case s::pwr_up:
            return c::undefined;
        case s::act_pwr_down:
            return c::PDX;
        case s::pre_pwr_down:
            return c::PDX;
        case s::self_refresh:
            return c::SRX;
        default:
            throw std::runtime_error("Wrong prereq triggered.");
        }
    };
    t[int(l::rank)][int(c::WR)] = t[int(l::rank)][int(c::RD)];
    t[int(l::bank)][int(c::RD)] = [](DRAM<DDR4> *d, command cmd, int id) {
        switch (d->curr_state) {
        case s::closed:
            return c::ACT;
        case s::opened:
            if (d->row_state.find(id) != d->row_state.end()) {
                return cmd;
            } else {
                return c::PRE;
            }
        default:
            throw std::runtime_error("Wrong prereq triggered.");
        }
    };
    t[int(l::bank)][int(c::WR)] = t[int(l::bank)][int(c::RD)];

    t[int(l::rank)][int(c::REF)] = [](DRAM<DDR4> *d, command cmd, int id) {
        for (auto group : d->children) {
            for (auto bank : group->children) {
                if (bank->curr_state == s::closed)
                    continue;
                return c::PREA;
            }
        }
        return c::REF;
    };

    t[int(l::rank)][int(c::PDE)] = [](DRAM<DDR4> *d, command cmd, int id) {
        switch (d->curr_state) {
        case s::pwr_up:
            return c::PDE;
        case s::act_pwr_down:
            return c::PDE;
        case s::pre_pwr_down:
            return c::PDE;
        case s::self_refresh:
            return c::SRX;
        default:
            throw std::runtime_error("Wrong prereq triggered.");
        }
    };

    t[int(l::rank)][int(c::SRE)] = [](DRAM<DDR4> *d, command cmd, int id) {
        switch (d->curr_state) {
        case s::pwr_up:
            return c::SRE;
        case s::act_pwr_down:
            return c::PDX;
        case s::pre_pwr_down:
            return c::PDX;
        case s::self_refresh:
            return c::SRE;
        default:
            throw std::runtime_error("Wrong prereq triggered.");
        }
    };
}

bool DDR4::is_opening(DDR4::command cmd)
{
    return cmd == command::ACT;
}

bool DDR4::is_accessing(DDR4::command cmd)
{
    switch (cmd) {
    case command::RD:
    case command::WR:
    case command::RDA:
    case command::WRA:
        return true;
    default:
        return false;
    }
}

bool DDR4::is_closing(DDR4::command cmd)
{
    switch (cmd) {
    case command::RDA:
    case command::WRA:
    case command::PRE:
    case command::PREA:
        return true;
    default:
        return false;
    }
}

bool DDR4::is_refreshing(DDR4::command cmd)
{
    return cmd == command::REF;
}

void DDR4::print_config()
{
    std::cout << "size:\t" << size << std::endl;
    std::cout << "data_width:\t" << data_width << std::endl;
    std::cout << "channel:\t" << count[int(level::channel)] << std::endl;
    std::cout << "rank:\t" << count[int(level::rank)] << std::endl;
    std::cout << "bank_group:\t" << count[int(level::bank_group)] << std::endl;
    std::cout << "bank:\t" << count[int(level::bank)] << std::endl;
    std::cout << "row:\t" << count[int(level::row)] << std::endl;
    std::cout << "col:\t" << count[int(level::col)] << std::endl;
}

} // namespace vans::dram::ddr
