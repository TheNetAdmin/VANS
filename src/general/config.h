
#ifndef VANS_CONFIG_H
#define VANS_CONFIG_H

#include <algorithm>
#include <exception>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>
#include <utility>

namespace vans
{

template <typename ValueType> class base_config
{
  public:
    std::unordered_map<std::string, ValueType> cfg;

    base_config() = default;

    ValueType &operator[](const std::string &key)
    {
        try {
            return cfg.at(key);
        } catch (std::out_of_range &e) {
            std::cerr << "Config value for key [" << key << "] not found" << std::endl;
            throw e;
        }
    }

    const ValueType &operator[](const std::string &key) const
    {
        try {
            return cfg.at(key);
        } catch (std::out_of_range &e) {
            std::cerr << "Config value for key [" << key << "] not found" << std::endl;
            throw e;
        }
    }

    decltype(cfg.at("")) at(const std::string &key)
    {
        try {
            return cfg.at(key);
        } catch (std::out_of_range &e) {
            std::cerr << "Config value for key [" << key << "] not found" << std::endl;
            throw e;
        }
    }
};

class config : public base_config<std::string>
{
  public:
    std::string section_name;
    config() = delete;

    explicit config(std::string section_name) : base_config<std::string>(), section_name(std::move(section_name)) {}

    bool check(const std::string &key) const
    {
        return this->cfg.count(key) == 1;
    }

    const std::string &get_string(const std::string &key) const
    {
        try {
            return this->cfg.at(key);
        } catch (std::out_of_range &e) {
            std::cerr << "Config value for key [" << key << "] not found under section [" << section_name << "]"
                      << std::endl;
            throw e;
        }
    }

    unsigned long get_ulong(const std::string &key) const
    {
        try {
            return std::stoul(this->cfg.at(key));
        } catch (std::out_of_range &e) {
            std::cerr << "Config value for key [" << key << "] not found under section [" << section_name << "]"
                      << std::endl;
            throw e;
        }
    }
};

class root_config : public base_config<config>
{
  public:
    root_config() = delete;
    explicit root_config(std::string &filename)
    {
        std::ifstream cfg_file(filename);
        if (!cfg_file.is_open()) {
            std::cerr << "vans::root_config: cannot open config file: " << filename << std::endl;
            exit(1);
        }

        std::string line;
        std::string curr_section;
        auto is_space = [](unsigned char const c) { return isspace(c); };
        while (getline(cfg_file, line)) {
            line.erase(remove_if(line.begin(), line.end(), is_space), line.end());
            if (line[0] == '#' || line[0] == ';' || line.empty()) {
                continue;
            }

            if (curr_section.empty() && line[0] != '[') {
                std::cerr << "vans::root_config: the first non-comment line must be a section name" << std::endl;
                exit(1);
            }

            if (line[0] == '[') {
                curr_section = line.substr(1, line.size() - 2);
                if (cfg.find(curr_section) == cfg.end()) {
                    cfg.emplace(curr_section, curr_section);
                }
                continue;
            }

            auto delimiter_pos = line.find(':');
            if (delimiter_pos == std::string::npos) {
                delimiter_pos = line.find('=');
                if (delimiter_pos == std::string::npos) {
                    throw std::runtime_error("Config format error: " + line);
                }
            }
            auto key   = line.substr(0, delimiter_pos);
            auto value = line.substr(delimiter_pos + 1);

            cfg.at(curr_section).cfg[key] = value;
        }
    }

    using organization = struct {
        int count;
        std::string type;
    };

    organization get_organization(const std::string &key) const
    {
        auto org_str       = cfg.at("organization")[key];
        auto delimiter_pos = org_str.find('*');
        if (delimiter_pos == std::string::npos) {
            throw std::runtime_error("Config format error: " + org_str);
        }
        auto count = std::stoi(org_str.substr(0, delimiter_pos));
        auto level = org_str.substr(delimiter_pos + 1);
        return {count, level};
    }
};

} // namespace vans

#endif // VANS_CONFIG_H
