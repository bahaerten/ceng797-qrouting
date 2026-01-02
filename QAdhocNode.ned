package omnetrlmodels.networklayer.qrouting;

import inet.common.packet.recorder.PcapRecorder;
import inet.linklayer.contract.IWirelessInterface;
import inet.mobility.contract.IMobility;
import inet.networklayer.common.InterfaceTable;

module QAdhocNode
{
    parameters:
        @networkNode;
        @display("i=device/pocketpc;bgb=312,352");

    gates:
        input radioIn @directIn;

    submodules:
        interfaceTable: InterfaceTable {
            @display("p=62,160");
        }

        mobility: <default("RandomWaypointMobility")> like IMobility {
            @display("p=62,56");
        }
        //custom traffic generator
        app: QTrafficApp {
            @display("p=153,56");
        }
        //core of rl logic and routing decision
        qnode: QNode {
            @display("p=153,160");
        }
        //for getting the ACK and DROP signals from MAC layer
        wlan: <default("AckingWirelessInterface")> like IWirelessInterface {
            @display("p=153,272");
        }

    connections allowunconnected:
        app.out --> qnode.appIn;
        qnode.appOut --> app.in;

        qnode.netOut --> wlan.upperLayerIn;
        wlan.upperLayerOut --> qnode.netIn;

        // CRITICAL FIX: Connect radio gate
        wlan.radioIn <-- radioIn;
}
