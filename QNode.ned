package omnetrlmodels.networklayer.qrouting;

simple QNode
{
    parameters:
        //here lies reinforcement learning parameters
        double alpha = default(0.2); //learning rate alpha
        double epsilon = default(0.05); //exploration rate
        double initialQ @unit(s) = default(0.05s); //initialQ for first explored neighbors
        double neighborTimeout @unit(s) = default(5s); //periodic neighbor check and deletion

        // Signals for PDR and end-to-end delay
        @signal[deliveredE2EDelay](type=double);
        @signal[dataSent](type=long);
        @signal[dataDelivered](type=long);

        // === Statistics ===
        //End-to-end delay is recorded as vectors with time
        @statistic[deliveredE2EDelay](
            title="End-to-End Delay";
            source=deliveredE2EDelay;
            record=vector,mean;
            interpolationmode=none;
            unit=s
        );

        //PDR signals are kept as 2 counts one for dataSent and one for dataDelivered
        @statistic[dataSent](
            title="Packets Sent";
            source=dataSent;
            record=count
        );


        @statistic[dataDelivered](
            title="Packets Delivered";
            source=dataDelivered;
            record=count
        );

        @display("i=block/router");

    gates:
        input  appIn;
        output appOut;

        input  netIn;
        output netOut;
}
