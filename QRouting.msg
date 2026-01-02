// omnetrlmodels/networklayer/qrouting/QRouting.msg

import inet.common.INETDefs;
import inet.common.packet.chunk.Chunk;

cplusplus {{
#include "inet/common/INETDefs.h"
#include "inet/common/packet/chunk/Chunk.h"
}}

enum QrKind
{
    DATA = 100;
    FEEDBACK = 101;
    HELLO = 102;
}

class QrHeader extends inet::FieldsChunk
{
    int src;
    int dest;
    int senderId;
    int nextHopId;
}

class QrData extends QrHeader
{
    int hopCount;
    simtime_t txStartTime;
    unsigned long seqNo;
}

class QrFeedback extends QrHeader
{
    int forDest;
    int toNodeId;
    double minQatNext;
    simtime_t measuredLink;
}

class QrHello extends QrHeader
{
}
