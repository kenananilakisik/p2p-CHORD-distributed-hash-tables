#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <sys/time.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <arpa/nameser.h>
#include <errno.h>
#include <netinet/in.h>
#include <netdb.h> 
#include <sys/socket.h>
#include <openssl/sha.h>
#include <time.h>
#include <openssl/bn.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>
#include <dirent.h>
#include <pthread.h>

#define JOIN 1
#define LOCATION_FOUND 2
#define FORWARD 3
#define STAT 4
#define PING 5
#define PING_RESPONSE 6
#define STORE 7
#define RECURSIVE 8
#define OBJLOCATION 9
#define UPLOAD 10
#define RETREIVE 11
#define BUFFERSIZE 1024
#define STORAGE_SUCCESS 12
#define DOWNLOAD_REQUEST 13
#define NO_SUCH_FILE 14
#define DOWNLOAD 15

// Type Definitions
typedef struct node{
  struct sockaddr_in node_addr;
  unsigned char node_id[SHA_DIGEST_LENGTH];
  int alive;
}Node;

typedef struct packet{
   int type;
   Node node;
   Node next;
   unsigned char obj_id[SHA_DIGEST_LENGTH];
   struct sockaddr_in client_addr;
   long filelen;
   char data[1024];
}Packet;


// Function Prototypes
Node build_node(struct sockaddr_in node_addr);
struct sockaddr_in build_addr(char *hostname, int port);
int create_socket();
void parse_args(int argc, char **argv);
void error(char * msg);
int ready_socket(int sock);
void join();
void print_hash(unsigned char hash[20]);
void location_found(Packet receive);
void join_request(Packet receive);
void status(Packet receive);
void print_location();
void *connection_handler(void *);
void *listen_handler(void *);
void send_packet(Packet p, struct sockaddr_in s);
void send2root(Packet receive, Node n);
void send2joining(Packet receive, Node n);
void send2next(Packet receive);
void who2send(Packet receive, Node n);
void ping(Packet receive);
void ping_response(Packet receive);
void create_folder();
void storage_location(Packet receive);
void recursive(Packet receive);
void send_location(Packet receive, struct sockaddr_in s);
void upload_file(Packet receive);
void load(Packet receive);

// Global Variables
int mode = 0;
struct sockaddr_in self_addr;
struct sockaddr_in root_addr;
Node self, next, nextnext;
int client_sock = 0;
char dir_path[1024];
int count = 0;
int sock=-1;

void my_signal_handler(int signal_number)
{
  // fd is the file descriptor you used with your bind() call
  close(sock);
  exit(1); // or any other exit value...
}
int main(int argc, char ** argv) {
  signal(SIGINT, my_signal_handler);
  self.alive = 1;
  next.alive = -1;
  

  // Create folder to store objects for this peer
  create_folder(argv);

  // Parse args and build addresses
  parse_args(argc,argv);

  // Build Node self next and prev
  self = build_node(self_addr);

  // If this is a peer notify root
  if (mode == 0)
    join();
  
  sock = create_socket();
  sock = ready_socket(sock);
  pthread_t listen_thread;

  // A thread for server
  if( pthread_create( &listen_thread , NULL ,  listen_handler , (void*) &sock) < 0)
      error("could not create thread");

  // Sends ping to next every 2 sec as longs as this node has a next
  while(1){
    if(next.alive == 1){
      Packet ping;
      ping.type = PING;
      ping.node = self;
      ping.next = next;
      send_packet(ping, next.node_addr);
      sleep(2);
    }
  }
  close(sock);
  return 0;
}

// Server thread
void *listen_handler(void *sock){
  int sockd = *(int*)sock;
  int length;
  struct sockaddr_in sender_addr;
  length = sizeof(sender_addr);
  int new_socket=0;
  pthread_t accept_thread;

  // Create a thread for each connection
  for(;;) {
    if ((new_socket=accept(sockd, (struct sockaddr *) &sender_addr, (socklen_t *) &length)) ==-1)
      error("Could not accept connection");
    
    if( pthread_create( &accept_thread , NULL ,  connection_handler , (void*) &new_socket) < 0)
      error("could not create thread");   
  }
}

// Incomming Connection thread
void *connection_handler(void *socket_desc)
{
  Packet receive;
  int new_socket = *(int*)socket_desc;
  read(new_socket,&receive,sizeof(receive));
  char a1[INET_ADDRSTRLEN];
  inet_ntop(AF_INET, &(receive.node.node_addr.sin_addr), a1, INET_ADDRSTRLEN);
  //printf("\n RECEIVED FROM: %s\n", a1);

  switch(receive.type){
    case 1: //JOIN
      join_request(receive);
      break;
    case 2: //LOCATION_FOUND
      location_found(receive);
      break;
    case 3: //FORWARD
      join_request(receive);
      break;
    case 4: //STAT
      status(receive);
      break;
    case 5: //PING
      ping(receive);
      break;
    case 6: //PING_RESPONSE
      ping_response(receive);
      break;
    case 7: //STORE
      client_sock = new_socket;
      storage_location(receive);
      break;
    case 8: //RECURSIVE
      storage_location(receive);
      break;
    case 9: //OBJLOCATION
      send_location(receive, receive.node.node_addr);
      break;
    case 10: //UPLOAD
      client_sock = new_socket;
      upload_file(receive);
      break;
    case 11: //RETREIVE
      client_sock = new_socket;
      storage_location(receive);
      break;
    case 13: //DOWNLOAD_REQUEST
      client_sock = new_socket;
      load(receive);
      break;
  }
  //close(new_socket);
  return 0;
}

// Load file into packet and send to client
void load(Packet receive){
  DIR *dir;
  struct dirent *ent;
  dir = opendir(dir_path);
  char buf[SHA_DIGEST_LENGTH*2];
  int i;
  long filelen;
  FILE *fileptr;
  char *file_buffer;

  for (i=0; i < SHA_DIGEST_LENGTH; i++) {
        sprintf((char*)&(buf[i*2]), "%02x", receive.obj_id[i]);
  }
  size_t len = strlen(buf);
  while((ent = readdir(dir)) != NULL){
    if(memcmp(ent->d_name,buf,len) == 0){
      Packet download;
      download.type = DOWNLOAD;
      char file_path[1024];
      sprintf(file_path,"%s/%s",dir_path,buf);

      if( (fileptr = fopen(file_path, "rb")) == NULL){
        puts("cant open the file");
      }
      fseek(fileptr, 0, SEEK_END);
      filelen = ftell(fileptr);
      rewind(fileptr);
      file_buffer = (char *)malloc((filelen+1));
      fread(file_buffer, filelen, 1, fileptr);
      fclose(fileptr);
      download.filelen = filelen;
      memcpy(download.data,file_buffer,strlen(file_buffer));
      write(client_sock, &download, sizeof(download));
      break;
    }
  }
  Packet fail;
  fail.type = NO_SUCH_FILE;
  write(client_sock, &fail, sizeof(fail));
}

// Write the file client send to disc
void upload_file(Packet receive){
  char buf[SHA_DIGEST_LENGTH*2];
  int i;
  FILE *fp;
  for (i=0; i < SHA_DIGEST_LENGTH; i++) {
        sprintf((char*)&(buf[i*2]), "%02x", receive.obj_id[i]);
  }
  char file_path[1024];
  sprintf(file_path,"%s/%s",dir_path,buf);
  fp = fopen(file_path, "wb");
  fwrite(receive.data, 1, receive.filelen, fp);
  rewind(fp);
  Packet success;
  success.type = STORAGE_SUCCESS;
  if( (write(client_sock, &success, sizeof(success))) < 0){
      printf("error writing %s", strerror(errno));
    }
}

// Function which finds a locaion for the incoming object
void storage_location(Packet receive){
  BIGNUM *objid = BN_bin2bn(receive.obj_id, sizeof(receive.obj_id), NULL);
  BIGNUM *selfid = BN_bin2bn(self.node_id, sizeof(self.node_id), NULL);
  int obj2self = BN_cmp(objid, selfid);
  if(next.alive == 1){
    BIGNUM *nextobjid = BN_bin2bn(next.node_id, sizeof(next.node_id), NULL);
    int obj2next = BN_cmp(objid, nextobjid);
    int self2next = BN_cmp(selfid, nextobjid);
    if(obj2self == 1){ //if object > this node
      if(obj2next == 1){ //if object > successor
        //puts("sending it to my next1");
        recursive(receive);
      }
      if(obj2next == -1){ //if object < successor
        //puts("Should send to my next");
        send_location(receive, next.node_addr);
        
      }
      if(self2next == 1){ // if this node > successor
        //puts("Should send to me1");
        send_location(receive, self.node_addr);
      }
    }
    else if(obj2self == 0){ // if object == this node
        //puts("Should send to me2");
        send_location(receive, self.node_addr);
    }
    else{ // if object < this node
      if(obj2next == -1){// if object < successor
        if(self2next == 1){// if this node > successor
          //puts("Should send to me3");
          send_location(receive, self.node_addr);
        }
        else{
          //puts("sending it to my next2");
          recursive(receive);
        }
      }
      else{
        //puts("sending it to my next3");
        recursive(receive);
      }
    } 
  }
  else{
    //puts("Should send to me");
    send_location(receive, self.node_addr);
  }
}

// A generic function used when a location is found for an object
void send_location(Packet receive, struct sockaddr_in s){
  Packet obj_location;
  obj_location.type = OBJLOCATION;
  memcpy(obj_location.obj_id,&receive.obj_id, sizeof(receive.obj_id));
  obj_location.client_addr = receive.client_addr;
  obj_location.node.node_addr = s;
  if(mode == 1){
    if( (write(client_sock, &obj_location, sizeof(obj_location))) < 0){
      printf("error writing %s", strerror(errno));
    }
  }
  else{
    send_packet(obj_location, root_addr);
  }
  
}
// Forwards packet to successor
void recursive(Packet receive){
  Packet recursive;
  recursive.type = RECURSIVE;
  memcpy(recursive.obj_id,&receive.obj_id, sizeof(receive.obj_id));
  recursive.client_addr = receive.client_addr;
  send_packet(recursive, next.node_addr);
}

// Create a folder with the name of the host
void create_folder(char ** argv){
  char hostname[1024];
  hostname[1023] = '\0';
  gethostname(hostname, 1023);

  char cwd[1024];
  getcwd(cwd, sizeof(cwd));
  sprintf(dir_path,"%s/%sObjects", cwd,hostname);
  //printf("%s",path);
  mkdir(dir_path, 0700);
}

// If receive a ping send this nodes successor to sender
void ping(Packet receive){
  char addr_buffer[INET_ADDRSTRLEN];
  char next_buffer[INET_ADDRSTRLEN];
  inet_ntop(AF_INET, &(receive.node.node_addr.sin_addr), addr_buffer, INET_ADDRSTRLEN);
  inet_ntop(AF_INET, &(next.node_addr.sin_addr), next_buffer, INET_ADDRSTRLEN);
  Packet response;
  response.type = PING_RESPONSE;
  response.node = self;
  response.next = next;
  send_packet(response, receive.node.node_addr);
}

// update next according to ping
void ping_response(Packet receive){
  nextnext = receive.next;
}

// update status
void status(Packet receive){
  next = receive.next;
  print_location();
}

// When a location is found for a node send its location to itself
void location_found(Packet receive){
  Packet status;
  status.type = STAT;
  status.next = receive.next;
  status.node = receive.node;
  send_packet(status, status.node.node_addr);
}

// Sends the given packet to root
void send2root(Packet receive, Node n){
  Packet found;
  found.type = LOCATION_FOUND;
  found.node = receive.node;
  found.next = n;
  found.next.alive = 1;
  send_packet(found, root_addr);
}

// Send the given packet to the joining node
void send2joining(Packet receive, Node n){
  Packet stat;
  stat.type = STAT;
  stat.node = receive.node;
  stat.next = n;
  stat.next.alive = 1;
  send_packet(stat, stat.node.node_addr);
}

// send this packet to next
void send2next(Packet receive){
  Packet forward;
  forward.type = FORWARD;
  forward.node = receive.node;
  send_packet(forward, next.node_addr);
}

// Decide whether to this packet to root or a node
void who2send(Packet receive, Node n){
  if(mode == 0)
    send2root(receive, n);
  else
    send2joining(receive, n);
}

// Function which deals with join requests coming from peers
void join_request(Packet receive){
  char addr_buffer[INET_ADDRSTRLEN];
  inet_ntop(AF_INET, &(receive.node.node_addr.sin_addr), addr_buffer, INET_ADDRSTRLEN);

  BIGNUM *receive_id = BN_bin2bn(receive.node.node_id, sizeof(receive.node.node_id), NULL);
  BIGNUM *self_id = BN_bin2bn(self.node_id, sizeof(self.node_id), NULL);

  int receive2self = BN_cmp(receive_id, self_id);

  // If no successor simply add node to hhe ring as successor
  if(next.alive == -1 || next.node_addr.sin_addr.s_addr == self.node_addr.sin_addr.s_addr){
    Packet stat;
    stat.type = STAT;
    stat.node = receive.node;
    stat.next = self;
    stat.next.alive = 1;
    send_packet(stat, stat.node.node_addr);
    next = receive.node;
    next.alive = 1;
    print_location();
  }
  else{
    BIGNUM *next_id = BN_bin2bn(next.node_id, sizeof(next.node_id), NULL);
    int receive2next = BN_cmp(receive_id, next_id);
    int self2next = BN_cmp(self_id, next_id);
    BN_free(self_id);
    BN_free(next_id);
    BN_free(receive_id);

    if(receive2self == 1){
      //printf("\njoin is bigger than me\n");
      if(receive2next == 1){
        //printf("\njoin is bigger than mynext\n");
        if(self2next == 1){
          //printf("\nI am bigger than my next\n");
          who2send(receive, next);
          next = receive.node;
          next.alive = 1;
          print_location();
        }
        else{ // forward and keep searching
          send2next(receive);
        }
      }
      else{
        //printf("\njoin is smaller than my next\n");
        who2send(receive, next);
        next = receive.node;
        next.alive = 1;
        print_location();
      }
    }
    else{
      //printf("\njoin is smaller than me\n");
      if(self2next == -1){
        send2next(receive);
      }
      else{
        who2send(receive, next);
        next = receive.node;
        next.alive = 1;
        print_location();
      }
    }
  }
}

// Sends packets to the given address
void send_packet(Packet p, struct sockaddr_in s)
{
  char a1[INET_ADDRSTRLEN];
  inet_ntop(AF_INET, &(s.sin_addr), a1, INET_ADDRSTRLEN);
  int temp_sock = 0;
  temp_sock = create_socket();
  if (connect(temp_sock, (struct sockaddr *)&s, sizeof(s)) < 0){
    printf("\n[-] IP: %s, Port: %hu not responding, removing peer from ring\n", a1, ntohs(s.sin_port));
    if(p.type == PING){
      next = nextnext;
      print_location();
    }
  }else{
    if (sendto( temp_sock, &p, sizeof(p), 0, (struct sockaddr *)&s, sizeof(s)) < 0 ) 
      error( "sendto failed" );
  }
  close(temp_sock);
}
Node build_node(struct sockaddr_in node_addr){
  Node node;
  char addr_buffer[INET_ADDRSTRLEN];
  inet_ntop(AF_INET, &(node_addr.sin_addr), addr_buffer, INET_ADDRSTRLEN);
  size_t data_length = sizeof(addr_buffer);
  unsigned char nodeID[SHA_DIGEST_LENGTH];
  SHA1((unsigned char *)addr_buffer, data_length, nodeID);
  node.node_addr = self_addr;
  memcpy(node.node_id,&nodeID, sizeof(nodeID));
  return node;
}
void parse_args(int argc, char **argv){
  int self_port = 0;
  int root_port = 0;
  int i = 0;
  char *self_hostname, *root_hostname;
  if (argc!=7 && argc!=9) {
      fprintf(stderr,"Synopsis:\n./dht_peer -p 3402 -h top.ccs.neu.edu -r 3400 -R crown.ccs.neu.edu\n./dht_peer -m 1 -p 3400 -h crown.ccs.neu.edu\n");
      exit(1);
  }
  for (i = 1; i < argc; i++)      {

      if (strcmp(argv[i], "-m") == 0)  
      {
          mode = atoi(argv[i+1]);  
      }
      else if (strcmp(argv[i], "-p") == 0)
      {
          self_port = atoi(argv[i+1]);
      }
      else if (strcmp(argv[i], "-h") == 0)
      {
          self_hostname = argv[i+1];
      }
      else if (strcmp(argv[i], "-r") == 0)
      {
          root_port = atoi(argv[i+1]);
      }
      else if (strcmp(argv[i], "-R") == 0)
      {
          root_hostname = argv[i+1];
      }
    }
    if(mode == 0)
      root_addr = build_addr(root_hostname, root_port);

    self_addr = build_addr(self_hostname, self_port);
}

struct sockaddr_in build_addr(char *hostname, int port){
   struct sockaddr_in self_addr;
   struct hostent *peer;
   peer = gethostbyname(hostname);
   memset( &self_addr, 0, sizeof(self_addr) );
   self_addr.sin_family = AF_INET;
   self_addr.sin_port = htons(port);              
   self_addr.sin_addr = *((struct in_addr *)peer->h_addr);
   return self_addr;
}

int create_socket(sock){
   if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
      error("Could not open TCP socket");
   return sock;
}

int ready_socket(sock){
   int on = 1;
   if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0)
      error("error: set socket option");
   if (bind (sock, (struct sockaddr *) &self_addr, sizeof(self_addr)) !=0)
      error("Could not bind TCP socket to address");
   if (listen(sock, 10)==-1) {
      error("Could not listen");
   }
   return sock;
}

void join(){
  printf("[-] Please re-start te peer if you don't receive your location back from the root\n");
  Packet send;
  send.type = JOIN;
  send.node = self;
  int join_sock = 0;
  join_sock = create_socket();
  if (connect(join_sock, (struct sockaddr *)&root_addr, sizeof(root_addr)) < 0) 
    error("ERROR connecting");
  if (sendto( join_sock, &send, sizeof(send), 0, (struct sockaddr *)&root_addr, sizeof(root_addr)) < 0 ) 
    perror( "sendto failed" );

  close(join_sock);
}

void error(char * msg) {  
   //close(sock);
   fprintf(stderr,"%s",msg);
   exit(1);
}

void print_hash(unsigned char hash[20]){
  int i;
  for(i=0;i<20;i++){
    printf("%02x", (int) hash[i]);
  }
}
void print_location(){
  char a1[INET_ADDRSTRLEN];
  inet_ntop(AF_INET, &(next.node_addr.sin_addr), a1, INET_ADDRSTRLEN);
  printf("\n[+] LOCATION = NEXT PEER: %s, PORT: %d\n", a1, ntohs(next.node_addr.sin_port));
}


