
#ifndef VANS_TICK_H
#define VANS_TICK_H

#include "utils.h"

namespace vans
{

class tick_able
{
  public:
    /* Use a global clock signal from outside */
    virtual void tick(clk_t curr_clk) = 0;
};

} // namespace vans
#endif // VANS_TICK_H
