#include <omnetpp.h>
#include "QRouting_m.h"
#include "inet/common/packet/Packet.h"

using namespace omnetpp;
using namespace inet;

class QTrafficApp : public cSimpleModule
{
  private:
    cMessage *tick = nullptr;
    simtime_t startTime{}, sendInterval{};
    int fixedDest{-1};

    // Global counter for unique Packet IDs across all nodes
    static unsigned long globalSeqNum;

  protected:
    virtual void initialize() override
    {
        startTime = par("startTime");
        sendInterval = par("sendInterval");
        fixedDest = par("dest");

        tick = new cMessage("tick");
        scheduleAt(simTime() + startTime, tick);
    }

    virtual void handleMessage(cMessage *msg) override //
    {
        if (msg == tick)
        {
            int self = getParentModule()->getIndex();
            int n = getParentModule()->getVectorSize();
            int dest = fixedDest;

            // Pick random destination
            if (dest < 0 || dest == self) {
                if (n > 1) {
                    do {
                        dest = intuniform(0, n - 1);
                    } while (dest == self);
                } else {
                    scheduleAt(simTime() + sendInterval, tick);
                    return;
                }
            }

            // 1. Create Data Chunk
            auto payload = makeShared<QrData>();
            payload->setDest(dest);
            payload->setHopCount(0);
            payload->setChunkLength(B(100));

            // Assign Unique Sequence Number
            payload->setSeqNo(globalSeqNum++);

            // 2. Wrap in Packet
            Packet *pkt = new Packet("AppTraffic");
            pkt->insertAtBack(payload);

            // 3. Send
            send(pkt, "out");

            scheduleAt(simTime() + sendInterval, tick);
        }
        else {
            delete msg;
        }
    }

    virtual ~QTrafficApp() override
    {
        cancelAndDelete(tick);
    }
};

// Initialize static member
unsigned long QTrafficApp::globalSeqNum = 0;

Define_Module(QTrafficApp);
