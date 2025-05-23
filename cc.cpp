// -*- c-basic-offset: 4; indent-tabs-mode: nil -*- 
#include <math.h>
#include <iostream>
#include <algorithm>
#include "cc.h"
#include "queue.h"
#include <stdio.h>
#include "switch.h"
#include "ecn.h"
using namespace std;

////////////////////////////////////////////////////////////////
//  CC SOURCE. Aici este codul care ruleaza pe transmitatori. Tot ce avem nevoie pentru a implementa
//  un algoritm de congestion control se gaseste aici.
////////////////////////////////////////////////////////////////

#define CUBIC_COEFFICIENT      0.30
#define BETA_COEFFICIENT       0.20

int CCSrc::_global_node_count = 0;
unsigned long minRoundTripTime = 0;
static auto congestionWindowChange = 0;
static double lastCongestionOrigin = 0.0;
static simtime_picosec epochStartTime = 0;
static auto ackCount = 0,
            maxWindowIncrease = 0;
static int windowAdjustmentDelta = 0;
static double lastWindowBeforeLoss = 0.0, 
             tcpFriendlyWindow = 0.0, 
             cubicTimeFactor = 0.0; 
static bool isTcpFriendly = true,
            useFastConvergence = true;

CCSrc::CCSrc(EventList &eventlist)
    : EventSource(eventlist,"cc"), _flow(NULL)
{
    _mss = Packet::data_packet_size();
    _acks_received = 0;
    _nacks_received = 0;

    _highest_sent = 0;
    _next_decision = 0;
    _flow_started = false;
    _sink = 0;
  
    _node_num = _global_node_count++;
    _nodename = "CCsrc " + to_string(_node_num);

    _cwnd = 10 * _mss;
    _ssthresh = 0xFFFFFFFFFF;
    _flightsize = 0;
    _flow._name = _nodename;
    setName(_nodename);
}

static void reset() {
    lastWindowBeforeLoss = 0.0;
    tcpFriendlyWindow = 0.0;
    ackCount = 0;
    lastCongestionOrigin = 0.0;
    epochStartTime = 0;
    minRoundTripTime = 0;
    cubicTimeFactor = 0.0;
}

/* Porneste transmisia fluxului de octeti */
void CCSrc::startflow(){
    cout << "Start flow " << _flow._name << " at " << timeAsSec(eventlist().now()) << "s" << endl;

    reset();

    _flow_started = true;
    _highest_sent = 0;
    _packets_sent = 0;

    while (_flightsize + _mss < _cwnd)
        send_packet();
}

/* Initializeaza conexiunea la host-ul sink */
void CCSrc::connect(Route* routeout, Route* routeback, CCSink& sink, simtime_picosec starttime) {
    assert(routeout);
    _route = routeout;
    
    _sink = &sink;
    _flow._name = _name;
    _sink->connect(*this, routeback);

    eventlist().sourceIsPending(*this,starttime);
}

static int cubic_update(CCSrc& self, simtime_picosec timestamp) {
    ackCount++;

    double t = (timestamp - epochStartTime) / 1e9;

    if (epochStartTime == 0) {  // daca este prima executie sau timpul de start al epocii a fost resetat
        epochStartTime = timestamp;  // setam startul epocii la timestamp-ul actual
        lastCongestionOrigin = self._cwnd;  // setam originea congestiei la valoarea curenta a ferestrei de congestie
    }

    // calculam valoarea k folosind formula cubica
    double k = pow((lastWindowBeforeLoss - self._cwnd) / CUBIC_COEFFICIENT, 1.0/3.0);

    // calculam tinta pentru fereastra de congestie folosind formula cubica
    double target_cwnd = CUBIC_COEFFICIENT * pow(t - k, 3) + lastCongestionOrigin;

    if (target_cwnd > self._cwnd) {
        // daca tinta pentru fereastra de congestie este mai mare decat fereastra curenta
        double windowIncrease = target_cwnd - self._cwnd;  // calculam cresterea necesara a ferestrei
        // limitam cresterea ferestrei la un maxim specificat
        windowAdjustmentDelta = min(windowIncrease, self._mss * (0.4 + 0.4 * (t / 20)));
        self._cwnd += windowAdjustmentDelta;  // aplicam cresterea la fereastra de congestie
    } else {
        // daca tinta pentru fereastra de congestie este mai mica sau egala cu fereastra curenta
        windowAdjustmentDelta = self._mss * 0.1;  // aplicam o crestere minima la fereastra de congestie
        self._cwnd += windowAdjustmentDelta;  // aplicam cresterea la fereastra de congestie
    }

    return windowAdjustmentDelta;
}


/* Variabilele cu care vom lucra:
    _nacks_received
    _flightsize -> numarul de bytes aflati in zbor
    _mss -> maximum segment size
    _next_decision 
    _highest_sent
    _cwnd
    _ssthresh
    
    CCAck._ts -> timestamp ACK
    eventlist.now -> timpul actual
    eventlist.now - CCAck._tx -> latency
    
    ack.ackno();
    
    > Puteti include orice alte variabile in restul codului in functie de nevoie.
*/
/* TODO: In mare parte aici vom avea implementarea algoritmului si in functie de nevoie in celelalte functii */


//Aceasta functie este apelata atunci cand dimensiunea cozii a fost depasita iar packetul cu numarul de secventa ackno a fost aruncat.
void CCSrc::processNack(const CCNack& nack) {
    _nacks_received++;  // contorizeaza un NACK primit
    _flightsize -= _mss;  // scade dimensiunea datelor  cu marimea unui  MSS

    // verifica daca fereastra de congestie este suficient de mare pentru a fi redusa
    if (_cwnd > 2 * _mss) {
        _cwnd *= 0.9;  // reduce fereastra de congestie cu 10% pentru a raspunde la semnalul de congestie indicat de NACK
        _ssthresh = max(_cwnd, 2.0 * _mss);  // actualizeaza ssthresh
    }
}
    
/* Process an ACK.  Mostly just housekeeping*/    
void CCSrc::processAck(const CCAck& ack) {
    _acks_received++;  // inregistreaza un nou ACK primit
    _flightsize -= _mss;  // scade dimensiunea datelor  cu marimea unui  MSS

    auto ts = ack.ts();  // timestamp-ul pachetului ACK
    auto rtt = eventlist().now() - ts;  // calculeaza Round-Trip Time (RTT) pe baza timestamp-ului

    // verifica si actualizeaza RTT-ul minim daca este necesar
    if (minRoundTripTime == 0 || rtt < minRoundTripTime)
        minRoundTripTime = rtt;  // seteaza RTT-ul minim la valoarea curenta daca acesta este mai mic

    // logica pentru ajustarea ferestrei de congestie
    if (_cwnd < _ssthresh) {
        _cwnd += _mss;  // daca fereastra de congestie este sub pragul slow start, creste-o linear
    } else {
        // daca este peste pragul slow start, creste fereastra folosind update-ul cubic sau cu o unitate de MSS, oricare este mai mare
        _cwnd += max(static_cast<int>(_mss), cubic_update(*this, ts));
    }

    // implementeaza o strategie de reducere rapida daca RTT-ul actual este semnificativ mai mare decat RTT-ul minim
    if (rtt > 1.2 * minRoundTripTime) {
        _ssthresh = _cwnd * 0.85;  // reduce pragul de slow start
        _cwnd *= 0.85;  // reduce fereastra de congestie pentru a raspunde la o posibila congestie
    }
}


/* Functia de receptie, in functie de ce primeste cheama processLoss sau processACK */
void CCSrc::receivePacket(Packet& pkt) 
{
    if (!_flow_started){
        return; 
    }

    switch (pkt.type()) {
    case CCNACK: 
        processNack((const CCNack&)pkt);
        pkt.free();
        break;
    case CCACK:
        processAck((const CCAck&)pkt);
        pkt.free();
        break;
    default:

        reset();

        cout << "Got packet with type " << pkt.type() << endl;
        abort();
    }

    //now send packets!
    while (_flightsize + _mss < _cwnd)
        send_packet();
}

// Note: the data sequence number is the number of Byte1 of the packet, not the last byte.
/* Functia care se este chemata pentru transmisia unui pachet */
void CCSrc::send_packet() {
    CCPacket* p = NULL;

    assert(_flow_started);

    p = CCPacket::newpkt(*_route,_flow, _highest_sent+1, _mss, eventlist().now());
    
    _highest_sent += _mss;
    _packets_sent++;

    _flightsize += _mss;

    //cout << "Sent " << _highest_sent+1 << " Flow Size: " << _flow_size << " Flow " << _name << " time " << timeAsUs(eventlist().now()) << endl;
    p->sendOn();
}

void CCSrc::doNextEvent() {
    if (!_flow_started){
      startflow();
      return;
    }
}

////////////////////////////////////////////////////////////////
//  CC SINK Aici este codul ce ruleaza pe receptor, in mare nu o sa aducem multe modificari
////////////////////////////////////////////////////////////////

/* Only use this constructor when there is only one for to this receiver */
CCSink::CCSink()
    : Logged("CCSINK"), _total_received(0) 
{
    _src = 0;
    
    _nodename = "CCsink";
    _total_received = 0;
}

/* Connect a src to this sink. */ 
void CCSink::connect(CCSrc& src, Route* route)
{
    _src = &src;
    _route = route;
    setName(_src->_nodename);
}


// Receive a packet.
// seqno is the first byte of the new packet.
void CCSink::receivePacket(Packet& pkt) {
    switch (pkt.type()) {
    case CC:
        break;
    default:
        abort();
    }

    CCPacket *p = (CCPacket*)(&pkt);
    CCPacket::seq_t seqno = p->seqno();

    simtime_picosec ts = p->ts();
    //bool last_packet = ((CCPacket*)&pkt)->last_packet();

    if (pkt.header_only()){
        send_nack(ts,seqno);      
    
        p->free();

        //cout << "Wrong seqno received at CC SINK " << seqno << " expecting " << _cumulative_ack << endl;
        return;
    }

    int size = p->size()-ACKSIZE; 
    _total_received += Packet::data_packet_size();;

    bool ecn = (bool)(pkt.flags() & ECN_CE);

    send_ack(ts,seqno,ecn);
    // have we seen everything yet?
    pkt.free();
}

void CCSink::send_ack(simtime_picosec ts,CCPacket::seq_t ackno,bool ecn) {
    CCAck *ack = 0;
    ack = CCAck::newpkt(_src->_flow, *_route, ackno,ts,ecn);
    ack->sendOn();
}

void CCSink::send_nack(simtime_picosec ts, CCPacket::seq_t ackno) {
    CCNack *nack = NULL;
    nack = CCNack::newpkt(_src->_flow, *_route, ackno,ts);
    nack->sendOn();
}
