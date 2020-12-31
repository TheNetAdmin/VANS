#ifndef VANS_COMMON_H
#define VANS_COMMON_H

#include <cstdint>
#include <functional>
#include <limits>

namespace vans
{

using clk_t         = uint64_t;
using addr_t        = uint64_t;
using logic_addr_t  = uint64_t;
using addr_offset_t = uint64_t;

/* Compile time constants: https://stackoverflow.com/a/40405554 */
enum : clk_t { clk_invalid = std::numeric_limits<uint64_t>::max() };
enum : size_t { size_invalid = std::numeric_limits<size_t>::max() };
enum : addr_t { addr_invalid = std::numeric_limits<uint64_t>::max() };
enum : size_t { cpu_cl_size = 64, cpu_cl_bitshift = 6, /* Log2(CPU_CL_SIZE) --> Log2(64) */ };

using base_callback_f = std::function<void(logic_addr_t, clk_t)>;
enum class base_request_type { read, write };

class base_request
{
  public:
    logic_addr_t addr = addr_invalid;
    clk_t arrive      = clk_invalid;
    clk_t depart      = clk_invalid;

    base_request_type type;

    base_callback_f callback;

    /* Methods */
    base_request() = delete;
    base_request(base_request_type type, logic_addr_t addr, clk_t arrive, base_callback_f callback = nullptr) :
        addr(addr), arrive(arrive), type(type), callback(std::move(callback))
    {
    }
};

/* base_response: [(field_index) field_name: field_value] reason
 * [(0) issued: false]
 *   if request not issued
 * [(0) issued: true, (1) deterministic: true , (2) next_clk: a valid clk_t value]
 *   if issued and the request is guaranteed to be served at `next_clk`
 * [(0) issued: true, (1) deterministic: false, (2) next_clk: `clk_invalid`      ]
 *   if issued and the request is dynamically served, which will call a callback function once served
 */
using base_response = std::tuple<bool, bool, clk_t>;
} // namespace vans

#endif // VANS_COMMON_H
