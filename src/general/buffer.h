#ifndef VANS_BUFFER_H
#define VANS_BUFFER_H

#include <algorithm>
#include <stdexcept>
#include <unordered_map>

#include "utils.h"

namespace vans
{

// C++17 feature template<auto>:
//   https://stackoverflow.com/questions/24185315/passing-any-function-as-template-parameter
template <typename AddrType, typename EntryType, auto AddrFunc, typename... ArgTypes> struct internal_buffer {
    std::unordered_map<AddrType, EntryType> entry_map;
    size_t max_entries;

    size_t next_available_index = 0;

    explicit internal_buffer(size_t max_entries) : max_entries(max_entries)
    {
        entry_map.reserve(max_entries);
    }

    decltype(entry_map.emplace().first) insert(AddrType addr, ArgTypes const &...args)
    {
        /* Insert new entry
         * Avoid copy/move, according to https://en.cppreference.com/w/cpp/container/map/emplace
         * Parameter pack with forward_as_tuple, according to https://stackoverflow.com/a/52376368
         */
        auto ret = entry_map.emplace(
            std::piecewise_construct, std::forward_as_tuple(AddrFunc(addr)), std::forward_as_tuple(args...));

        /* Make sure we don't misuse `emplace()` function:
         *   emplace can run on existing key, without throwing exception, and won't change the value
         *   but check the return value's second is true/false can determine if it's already exist
         */
        if (!ret.second) {
            throw std::runtime_error("Internal error, insert to an existing entry.");
        }

        if (entry_map.size() > max_entries) {
            throw std::runtime_error("Internal error, insert to a full rmw.");
        }

        if (this->next_available_index > max_entries) {
            throw std::runtime_error("Internal error, buffer local index out of range: "
                                     + std::to_string(this->next_available_index) + " > "
                                     + std::to_string(this->max_entries) + " for addr [" + std::to_string(addr) + "]");
        }

        ret.first->second.buffer_index = this->next_available_index;

        this->next_available_index++;

        return ret.first;
    }

    decltype(entry_map.begin()) find(AddrType logic_addr)
    {
        return entry_map.find(AddrFunc(logic_addr));
    }

    decltype(entry_map.end()) end()
    {
        return entry_map.end();
    }

    decltype(entry_map.erase(0x0)) erase(AddrType logic_addr)
    {
        this->next_available_index--;
        return entry_map.erase(AddrFunc(logic_addr));
    }

    bool full()
    {
        return entry_map.size() >= max_entries;
    }

    bool empty()
    {
        return entry_map.empty();
    }

    bool pending()
    {
        return std::any_of(entry_map.begin(), entry_map.end(), [](const auto &entry) { return entry.second.pending; });
    }

    bool dirty()
    {
        return std::any_of(entry_map.begin(), entry_map.end(), [](const auto &entry) { return entry.second.dirty; });
    }
};
} // namespace vans

#endif // VANS_BUFFER_H
