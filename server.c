/* 
 * server.c - Selective Repeat protocol based on UDP.
 * usage: ./server <portNumber>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <netdb.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <signal.h>



struct SendArgs {
	int fd; //socket fd
	short seq;
	int len;
	char buffer[1024];
	const struct sockaddr *dest_addr;
	socklen_t serverlen;
};

void* sendOnePacketThread(void* args);
void error(char *msg);

#define BUFFER_LENGTH 1024
#define WINDOW_LENGTH 5120
#define MAXSEQ 30720

int ACK[31];


int main(int argc, char **argv) {
	int n; 
	int sockfd; 
	int portno; 
	int clientlen; 
	char *hostaddrp; 
	struct hostent *hostp; 
	struct sockaddr_in serveraddr;
	struct sockaddr_in clientaddr; 
	char buf[BUFFER_LENGTH]; 
	char fileName[BUFFER_LENGTH];

	if (argc != 2) {
		fprintf(stderr, "Please input as: %s <portNumber>\n", argv[0]);
		exit(1);
	}

	// initialize the input parameter.
	clientlen = sizeof(clientaddr);
	portno = atoi(argv[1]);

	// try to start the socket.
	sockfd = socket(AF_INET, SOCK_DGRAM, 0);
	if (sockfd < 0) {
		error("main: Error on opening socket");
	}

	// try to start the server internet address.
	int optval = 1;
	setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (const void *)&optval , sizeof(int));
	bzero((char *) &serveraddr, sizeof(serveraddr));
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
	serveraddr.sin_port = htons((unsigned short)portno);

	// socket binding.
	if (bind(sockfd, (struct sockaddr *) &serveraddr, sizeof(serveraddr)) < 0) {
		error("main: Error on socket binding");
	}
	 
	// Start serving with multiple files.
	while (1) {

		// Init  
		int i;
		for (i = 0; i < 31; i++)
			ACK[i] = 1;
		short windowStart = 0;
		short windowEnd = windowStart + WINDOW_LENGTH;
		long dataSent = 0;
		short currSeq = 0;
		int activeThreads, err;

		pthread_t parallelThreadArgs[5];

		struct SendArgs threadInfo[5];


		// Recieve File Request
		memset(fileName, 0, BUFFER_LENGTH);
		n = recvfrom(sockfd, fileName, BUFFER_LENGTH, 0,
				(struct sockaddr *) &clientaddr, &clientlen);
		if (n < 0)
			error("ERROR in recvfrom");

		// Determine the Origin of the Request
		hostp = gethostbyaddr((const char *)&clientaddr.sin_addr.s_addr, 
					sizeof(clientaddr.sin_addr.s_addr), AF_INET);
		if (hostp == NULL)
			error("ERROR on gethostbyaddr");
		hostaddrp = inet_ntoa(clientaddr.sin_addr);
		if (hostaddrp == NULL)
			error("ERROR on inet_ntoa\n");

		// Open the Requested File and Read the Size
		if (!isalnum(fileName[strlen(fileName) - 1]))
			fileName[strlen(fileName) - 1] = '\0';
		
		fileName[n] = '\0'; // mark the end of the file name string.


		printf("fileName: %s\n", fileName);
		printf("Sending packet SYN\n");

		FILE* file = fopen(fileName, "rb");
		if (file == NULL) {
			error("Error opening file");
		}
	
		fseek(file, 0, SEEK_END);
		long fileSize = ftell(file);
		rewind(file);

		// Send the First Five Packets if Possible
		for (i = 0; i < 5; i++) {
			if (dataSent == fileSize) { 
				break; // Done sending the file
			} else {
				// Launch a thread to send the next segment
				threadInfo[i].fd = sockfd;
				threadInfo[i].len = fileSize - dataSent > 1022 ? 1022 : fileSize - dataSent;
				threadInfo[i].seq = currSeq;
				threadInfo[i].dest_addr = &clientaddr;
				threadInfo[i].serverlen = clientlen;
				memset(threadInfo[i].buffer, 0, 1024); //reset to 0
				fread(threadInfo[i].buffer, sizeof(char), threadInfo[i].len, file);			// read data from file to buff.
				ACK[threadInfo[i].seq/1024] = 0;
				err = pthread_create(&parallelThreadArgs[i], NULL, sendOnePacketThread, &threadInfo[i]);	// creat the thread.
				if (err != 0)
					error("Error creating thread\n");

				currSeq += threadInfo[i].len + 2;
				dataSent += threadInfo[i].len;
			}
		}	

		int seq = 0;
		// Send the Rest of the File While Shifting Window
		while(dataSent < fileSize) {

			memset(buf, 0, 1024);
			n = recvfrom(sockfd, buf, BUFFER_LENGTH, 0, (struct sockaddr *) &clientaddr, &clientlen);

			if (buf[strlen(buf) - 1] == '\n')
				buf[strlen(buf) - 1] = '\0';
			short ack = (((short)buf[0]) << 8) | buf[1];
			printf("Recieved ACK seq=%d\n", ack);
			int ackNum = (((short)buf[0]) << 8) | buf[1]; //redundant?
			if (ackNum < 0 || ackNum > 30720)
				error("Invalid ACK number");
			ACK[ackNum/1024] = 1; //mark ACKed.

			//once I receive the windowStart ACK that I am waiting, I update all those ACKs I received together.
			if (ackNum == windowStart) {
				// Launch the New Segment(s) and Advance the Window
				while (ACK[windowStart/1024]) {
					if (dataSent == fileSize)
						break;
					for(i = 0; i < 5; i++) {
						if (threadInfo[i].seq == windowStart) {
							if (pthread_join(parallelThreadArgs[i], NULL) != 0)
								printf("failed to join thread %d", windowStart);

							threadInfo[i].seq = windowEnd;
							threadInfo[i].len = fileSize - dataSent > 1022 ? 1022 : fileSize - dataSent;
							threadInfo[i].dest_addr = &clientaddr;
							threadInfo[i].serverlen = clientlen;
							memset(threadInfo[i].buffer, 0, 1024);
							fread(threadInfo[i].buffer, sizeof(char), threadInfo[i].len, file);
							//printf("setting array %d to 0\n", threadInfo[i].seq/1024);	
							ACK[threadInfo[i].seq/1024] = 0;
							pthread_create(&parallelThreadArgs[i], NULL, sendOnePacketThread, &threadInfo[i]);
							currSeq += threadInfo[i].len + 2;
							dataSent += threadInfo[i].len;
						} 
					}
					windowStart = (windowStart == 30720 ? 0 : windowStart + 1024);
					windowEnd = (windowEnd == 30720 ? 0 : windowEnd + 1024);
				}
			} 
		}

		// See if there are more ACKS we are waiting for
		int sum = 0;
		for (i = 0; i < 31; i++) {
			sum += ACK[i];
		}

		// Wait for the rest of the ACKS
		while (sum != 31) {

			sum = 0;
			memset(buf, 0, 1024);
			n = recvfrom(sockfd, buf, BUFFER_LENGTH, 0, (struct sockaddr *) &clientaddr, &clientlen);

			if (buf[strlen(buf) - 1] == '\n')
				buf[strlen(buf) - 1] = '\0';
			
			short ack = (((short)buf[0]) << 8) | buf[1];
			printf("Receiving packet %4d\n", ack);
			ACK[ack/1024] = 1;
			for (i = 0; i < 5; i++) {
				if (threadInfo[i].seq == ack) {
					pthread_join(parallelThreadArgs[i], NULL);
				}
			}

			for (i = 0; i < 31; i++) {
				sum += ACK[i];
			}
		}
		// All ACKs Recieved, Send FIN
		char packet[6];
		memset(packet, 0, 6);
		printf("Sending Packet FIN\nReceiving Packet ACK\nReceiving Packet FIN\nSending Packet ACK\n");
		short seqNum = currSeq;
		packet[0] = (currSeq >> 8) & 0xFF;
		packet[1] = currSeq & 0xFF;
		packet[2] = 'F';
		packet[3] = 'I';
		packet[4] = 'N';
		struct timeval timeoutStart, sent, check, result;
		int n = sendto(sockfd, packet, 5, 0, &clientaddr, clientlen);
		gettimeofday(&timeoutStart, NULL);
		gettimeofday(&sent, NULL);

		while (1) {
			gettimeofday(&check, NULL);
			timersub(&check, &sent, &result);
			if (result.tv_usec > 10) {
				sendto(sockfd, packet, 5, 0, &clientaddr, clientlen);
				gettimeofday(&sent, NULL);
			}

			timersub(&check, &timeoutStart, &result);
			if (result.tv_usec > 1000)
				break;
		}
	}
}



void* sendOnePacketThread(void* args) {
	
	char packet[1024];
	struct SendArgs inputArg = *(struct SendArgs *) args;
	struct timeval update;
	struct timeval send;

	if (inputArg.len == 0){
		printf("Error in sendPacket: No data to send.\n");
		return 0;
	}

	// write the sequence number
	packet[1] = (inputArg.seq >> 0) & 0xFF;
	packet[0] = (inputArg.seq >> 8) & 0xFF;

	// populate the data in packet.
	memcpy(packet + 2, inputArg.buffer, inputArg.len);

	// start sending data.
	if (sendto(inputArg.fd, packet, inputArg.len + 2, 0, inputArg.dest_addr, inputArg.serverlen) >= 0) {
		printf("Sending Packet %4d %d len=%d\n", inputArg.seq, WINDOW_LENGTH, inputArg.len);
		gettimeofday(&send, NULL);
	} else {
		error("Error in sendPacket: error in sendto function.\n");
		return 0;
	}

	// check the timer.
	while (1) {
		// When package is ACKed.
		if (ACK[inputArg.seq/1024] == 1) {
			return 0;
		}

		// Update the time and check how long it has passed.
		gettimeofday(&update, NULL);
		struct timeval period;
		timersub(&update, &send, &period);

		// Get microsecond (1/1000 millisecond) and check whether it is greater than 500ms.
		// If greater, then resend.
		if (period.tv_usec / 1000 > 500) {
			if (sendto(inputArg.fd, packet, inputArg.len + 2, 0, inputArg.dest_addr, inputArg.serverlen) > 0) {
				gettimeofday(&send, NULL);
				printf("Sending packet %4d %d Retransmission len=%d\n", inputArg.seq, WINDOW_LENGTH, inputArg.len);
			} else {
				error("Error sending packet.\n");				
			}
		}
	}
	
}



void error(char *msg) {
	perror(msg);
	exit(1);
}

