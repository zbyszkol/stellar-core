# Roadmap for creating a benchmark of the stellar-core

1. research current metrics subsystem used in stellar-core (medida?)
2. extend the metrics subsystem, so it is possible to measure the performance of the following parts of the stellar protocol:
** txset submit
** txset accepted by node
** nomination protocol
** txset externalizing
** txset applied
3. prepare benchmarks showing performance of mentioned parts of the protocol in the following
   context: #of accounts in ledger -> #tx/sec (how it behaves when we are changing number of nodes),
   #of nodes in network -> #tx/sec (how it changes when we are running more nodes) - chart
   displaying cumulative and detailed stats for mentioned parts
4. plot collected data in human-readable, publishable format
