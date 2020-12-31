#ifndef VANS_VANS_H
#define VANS_VANS_H

#include <functional>
#include <memory>
#include <queue>
#include <string>
#include <tuple>
#include <unordered_map>

#include "mem/abstract_mem.hh"
#include "mem/qport.hh"
#include "params/VANS.hh"
#include "vans/src/gem5/wrapper.h"
#include "vans/src/general/config.h"
#include "vans/src/general/utils.h"


namespace vans
{
class base_request;
class gem5_wrapper;
} // namespace vans

using vans::base_request;
using vans::clk_t;
using vans::gem5_wrapper;
using vans::logic_addr_t;

class VANS : public AbstractMemory
{
  private:
    class vans_port : public ResponsePort
    {
      private:
        VANS &mem;

      public:
        vans_port(const std::string &_name, VANS &_mem) : ResponsePort(_name, &_mem), mem(_mem) {}

      protected:
        Tick recvAtomic(PacketPtr pkt)
        {
            return mem.recvAtomic(pkt);
        }

        void recvFunctional(PacketPtr pkt)
        {
            mem.recvFunctional(pkt);
        }

        bool recvTimingReq(PacketPtr pkt)
        {
            return mem.recvTimingReq(pkt);
        }

        void recvRespRetry()
        {
            mem.recvRespRetry();
        }

        AddrRangeList getAddrRanges() const
        {
            AddrRangeList ranges;
            ranges.push_back(mem.getAddrRange());
            return ranges;
        }
    } port;

    size_t reqs_in_flight;
    std::unordered_map<Addr, std::queue<PacketPtr>> reads;
    std::unordered_map<Addr, std::queue<PacketPtr>> writes;
    std::deque<PacketPtr> resp_queue;
    std::unique_ptr<Packet> pending_del;

    std::string config_file_path;
    gem5_wrapper *wrapper;
    std::function<void(logic_addr_t, clk_t)> read_callback;
    std::function<void(logic_addr_t, clk_t)> write_callback;

    Tick ticks_per_clk;
    bool resp_stall;
    bool req_stall;

    unsigned int num_outstanding() const
    {
        return reqs_in_flight + resp_queue.size();
    }

    void sendResponse();
    void tick();

    EventFunctionWrapper send_resp_event;
    EventFunctionWrapper tick_event;

  public:
    typedef VANSParams Params;
    VANS(const Params *p);

    virtual void init();
    virtual void startup();
    DrainState drain() override;
    virtual Port &getPort(const std::string &if_name, PortID idx = InvalidPortID);
    virtual ~VANS();

  protected:
    Tick recvAtomic(PacketPtr pkt);
    void recvFunctional(PacketPtr pkt);
    bool recvTimingReq(PacketPtr pkt);
    void recvRespRetry();
    void accessAndRespond(PacketPtr pkt);

    void readComplete(logic_addr_t addr, clk_t curr_clk);
    void writeComplete(logic_addr_t addr, clk_t curr_clk);
};

#endif // VANS_VANS_H
