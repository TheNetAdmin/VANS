#ifndef VANS_GEM5_WRAPPER_H
#define VANS_GEM5_WRAPPER_H

#include "../general/common.h"
#include "../general/config.h"
#include <memory>
#include <string>

namespace vans
{
class base_component;

class gem5_wrapper
{
    std::shared_ptr<base_component> memory;

  public:
    /* GEM5 initiate two wrapper objects:
     *   - The first  one for address < 3GB
     *   - The second one for address >= 4GB
     * So use `vans_id` to initiate the correct object
     */
    static size_t vans_cnt;
    size_t vans_id;
    double tCK;
    size_t curr_clk = 0;

  public:
    gem5_wrapper(const std::string cfg_path);
    void tick();
    bool send(base_request req);
    void finish();
    clk_t get_clk()
    {
        return this->curr_clk;
    }
};

} // namespace vans


#endif // VANS_GEM5_WRAPPER_H
