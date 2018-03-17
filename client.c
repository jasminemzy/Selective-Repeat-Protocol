/*
 A simple client in the internet domain using UDP
 Usage: ./client hostname port filename
 */
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>      // define structures like hostent
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>

int print_message (int, short, char*);


void error(char *msg)
{
    perror(msg);
    exit(0);
}

int main(int argc, char *argv[])
{
    int sockfd;  // socket descriptor
    int portno, n;
    struct sockaddr_in serv_addr;
    socklen_t servlen;
    struct hostent *server;  // contains tons of information, including the server's IP address
    //char *hostname;
    char *filename;
    char filename_buffer[1025];
    FILE *datafile;
    char buffer[1025];
    int flag = 0;
    int result;
    char data[31][1023];
    int tracker[31];
    int seq_min = 0;
    int seq_max = 5;
    int shift;
   

    if (argc < 4) {
       fprintf(stderr,"usage %s hostname port filename\n", argv[0]);
       exit(0);
    }

    portno = atoi(argv[2]);
    filename = argv[3];

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);  // create a new socket
    if (sockfd < 0)
        error("ERROR opening socket");

    server = gethostbyname(argv[1]);  // takes a string like "www.yahoo.com", and returns a struct hostent which contains information, as IP address, address type, the length of the addresses...
    if (server == NULL) {
        fprintf(stderr,"ERROR, no such host\n");
        exit(0);
    }

    memset((char *) &serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET; //initialize server's address
    bcopy((char *)server->h_addr, (char *)&serv_addr.sin_addr.s_addr, server->h_length);
    serv_addr.sin_port = htons(portno);

    //if (connect(sockfd,(struct sockaddr *)&serv_addr,sizeof(serv_addr)) < 0) //establish a connection to the server
    //    error("ERROR connecting");

    strncpy(filename_buffer, filename, strlen(filename));
    filename_buffer[strlen(filename)] = '\0';
    servlen = sizeof(struct sockaddr_in);

    //send request filename to server
    n = sendto(sockfd, &filename_buffer, strlen(filename_buffer), 0, (struct sockaddr*) &serv_addr, servlen);
    if(n < 0){
        error("ERROR sending requested file to server\n");
    }

    //open a file to store data received from server
    // datafile = fopen("result.txt", "wb");
    result = open("received.data", O_TRUNC | O_CREAT | O_RDWR);


    //initialize a tracker to track whether packet after the current window is waiting to be write to file
    int index;
    index = 0;
    while(index < 30){
        tracker[index] = 0;
        index++;
    }

    //wait for server to send packets
    while(1){
        memset(buffer, 0, 1024);
        int receive = recvfrom(sockfd, &buffer, sizeof(buffer), 0, (struct sockaddr*) &serv_addr, &servlen);
        if(receive < 0){
            error("ERROR receiving data from server\n");
        }

        char seq_buffer[2];
        strncpy(seq_buffer, buffer, 2);
        short seq = (((short)seq_buffer[0]) << 8) | seq_buffer[1];

        //print out the sending/receiving process
        flag = print_message(flag, seq, buffer);
        //if FIN is received, exit the program
        if(flag == 2){
            close(sockfd);
            fclose(datafile);
            return 0;
        }
        
        //send ACK reply to server
        n = sendto(sockfd, &seq_buffer, 2, 0, (struct sockaddr*) &serv_addr, servlen);
        if(n < 0){
            error("ERROR sending ACK to server\n");
        }

        //Keep tracker of packets in receiver window and write to file when appropriate
        short seq_num = seq/1024;
        
        if(seq_num == seq_min && seq_min > seq_max){
            shift = 31;
        }
        else{
            shift = 0;
        }

        memset(data[seq_num], 0, 1022);
        memcpy(data[seq_num], buffer+2, 1022);

        int index;
        int n;
        //int temp;
        
        if(seq_num == seq_min){
            // fwrite(&data[seq_num], 1, sizeof(data[seq_num]), datafile);
            n = write(result, &data[seq_num], receive - 2);

            tracker[seq_num] = 0;
            int i;
            for(i = seq_min + 1; i < seq_max + shift; i++){
                index = i % 31;
                if(tracker[index]==0){
                    break;
                }
                // fwrite(&data[index], 1, sizeof(data[index]), datafile);
                n = write(result, &data[index], receive - 2);

                tracker[index] = 0;
            }
            seq_min = i % 31;
            seq_max = (seq_min + 5) % 31;
        }
        //if later packet arrives first, use tracker to tracker packets
        else{
            tracker[seq_num] = 1;
        }
    }

    close(sockfd);  // close socket
    fclose(datafile);
    return 0; 
}

//function to print sending/receiving process for debug purposes
int print_message (int flag, short seq, char* buffer){

    if(flag == 0){
            printf("Receiving packet SYN\n");
            printf("Sending packet SYN\n");
            printf("Receiving packet %d\n", seq);
            printf("Sending packet %d\n", seq);
            return 1;
    }
    else{
            //the first 2 slots are used to store sequence number
        if(buffer[2] == 'F' && buffer[3] == 'I' && buffer[4] == 'N'){
            printf("Receiving packet FIN\n");
            printf("Sending packet FIN\n");
            return 2;
        }

        else{
            printf("Receiving packet %d\n", seq);
            printf("Sending packet %d\n", seq);
            return 1;
        }
    }
}





