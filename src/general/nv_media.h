#ifndef VANS_NV_MEDIA_H
#define VANS_NV_MEDIA_H

#include "static_memory.h"

namespace vans
{
class nv_media : public static_memory
{
  public:
    nv_media() = delete;
    explicit nv_media(const config &cfg) : static_memory(cfg) {}
};
} // namespace vans


#endif // VANS_NV_MEDIA_H
