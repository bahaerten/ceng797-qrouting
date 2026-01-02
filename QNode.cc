#include <omnetpp.h>
#include <unordered_map>
#include <unordered_set>
#include <set>
#include <algorithm>
#include <limits>
#include <memory>

#include "QRouting_m.h"
#include "inet/linklayer/acking/AckingMacHeader_m.h"
#include "inet/common/packet/Packet.h"
#include "inet/common/Simsignals.h"
#include "inet/linklayer/common/MacAddressTag_m.h"
#include "inet/linklayer/common/MacAddress.h"
#include "inet/common/ProtocolTag_m.h"
#include "inet/common/Protocol.h"

using namespace omnetpp;
using namespace std;
using namespace inet;

class QNode : public cSimpleModule, public cListener
{
  private:
    double alpha{};
    double epsilon{};
    simtime_t initialQ{};
    simtime_t neighborTimeout{};
    int myId{-1};

    double dropPenalty = 2.0;

    unordered_map<int, unordered_map<int, double>> Q;
    unordered_set<int> neighbors;
    unordered_map<int, simtime_t> lastHeard;
    unordered_map<int, MacAddress> idToMac;

    // Duplicate Filter
    std::set<unsigned long> seenPackets;

    // Signals
    simsignal_t sigDelivered{};
    simsignal_t sigDataSent{};
    simsignal_t sigDataReceived{};

    cMessage *ageTimer = nullptr;
    cMessage *helloTimer = nullptr;

  protected:
    virtual void initialize() override
    {
        alpha = par("alpha");
        epsilon = par("epsilon");
        initialQ = par("initialQ");
        neighborTimeout = par("neighborTimeout");

        myId = getParentModule()->getIndex(); // Assuming node ID corresponds to module index

        sigDelivered = registerSignal("deliveredE2EDelay"); // Signal registrations
        sigDataSent = registerSignal("dataSent");
        sigDataReceived = registerSignal("dataDelivered");

        ageTimer = new cMessage("age");
        scheduleAt(simTime() + 1, ageTimer); // Start aging timer

        helloTimer = new cMessage("hello");
        scheduleAt(simTime() + uniform(0, 0.5), helloTimer);

        cModule *wlan = getParentModule()->getSubmodule("wlan"); // Access WLAN module
        if (wlan) {
            wlan->subscribe(packetDroppedSignal, this); // Listen for dropped packets
        }
    }

    void attachMacRequest(Packet *pkt, MacAddress destMac) // MAC address and ID attachment parts
    {
        auto req = pkt->addTag<MacAddressReq>();
        req->setDestAddress(destMac);
        req->setSrcAddress(MacAddress::BROADCAST_ADDRESS);
    }

    void attachProtocol(Packet *pkt)
    {
        if (pkt->hasTag<PacketProtocolTag>()) {
            pkt->removeTag<PacketProtocolTag>();
        }
        auto tag = pkt->addTag<PacketProtocolTag>();
        tag->setProtocol(&Protocol::ipv4);
    }

    void learnMac(Packet *pkt, int senderId)
    {
        auto ind = pkt->getTag<MacAddressInd>();
        if (ind) {
            idToMac[senderId] = ind->getSrcAddress();
        }
    }

    virtual void handleMessage(cMessage *msg) override // Main message handler
    {
        // 1. Neighbor Maintenance
        if (msg == ageTimer) {
            for (auto it = neighbors.begin(); it != neighbors.end(); ) {
                if (simTime() - lastHeard[*it] > neighborTimeout) {
                    idToMac.erase(*it);
                    it = neighbors.erase(it);
                } else {
                    ++it;
                }
            }
            scheduleAt(simTime() + 1, ageTimer);
            return;
        }

        // 2. Hello Broadcast
        if (msg == helloTimer) {
            Packet *pkt = new Packet("Hello");
            auto hello = makeShared<QrHello>();
            hello->setSenderId(myId);
            hello->setChunkLength(B(32));
            pkt->insertAtBack(hello);

            attachMacRequest(pkt, MacAddress::BROADCAST_ADDRESS);
            attachProtocol(pkt);
            send(pkt, "netOut");

            scheduleAt(simTime() + 1.0, helloTimer);
            return;
        }

        // 3. App Traffic
        if (msg->arrivedOn("appIn")) { // Handle outgoing application packets
            Packet *pkt = check_and_cast<Packet *>(msg);

            if (pkt->peekAtFront<QrData>()) {
                auto data = pkt->removeAtFront<QrData>();

                data->setSrc(myId);
                data->setSenderId(myId);
                data->setHopCount(0);
                data->setTxStartTime(simTime());

                pkt->insertAtFront(data);
                pkt->setName("Data");

                emit(sigDataSent, 1); // Count creation for PDR

                forwardData(pkt);
            } else {
                delete pkt;
            }
            return;
        }

        // 4. Network Traffic
        if (msg->arrivedOn("netIn")) { // Handle incoming network packets
            Packet *pkt = check_and_cast<Packet *>(msg);
            const auto& header = pkt->peekAtFront<QrHeader>();

            if (!header) { delete pkt; return; }

            if (dynamic_pointer_cast<const QrData>(header)) {
                handleData(pkt);
            }
            else if (dynamic_pointer_cast<const QrFeedback>(header)) {
                handleFeedback(pkt);
            }
            else if (dynamic_pointer_cast<const QrHello>(header)) {
                handleHello(pkt);
            }
            else {
                delete pkt;
            }
            return;
        }
        delete msg;
    }

    void handleHello(Packet *pkt) // Handle incoming Hello packets
    {
        auto hello = pkt->peekAtFront<QrHello>();
        int sender = hello->getSenderId();

        neighbors.insert(sender);
        lastHeard[sender] = simTime();
        learnMac(pkt, sender);

        delete pkt;
    }

    void handleData(Packet *pkt) // Handle incoming Data packets
    {
        auto data = pkt->removeAtFront<QrData>();

        int dest = data->getDest();
        int sender = data->getSenderId();
        int nextHop = data->getNextHopId();

        if (nextHop != myId && dest != myId) {
            pkt->insertAtFront(data);
            delete pkt;
            return;
        }

        neighbors.insert(sender);
        lastHeard[sender] = simTime();

        pkt->insertAtFront(data);
        learnMac(pkt, sender);
        data = pkt->removeAtFront<QrData>();

        simtime_t delay = simTime() - data->getTxStartTime(); // Calculate E2E delay
        double minQ = (myId == dest) ? 0.0 : initialQ.dbl(); // Default minQ

        if (myId != dest && !Q[dest].empty()) { // Update minQ for feedback
            minQ = numeric_limits<double>::max();
            for (auto const& [nid, q] : Q[dest]) minQ = min(minQ, q); // Find minimum Q-value
        }

        sendFeedback(sender, dest, delay, minQ);

        // === PDR & DUPLICATE CHECK ===
        if (dest == myId) {
            // Check if we have already counted this specific packet ID
            if (seenPackets.find(data->getSeqNo()) != seenPackets.end()) {
                // Duplicate packet (MAC retransmission)! Ignore it.
                delete pkt;
                return;
            }

            // Mark as seen
            seenPackets.insert(data->getSeqNo());

            // Count it
            emit(sigDelivered, delay.dbl());
            emit(sigDataReceived, 1);

            delete pkt;
        } else {
            data->setSenderId(myId);
            pkt->insertAtFront(data);
            forwardData(pkt);
        }
    }

    void sendFeedback(int targetId, int destId, simtime_t delay, double minQ)
    {
        if (idToMac.find(targetId) == idToMac.end()) return;

        Packet *fbPkt = new Packet("Feedback"); // Create feedback packet
        auto fb = makeShared<QrFeedback>();
        fb->setForDest(destId);
        fb->setToNodeId(targetId);
        fb->setSenderId(myId);
        fb->setMeasuredLink(delay);
        fb->setMinQatNext(minQ);
        fb->setChunkLength(B(32));

        fbPkt->insertAtBack(fb);
        attachMacRequest(fbPkt, idToMac[targetId]);
        attachProtocol(fbPkt);
        send(fbPkt, "netOut");
    }

    void handleFeedback(Packet *pkt)
    {
        auto fb = pkt->peekAtFront<QrFeedback>();

        if (fb->getToNodeId() == myId) {
            int sender = fb->getSenderId();
            neighbors.insert(sender);
            lastHeard[sender] = simTime();
            learnMac(pkt, sender);

            auto &qmap = Q[fb->getForDest()]; // Q-value update
            double oldVal = qmap.count(sender) ? qmap[sender] : initialQ.dbl();
            double reward = fb->getMeasuredLink().dbl() + fb->getMinQatNext(); // Reward calculation

            qmap[sender] = (1.0 - alpha) * oldVal + alpha * reward; // Q-value update
        }
        delete pkt;
    }

    void forwardData(Packet *pkt)
    {
        const auto& dataConst = pkt->peekAtFront<QrData>();
        int dest = dataConst->getDest();

        int nextHop = chooseNextHop(dest);

        if (nextHop < 0 || idToMac.find(nextHop) == idToMac.end()) {
            delete pkt;
            return;
        }

        auto data = pkt->removeAtFront<QrData>();
        data->setNextHopId(nextHop);
        data->setHopCount(data->getHopCount() + 1);
        pkt->insertAtFront(data);

        attachMacRequest(pkt, idToMac[nextHop]);
        attachProtocol(pkt);

        send(pkt, "netOut");
    }

    int chooseNextHop(int dest)
    {
        if (neighbors.empty()) return -1;

        if (uniform(0, 1) < epsilon) {
            auto it = neighbors.begin(); // Random neighbor selection for exploration
            std::advance(it, intuniform(0, neighbors.size() - 1));
            return *it;
        }

        int best = -1;
        double bestVal = numeric_limits<double>::max(); // Greedy selection based on Q-values
        auto &qmap = Q[dest];

        for (int n : neighbors) {
            double v = qmap.count(n) ? qmap[n] : initialQ.dbl();
            if (v < bestVal) {
                bestVal = v;
                best = n;
            }
        }
        return best;
    }

    virtual void receiveSignal(cComponent *source, simsignal_t signalID, cObject *obj, cObject *details) override // Handle dropped packets
    {
        if (signalID == packetDroppedSignal) {
             Packet *originalPkt = dynamic_cast<Packet*>(obj);
             if (!originalPkt) return;

             Packet *pkt = originalPkt->dup();

             if (pkt->peekAtFront<inet::AckingMacHeader>()) {
                 pkt->removeAtFront<inet::AckingMacHeader>();
             }

             const auto& header = pkt->peekAtFront<QrHeader>();

             if (header && dynamic_pointer_cast<const QrData>(header)) {
                 const auto& data = pkt->peekAtFront<QrData>();

                 int failed = data->getNextHopId();
                 int dest = data->getDest();

                 auto &qmap = Q[dest];
                 double old = qmap.count(failed) ? qmap[failed] : initialQ.dbl();
                 qmap[failed] = (1.0 - alpha) * old + alpha * dropPenalty;
             }
             delete pkt;
        }
    }

    virtual ~QNode() {
        cancelAndDelete(ageTimer);
        cancelAndDelete(helloTimer);
    }
};

Define_Module(QNode);
