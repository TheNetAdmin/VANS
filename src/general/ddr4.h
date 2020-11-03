#ifndef VANS_DDR4_H
#define VANS_DDR4_H

#include "config.h"
#include "dram.h"
#include "dram_memory.h"

namespace vans::dram::ddr
{

struct timing {
    int rate;
    double freq;
    double tCK;
    int nBL, nCCDS, nCCDL, nRTRS;
    int nCL, nRCD, nRP, nCWL;
    int nRAS, nRC;
    int nRTP, nWTRS, nWTRL, nWR;
    int nPD, nXP, nXPDLL;
    int nCKESR, nXSDLL;
    /* Extra timings */
    int nRRDS, nRRDL, nFAW, nRFC, nREFI, nXS;

    explicit timing(const config &cfg)
    {
#define LOAD_INT(name)   name = stoi(cfg.get_string(#name));
#define LOAD_FLOAT(name) name = stof(cfg.get_string(#name));
        LOAD_INT(rate)
        LOAD_FLOAT(freq)
        LOAD_FLOAT(tCK)
        LOAD_INT(nBL)
        LOAD_INT(nCCDS)
        LOAD_INT(nCCDL)
        LOAD_INT(nRTRS)
        LOAD_INT(nCL)
        LOAD_INT(nRCD)
        LOAD_INT(nRP)
        LOAD_INT(nCWL)
        LOAD_INT(nRAS)
        LOAD_INT(nRC)
        LOAD_INT(nRTP)
        LOAD_INT(nWTRS)
        LOAD_INT(nWTRL)
        LOAD_INT(nWR)
        LOAD_INT(nRRDS)
        LOAD_INT(nRRDL)
        LOAD_INT(nFAW)
        LOAD_INT(nRFC)
        LOAD_INT(nREFI)
        LOAD_INT(nPD)
        LOAD_INT(nXP)
        LOAD_INT(nXPDLL)
        LOAD_INT(nCKESR)
        LOAD_INT(nXS)
        LOAD_INT(nXSDLL)
#undef LOAD_INT
#undef LOAD_FLOAT
    }

    void print() const
    {
#define PRINT_TIMING(field) std::cout << #field << ":\t" << field << std::endl;
        PRINT_TIMING(rate);
        PRINT_TIMING(freq);
        PRINT_TIMING(tCK);
        PRINT_TIMING(nBL);
        PRINT_TIMING(nCCDS);
        PRINT_TIMING(nCCDL);
        PRINT_TIMING(nRTRS);
        PRINT_TIMING(nCL);
        PRINT_TIMING(nRCD);
        PRINT_TIMING(nRP);
        PRINT_TIMING(nCWL);
        PRINT_TIMING(nRAS);
        PRINT_TIMING(nRC);
        PRINT_TIMING(nRTP);
        PRINT_TIMING(nWTRS);
        PRINT_TIMING(nWTRL);
        PRINT_TIMING(nWR);
        PRINT_TIMING(nPD);
        PRINT_TIMING(nXP);
        PRINT_TIMING(nXPDLL);
        PRINT_TIMING(nCKESR);
        PRINT_TIMING(nXSDLL);
#undef PRINT_TIMING
    }
};

class DDR4
{
  public:
    /* States */
    static const size_t total_states = 6;

    enum class state {
        pwr_up,
        closed,
        self_refresh,
        pre_pwr_down,
        act_pwr_down,
        opened,
        undefined,
    };

    const std::map<state, const std::string> state_name = {
        {state::pwr_up, "Power On"},
        {state::closed, "Idle"},
        {state::self_refresh, "Self Refreshing"},
        {state::pre_pwr_down, "Precharge Power Down"},
        {state::act_pwr_down, "Active Power Down"},
        {state::opened, "Bank Active"},
    };

    /* Levels */
    static const size_t total_levels = 6;
    /* Levels need instance */
    static const size_t total_inst_levels = total_levels - 2;

    enum class level {
        channel,
        rank,
        bank_group,
        bank,
        row,
        col,
    };

    const std::map<level, const std::string> level_name = {
        {level::channel, "Channel"},
        {level::rank, "Rank"},
        {level::bank_group, "Bank Group"},
        {level::bank, "Bank"},
        {level::row, "Row"},
        {level::col, "Column"},
    };

    /* Commands */
    static const size_t total_commands = 12;

    enum class command {
        ACT,
        PRE,
        PREA,
        RD,
        WR,
        RDA,
        WRA,
        REF,
        PDE,
        PDX,
        SRE,
        SRX,
        undefined,
    };

    const std::map<command, const std::string> command_name = {
        {command::ACT, "ACT"},
        {command::PRE, "PRE"},
        {command::PREA, "PREA"},
        {command::RD, "RD"},
        {command::WR, "WR"},
        {command::RDA, "RDA"},
        {command::WRA, "WRA"},
        {command::REF, "REF"},
        {command::PDE, "PDE"},
        {command::PDX, "PDX"},
        {command::SRE, "SRE"},
        {command::SRX, "SRX"},
    };

    level scope[total_commands] = {level::row,
                                   level::bank,
                                   level::rank,
                                   level::col,
                                   level::col,
                                   level::col,
                                   level::col,
                                   level::rank,
                                   level::rank,
                                   level::rank,
                                   level::rank,
                                   level::rank};

    using req = dram::dram_media_request::req_type;

    const std::map<req, command> req_to_cmd = {
        {req::read, command::RD},
        {req::write, command::WR},
        {req::refresh, command::REF},
        {req::power_down, command::PDE},
        {req::self_refresh, command::SRE},
    };

    /* Transfer table entry */
    struct timing_entry {
        command cmd      = command::undefined;
        int delay        = 0;
        bool has_sibling = false;
        int dist         = 1;
    };

  public:
    /* Timing */
    using timing_type = struct timing;
    struct timing timing;
    /* Total size */
    uint64_t size;
    int data_width;
    /* Counts of components on each level */
    size_t count[total_levels];

    /* Init states */
    state init_state[total_levels] = {state::undefined, state::pwr_up, state::closed, state::closed, state::undefined};

    /* Tables */
    using timing_table_t = std::vector<struct timing_entry>;
    timing_table_t timing_table[total_levels][total_commands];

    using state_trans_table_t = std::function<void(DRAM<DDR4> *d, int id)>;
    state_trans_table_t state_trans_table[total_levels][total_commands];

    using prereq_table_t = std::function<command(DRAM<DDR4> *, command c, int id)>;
    prereq_table_t prereq_table[total_levels][total_commands];

    int read_latency;
    int prefetch_size = 8;
    int channel_width = 64;

  public:
    DDR4()             = delete;
    DDR4(const DDR4 &) = delete;
    explicit DDR4(struct timing t);

    static bool is_opening(command cmd);
    static bool is_closing(command cmd);
    static bool is_accessing(command cmd);
    static bool is_refreshing(command cmd);

    void print_config();

  private:
    void at(level l, command prev, struct timing_entry t);
    void init_timing_table();
    void init_state_trans_table();
    void init_prereq_table();
};


class ddr4_memory : public dram_memory<ddr::DDR4>
{
  public:
    ddr4_memory() = delete;
    explicit ddr4_memory(const config &cfg) : dram_memory(cfg) {}
};

} // namespace vans::dram::ddr

#endif // VANS_DDR4_H
