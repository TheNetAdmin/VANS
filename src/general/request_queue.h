
#ifndef VANS_REQUEST_QUEUE_H
#define VANS_REQUEST_QUEUE_H

#include "common.h"
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


struct base_request_queue : public request_queue<base_request> {
    base_request_queue() = delete;
    explicit base_request_queue(size_t max_entries) : request_queue(max_entries) {}
};

} // namespace vans

#endif // VANS_REQUEST_QUEUE_H
