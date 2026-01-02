import inet.physicallayer.wireless.unitdisk.UnitDiskRadioMedium;
import inet.networklayer.configurator.ipv4.Ipv4NetworkConfigurator;
import omnetrlmodels.networklayer.qrouting.QAdhocNode;

network QAdhocNetwork
{
    parameters:
        int numNodes = default(9);
        @display("bgb=800,600");

    submodules:

        configurator: Ipv4NetworkConfigurator {
            @display("p=50,50");
        }
        //for physical layer range is 250m
        radioMedium: UnitDiskRadioMedium {
            @display("p=50,100");
        }
        //9 custom QAdhocNode
        node[numNodes]: QAdhocNode {
            @display("p=250,200");
        }
}
