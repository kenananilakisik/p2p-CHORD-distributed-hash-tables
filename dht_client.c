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
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#define STORE 7
#define UPLOAD 10
#define RETREIVE 11;
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
void parse_args(int argc, char ** argv);
void print_menu();
void store();
void prep2store(char *filename, FILE *fileptr);
struct sockaddr_in build_addr(char *hostname, int port);
void menu_prompt();
void recursive();
void retreive(char *filemame);
void create_folder();
void parse_args(int argc, char ** argv);


// Global Variables
struct sockaddr_in self_addr;
struct sockaddr_in root_addr;
char dir_path[1024];

void my_signal_handler(int signal_number)
{
  // fd is the file descriptor you used with your bind() call
  //close(root_sock);
  //close(peer_sock);
  exit(1); // or any other exit value...
}
// Parse arguments, build root and self addr
void parse_args(int argc, char ** argv){
	int self_port = 0;
  	int root_port = 0;
  	char *self_hostname, *root_hostname;
  	int i;
  	if (argc!=9) {
      fprintf(stderr,"Synopsis:\n./dht_client <-p client_port> <-h client_hostname> <-r root_port> <-R root_hostname>\n");
      exit(1);
   	}
  	for (i = 1; i < argc; i++)      {

      if (strcmp(argv[i], "-p") == 0)
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
    root_addr = build_addr(root_hostname, root_port);
    self_addr = build_addr(self_hostname, self_port);
}

// MAIN
int main(int argc, char ** argv){
	signal(SIGINT, my_signal_handler);
	parse_args(argc,argv);
	create_folder();
	menu_prompt();
	return 0;
}

// Main Loop
void menu_prompt(){
	print_menu();
	printf("\nSelection: ");
	char name[1];
	while(1){
		fgets(name,100,stdin);
		name[1] = '\0';

		switch(atoi(name)){
			case 1:
				store();
				break;
			case 2:
				recursive();
				break;
			case 3:
				printf("\nUnfortunately not implemented :(\n");
				break;
			case 4:
				exit(1);
				break;
		}
	}
}

// Recursive Retreival option
void recursive(){
	char *filename;
	filename = (char *)malloc((64));
	char decision[10];
	while(1){
		printf("\nPlease provide a file name if the file is in the same directory that client is run or provide an absolute path for the file:\n\nFile: ");
		fgets(filename,64,stdin);
		//filename[64] = '\0';
		printf("\nIs this the file that you want to store: %s\nPress y or n to continue\nContinue: ", filename);
		fgets(decision,100,stdin);
		decision[1] = '\0';
		int filesize = 0;
		filesize = strlen(filename);
		filename[filesize-1] = '\0';
		
		if(strcmp(decision,"y") == 0)
			retreive(filename);
		
	}
}

// Conversation between client root and peer for file retreival
void retreive(char * filename){
	long namelen;
	namelen = strlen(filename);
	int root_sock = -1;
	int peer_sock = -1;
	unsigned char objID[SHA_DIGEST_LENGTH];
	SHA1((unsigned char *)filename, namelen, objID);
	// --------------------------------------------------- Write to Root
	Packet retreive;
	retreive.type = RETREIVE;
	retreive.client_addr = self_addr;
	memcpy(retreive.obj_id, &objID, sizeof(objID));

	//puts("This is the FILE ID: \n");
	BIGNUM *r = BN_bin2bn(objID, sizeof(objID), NULL);
    //BN_print_fp(stdout, r);
    BN_free(r);
    if ((root_sock = socket(AF_INET, SOCK_STREAM, 0)) < 0){
      printf("Could not open TCP socket");
      exit(1);
	}
	if ( (connect(root_sock, (struct sockaddr *)&root_addr, sizeof(root_addr))) < 0){
		puts("asdasdasdasd");
		printf("error connecting to server %s",strerror(errno));
		exit(1);
	}
	if( (write(root_sock, &retreive, sizeof(retreive))) < 0){
		printf("write error : %s\n", strerror(errno));
		exit(1);
	}
	// --------------------------------------------------- Read Root

    Packet receive;
    if ( (read(root_sock, &receive, sizeof(receive))) < 0){
    	printf("read failed error : %s", strerror(errno));
    	exit(1);
    }
    close(root_sock);

    // --------------------------------------------------- Send to Peer
    Packet send;
    send.type = DOWNLOAD_REQUEST;
    memcpy(send.obj_id, &objID, sizeof(objID));

    if ((peer_sock = socket(AF_INET, SOCK_STREAM, 0)) < 0){
		printf("Could not open TCP socket");
		exit(1);
	}
	if (connect(peer_sock, (struct sockaddr *)&receive.node.node_addr, sizeof(receive.node.node_addr)) < 0){
		printf("error connecting to server %s",strerror(errno));
		exit(1);
	}
	if( write(peer_sock, &send, sizeof(send)) < 0){
		printf("error writing %s",strerror(errno));
		exit(1);
	}

	// --------------------------------------------------- Receive From peer
	Packet re;
	if ( (read(peer_sock,&re,sizeof(re))) < 0){
		printf("error writing %s",strerror(errno));
		exit(1);
	}
	if(re.type == NO_SUCH_FILE){
		puts("\n[-] File cannot be found\n");
	}
	if(re.type == DOWNLOAD){
		FILE *fp = NULL;
		char file_path[1024];
		sprintf(file_path,"%s/%s",dir_path,filename);
		fp = fopen(file_path, "wb");
		fwrite(re.data, 1, re.filelen, fp);
		rewind(fp);
		puts("\n[+] Successfuly retreived\n");
	}
	close(peer_sock);
	menu_prompt();
}

// File Storage option
void store(){
	char *filename;
	filename = (char *)malloc((64));
	char decision[10];
	FILE *fileptr;
	while(1){
		printf("\nPlease provide a file name if the file is in the same directory that client is run or provide an absolute path for the file\n\nFile: ");
		fgets(filename,64,stdin);
		//filename[64] = '\0';
		printf("\nIs this the file that you want to store: %s\nPress y or n to continue\nContinue: ", filename);
		fgets(decision,100,stdin);
		decision[1] = '\0';
		int filesize = 0;
		filesize = strlen(filename);
		filename[filesize-1] = '\0';
		if ((fileptr = fopen(filename, "rb")) == NULL)
			printf("[-] No such file, please try again");
		else{
			if(strcmp(decision,"y") == 0)
				prep2store(filename, fileptr);
		}
	}
}

// Conversation between client root and peer for file storage
void prep2store(char *filename, FILE *fileptr){

	long filelen;
	long namelen;
	namelen = strlen(filename);
	char *file_buffer;
	int root_sock = -1;
	int peer_sock = -1;

	
	unsigned char objID[SHA_DIGEST_LENGTH];
	SHA1((unsigned char *)filename, namelen, objID);
	Packet store;
	store.type = STORE;
	store.client_addr = self_addr;
	memcpy(store.obj_id,&objID, sizeof(objID));
	//puts("This is the FILE ID: \n");
	BIGNUM *r = BN_bin2bn(objID, sizeof(objID), NULL);
    //BN_print_fp(stdout, r);
    BN_free(r);

    // --------------------------------------------------- Write to Root
	if ((root_sock = socket(AF_INET, SOCK_STREAM, 0)) < 0){
      printf("Could not open TCP socket");
      exit(1);
	}
	if (connect(root_sock, (struct sockaddr *)&root_addr, sizeof(root_addr)) < 0){
		puts("asdasdasdasd");
		printf("error connecting to server %s",strerror(errno));
		exit(1);
	}
	
	if( write(root_sock, &store, sizeof(store)) < 0){
		printf("write error : %s\n", strerror(errno));
		exit(1);
	}

	// --------------------------------------------------- Read from root
    Packet receive;
    if ( (read(root_sock,&receive,sizeof(receive))) < 0){
    	printf("read failed error : %s", strerror(errno));
    	exit(1);
    }
    char a1[INET_ADDRSTRLEN];
  	inet_ntop(AF_INET, &receive.node.node_addr.sin_addr, a1, INET_ADDRSTRLEN);
    close(root_sock);
    //---------------------------------------------------
    fseek(fileptr, 0, SEEK_END);
	filelen = ftell(fileptr);
	rewind(fileptr);

	file_buffer = (char *)malloc((filelen+1));
	fread(file_buffer, filelen, 1, fileptr);
	fclose(fileptr);

	Packet storage_item;
	storage_item.type = UPLOAD;
	storage_item.filelen = filelen;
	memcpy(storage_item.obj_id,&objID, sizeof(objID));
	memcpy(storage_item.data,file_buffer,strlen(file_buffer));
	// ---------------------------------------------------  Contatct peer
	if ((peer_sock = socket(AF_INET, SOCK_STREAM, 0)) < 0){
      printf("Could not open TCP socket");
      exit(1);
	}
	if (connect(peer_sock, (struct sockaddr *)&receive.node.node_addr, sizeof(receive.node.node_addr)) < 0){
		puts("asdasdasdasd");
		printf("error connecting to server %s",strerror(errno));
		exit(1);
	}
	if( write(peer_sock, &storage_item, sizeof(storage_item)) < 0){
		printf("error writing %s",strerror(errno));
		exit(1);
	}
	Packet re;
	if ( (read(peer_sock,&re,sizeof(re))) < 0){
		printf("error writing %s",strerror(errno));
		exit(1);
	}
	if(re.type == 12){
		printf("\nSTORAGE SUCCESS!\n");
	}

    close(peer_sock);
    menu_prompt();
	//exit(1);
	
}
void print_menu(){
	printf("\n1- Store object\n2- Retreive object recursively\n3- Retreive object iteratively\n4- Exit\n");
}

// Build sock address
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

// Creates a folder and name it hostname of the host
void create_folder(){
	//long pid = getpid();
	char hostname[1024];
	hostname[1023] = '\0';
	gethostname(hostname, 1023);
	char cwd[1024];
	getcwd(cwd, sizeof(cwd));
	sprintf(dir_path,"%s/%sObjects", cwd, hostname);
	//printf("%s",path);
	mkdir(dir_path, 0700);
}