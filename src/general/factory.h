#ifndef VANS_FACTORY_H
#define VANS_FACTORY_H

#include "ait.h"
#include "component.h"
#include "imc.h"
#include "nv_media.h"
#include "rmc.h"
#include "rmw.h"
#include "utils.h"

namespace vans::factory
{

std::shared_ptr<base_component>
make_single_component(const std::string &name, const root_config &cfg, unsigned component_id)
{
    std::shared_ptr<base_component> ret;
    if (name == "rmc") {
        ret = std::make_shared<rmc::rmc>(cfg["rmc"]);
    } else if (name == "imc") {
        ret = std::make_shared<imc::imc>(cfg["imc"]);
    } else if (name == "rmw") {
        ret = std::make_shared<rmw::rmw>(cfg["rmw"]);
    } else if (name == "ait") {
        ret = std::make_shared<ait::ait>(cfg["ait"]);
    } else if (name == "nv_media") {
        ret = std::make_shared<nv_media>(cfg["nv_media"]);
    }
    return ret;
}

/* Recursively make component */
std::shared_ptr<base_component>
make_component(const std::string &name, const root_config &cfg, unsigned component_id = 0)
{
    auto ret = make_single_component(name, cfg, component_id);
    auto org = cfg.get_organization(name);
    if (org.count != 0) {
        for (auto i = 0; i < org.count; i++) {
            auto next = make_component(org.type, cfg, i);
            ret->connect_next(next);
        }
        if (name == "rmw") {
            auto dumper =
                std::make_shared<vans::dumper>(get_dump_type(cfg), get_dump_filename(cfg, "stat_dump", component_id));
            ret->connect_dumper(dumper);
        }
    }
    return ret;
}

std::shared_ptr<base_component> make(const root_config &cfg)
{
    /* Return a single virtual root memory controller */
    return make_component("rmc", cfg);
}

} // namespace vans::factory

#endif // VANS_FACTORY_H
