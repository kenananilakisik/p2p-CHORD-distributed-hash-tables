all:
	gcc -Wall -o dht_peer dht_peer.c -lssl -lcrypto -lpthread
	gcc -Wall -o dht_client dht_client.c -lssl -lcrypto -lpthread
clean:
	rm dht_client
	rm dht_peer
	rm -rf *Objects
