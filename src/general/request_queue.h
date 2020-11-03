
#ifndef VANS_REQUEST_QUEUE_H
#define VANS_REQUEST_QUEUE_H

#include "utils.h"
#include <deque>
#include <functional>
#include <utility>

namespace vans
{

template <typename RequestType> struct request_queue {
    std::deque<RequestType> queue;
    size_t max_entries;

    request_queue() = delete;
    explicit request_queue(size_t max_entries) : max_entries(max_entries) {}

    [[nodiscard]] bool full() const
    {
        auto sz = queue.size();
        if (sz > max_entries)
            throw std::runtime_error("Internal error: queue overflow, " + std::to_string(sz) + " > "
                                     + std::to_string(max_entries));
        return sz == max_entries;
    }

    [[nodiscard]] bool empty() const
    {
        return queue.empty();
    }

    [[nodiscard]] bool pending() const
    {
        return !empty();
    }

    [[nodiscard]] bool size() const
    {
        return queue.size();
    }

    bool enqueue(RequestType &req)
    {
        if (full())
            return false;

        queue.push_back(req);
        return true;
    }
};

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

struct base_request_queue : public request_queue<base_request> {
    base_request_queue() = delete;
    explicit base_request_queue(size_t max_entries) : request_queue(max_entries) {}
};

} // namespace vans

#endif // VANS_REQUEST_QUEUE_H
