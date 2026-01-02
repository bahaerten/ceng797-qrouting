package omnetrlmodels.networklayer.qrouting;

simple QTrafficApp
{
    parameters:
        double startTime @unit(s) = default(0.5s);
        double sendInterval @unit(s) = default(1s);
        int dest = default(-1);

        @display("i=block/app");

    gates:
        input  in;
        output out;
}

