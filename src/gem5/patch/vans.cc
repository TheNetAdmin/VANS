#include "mem/vans.hh"

#include "base/callback.hh"
#include "debug/vans.hh"
#include "mem/mem_object.hh"
#include "sim/system.hh"
#include "vans/src/gem5/wrapper.h"
#include "vans/src/general/common.h"

using std::bind;
using vans::logic_addr_t;

VANS::VANS(const Params *p) :
    AbstractMemory(p),
    port(name() + ".port", *this),
    reqs_in_flight(0),
    config_file_path(p->config_path),
    wrapper(nullptr),
    read_callback(bind(&VANS::readComplete, this, std::placeholders::_1, 0)),
    write_callback(bind(&VANS::writeComplete, this, std::placeholders::_1, 0)),
    ticks_per_clk(0),
    resp_stall(false),
    req_stall(false),
    send_resp_event([this] { sendResponse(); }, name()),
    tick_event([this] { tick(); }, name())
{
}

VANS::~VANS()
{
    delete wrapper;
}

void VANS::init()
{
    AbstractMemory::init();

    if (!port.isConnected()) {
        fatal("VANS port not connected\n");
    } else {
        port.sendRangeChange();
    }

    this->wrapper = new gem5_wrapper(this->config_file_path);

    ticks_per_clk = Tick(wrapper->tCK * SimClock::Float::ns);

    printf(
        "vans:"
        "Init VANS with config files in: %s\n",
        config_file_path.c_str());
    printf(
        "vans:"
        "\ttCK=%lf, %ld ticks per clk\n",
        wrapper->tCK,
        ticks_per_clk);

    registerExitCallback([this]() { this->wrapper->finish(); });
}

void VANS::startup()
{
    schedule(tick_event, clockEdge());
}

DrainState VANS::drain()
{
    // DPRINTF(vans, "Requested to drain\n");

    if (num_outstanding()) {
        return DrainState::Draining;
    } else {
        return DrainState::Drained;
    }
}

Port &VANS::getPort(const std::string &if_name, PortID idx)
{
    if (if_name != "port") {
        return AbstractMemory::getPort(if_name, idx);
    } else {
        return port;
    }
}

void VANS::sendResponse()
{
    assert(!resp_stall);
    assert(!resp_queue.empty());

    DPRINTF(vans, "Attempting to send response\n");

    if (port.sendTimingResp(resp_queue.front())) {
        auto addr = resp_queue.front()->getAddr();
        DPRINTF(vans, "Response to %ld sent.\n", addr);
        resp_queue.pop_front();

        if (!resp_queue.empty() && !send_resp_event.scheduled())
            schedule(send_resp_event, curTick());

        if (num_outstanding() == 0)
            signalDrainDone();
    } else {
        resp_stall = true;
    }
}

void VANS::tick()
{
    wrapper->tick();

    if (req_stall) {
        req_stall = false;
        port.sendRetryReq();
    }

    schedule(tick_event, curTick() + ticks_per_clk);
}


Tick VANS::recvAtomic(PacketPtr pkt)
{
    access(pkt);

    // The atomic mode does not actually use VANS simulation
    // 5ns is just an arbitrary latency here
    return pkt->cacheResponding() ? 0 : 5;
}

void VANS::recvFunctional(PacketPtr pkt)
{
    pkt->pushLabel(name());
    functionalAccess(pkt);
    for (auto i = resp_queue.begin(); i != resp_queue.end(); ++i)
        pkt->trySatisfyFunctional(*i);
    pkt->popLabel();
}

bool VANS::recvTimingReq(PacketPtr pkt)
{
    if (pkt->cacheResponding()) {
        pending_del.reset(pkt);
        return true;
    }

    if (req_stall)
        return false;

    bool accepted = true;

    vans::logic_addr_t addr = pkt->getAddr();

    if (pkt->isRead()) {
        vans::base_request req(vans::base_request_type::read, addr, wrapper->get_clk(), read_callback);
        accepted = wrapper->send(req);
        if (accepted) {
            reads[req.addr].push(pkt);
            if (req.addr > 0x100000000)
                DPRINTF(vans, "HI_MEM_READ%lx\n", req.addr);
            else
                DPRINTF(vans, "LO_MEM_READ%lx\n", req.addr);
            reqs_in_flight++;
        } else {
            req_stall = true;
        }
    } else if (pkt->isWrite()) {
        vans::base_request req(vans::base_request_type::write, addr, wrapper->get_clk(), write_callback);
        accepted = wrapper->send(req);
        if (accepted) {
            writes[req.addr].push(pkt);
            accessAndRespond(pkt);
            if (req.addr > 0x100000000)
                DPRINTF(vans, "HI_MEM_WRITE%lx\n", req.addr);
            else
                DPRINTF(vans, "LO_MEM_WRITE%lx\n", req.addr);
            reqs_in_flight++;
        } else {
            req_stall = true;
        }
    } else {
        accessAndRespond(pkt);
    }

    return accepted;
}

void VANS::recvRespRetry()
{
    DPRINTF(vans, "Retrying\n");

    assert(resp_stall);
    resp_stall = false;
    sendResponse();
}

void VANS::accessAndRespond(PacketPtr pkt)
{
    DPRINTF(vans, "Access for address %lld\n", pkt->getAddr());

    bool need_resp = pkt->needsResponse();

    access(pkt);

    if (need_resp) {
        assert(pkt->isResponse());

        Tick time        = curTick() + pkt->headerDelay + pkt->payloadDelay;
        pkt->headerDelay = pkt->payloadDelay = 0;

        DPRINTF(vans, "Queuing response for address %ld\n", pkt->getAddr());

        resp_queue.push_back(pkt);

        if (!resp_stall && !send_resp_event.scheduled())
            schedule(send_resp_event, time);
    } else {
        /* TODO: check this behaviour */
        pending_del.reset(pkt);
    }
}

void VANS::readComplete(logic_addr_t addr, clk_t curr_clk)
{
    DPRINTF(vans, "R %lx\n", addr);

    assert(reads.count(addr) != 0);
    auto &pkt_q = reads.find(addr)->second;

    PacketPtr pkt = pkt_q.front();
    pkt_q.pop();

    if (pkt_q.empty())
        reads.erase(addr);

    assert(reqs_in_flight > 0);
    reqs_in_flight--;

    accessAndRespond(pkt);
}

void VANS::writeComplete(logic_addr_t addr, clk_t curr_clk)
{
    DPRINTF(vans, "W %lx\n", addr);

    auto &pkt_q = writes.find(addr)->second;

    pkt_q.pop();

    if (pkt_q.empty())
        writes.erase(addr);

    assert(reqs_in_flight > 0);
    reqs_in_flight--;

    if (num_outstanding() == 0)
        signalDrainDone();

    // No need to call accessAndRespond() here,
    //   it is called in VANS::recvTimingReq()
}

VANS *VANSParams::create()
{
    return new VANS(this);
}
