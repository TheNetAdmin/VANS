#ifndef VANS_TRACE_H
#define VANS_TRACE_H

#include "component.h"
#include "config.h"
#include <memory>
#include <string>

namespace vans::trace
{

class trace
{
  private:
    std::ifstream file;
    std::string name;

  public:
    trace()              = delete;
    trace(const trace &) = delete;

    explicit trace(const std::string &filename) : file(filename), name(filename)
    {
        if (!file.good()) {
            throw std::runtime_error("Trace file open failed.");
        }
    }

    virtual ~trace() = default;

    bool get_dram_trace_request(logic_addr_t &addr, base_request_type &type, bool &critical, clk_t &idle_clk_injection);
};

void run_trace(root_config &cfg, std::string &trace_filename, std::shared_ptr<base_component> model);

} // namespace vans::trace

#endif // VANS_TRACE_H
