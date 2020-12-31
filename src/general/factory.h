#ifndef VANS_FACTORY_H
#define VANS_FACTORY_H

#include "component.h"
#include "config.h"

namespace vans::factory
{

std::shared_ptr<base_component>
make_single_component(const std::string &name, const root_config &cfg, unsigned component_id);

/* Recursively make component */
std::shared_ptr<base_component>
make_component(const std::string &name, const root_config &cfg, unsigned component_id = 0);

std::shared_ptr<base_component> make(const root_config &cfg);

} // namespace vans::factory

#endif // VANS_FACTORY_H
