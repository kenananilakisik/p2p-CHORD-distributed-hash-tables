--High Level Approach

Every peer both act as a server and a client essentially. Server has a single thread for listening so that it won't block other jobs and every connection is handled in a separate thread. All peers have structs called Nodes and they store their successors and 2-hops away in these Nodes. All traversals, storages and retrievals are done based on the values that are stored in these nodes. In order to detect leaving and crashing peers on the ring all peers send pings to their successors and learn their successors and store this value in a Node which is called next next in the design.

--Run

./dht_peer -m <mode 0 for peer 1 for root> -p <host_port> -h <host> -r <root_port> -R <root>
./dht_client <-p client_port> <-h client_hostname> <-r root_port> <-R root_hostname>


