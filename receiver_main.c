#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>

#define DEBUG 1
#include "macros.h"

void reliablyReceive(unsigned short int myUDPport, char* destinationFile)
{
	int sock_fd, new_fd;  // listen on sock_fd, new connection on new_fd
	struct addrinfo hints, *res, *p;
	int yes = 1;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_flags = AI_PASSIVE; // use my IP

	int status;
	char portBuff[32];
	sprintf(portBuff, "%hu", myUDPport);
	if ((status = getaddrinfo(NULL, portBuff, &hints, &res)) != 0)
	{
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(status));
		exit(1);
	}

	// loop through all the results and bind to the first we can
	for(p = res; p != NULL; p = p->ai_next)
	{
		if ((sock_fd = socket(p->ai_family, p->ai_socktype,	p->ai_protocol)) == -1)
		{
			perror("socket error: ");
			continue;
		}

		if (setsockopt(sock_fd, SOL_SOCKET, (SO_REUSEADDR | SO_REUSEPORT), &yes, sizeof(int)) == -1)
		{
			perror("setsockopt error: ");
			close(sock_fd);
			exit(1);
		}

		if (bind(sock_fd, p->ai_addr, p->ai_addrlen) == -1)
		{
			close(sock_fd);
			perror("bind error: ");
			continue;
		}

		break;
	}
	freeaddrinfo(res); // all done with this structure

	if (p == NULL)
	{
		fprintf(stderr, "failed to bind\n");
		exit(1);
	}


	// Open File //
	FILE * file;
	if (NULL == (file = fopen(destinationFile, "w")))
	{
		perror("fopen error: ");
		close(sock_fd);
		exit(1);
	}

	struct sockaddr_storage their_addr; // connector's address information
	socklen_t addr_size = sizeof their_addr;

	uint32_t NFE = 0; // Next Frame Expected

	char frameBuff[PACKET_SIZE];
	int bytesRecvd;
	int done = 0;
	while(!done)
	{  // main recv() loop
		bytesRecvd = recvfrom(sock_fd, frameBuff, PACKET_SIZE, 0, (struct sockaddr *)&their_addr, &addr_size);
		if (bytesRecvd == -1)
		{
			perror("Reciever recvfrom error: ");
			close(sock_fd);
			fclose(file);
			exit(1);
		}
		else if (bytesRecvd == 0)
			break;

		// Check frame # recieved
		uint32_t net_frame = 0;
		char last;
		memcpy(&net_frame, &frameBuff[0], sizeof(net_frame));
		memcpy(&last, &frameBuff[0] + sizeof(net_frame), sizeof(last));
		uint32_t frame = ntohl(net_frame);


		// GO-BACK-N
		if (frame == NFE)
		{
			debug_print("Accepted frame #%u of size %d\n", frame, bytesRecvd);

			int bytesWritten;
			if( -1 == (bytesWritten = write(fileno(file), frameBuff + TRIPP_P_HEADER_SIZE, bytesRecvd - TRIPP_P_HEADER_SIZE)) )
			{
				perror("write: ");
				close(sock_fd);
				fclose(file);
				exit(1);
			}
			fflush(file);
			NFE++;
			debug_print("The next frame expected is %u\n", NFE);
		}
		else
		{
			debug_print("Rejected frame #%u of size %d\n", frame, bytesRecvd);
		}

		// Send ACK for NFE-1
		uint32_t net_fACK = htonl(NFE-1);
		if ( -1 == sendto(sock_fd, &net_fACK, ACK_SIZE, 0, (struct sockaddr *)&their_addr, addr_size) )
		{
			perror("sendto ack error: ");
			close(sock_fd);
			fclose(file);
			exit(1);
		}
		debug_print("Sending ACK #%u\n", NFE-1);

		if (last)
		{
			done = 1;
			debug_print("Receiver encountered last frame:%d\n",0);
		}
	}

	debug_print("Tearing down receiver:%d\n",0);

	fflush(file);

	close(sock_fd);
	fclose(file);
}

int main(int argc, char** argv)
{
	unsigned short int udpPort;

	if(argc != 3)
	{
		fprintf(stderr, "usage: %s UDP_port filename_to_write\n\n", argv[0]);
		exit(1);
	}

	udpPort = (unsigned short int)atoi(argv[1]);

	reliablyReceive(udpPort, argv[2]);
	return 0;
}
