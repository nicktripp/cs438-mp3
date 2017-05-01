#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <pthread.h>
#include <time.h>
#include <sys/time.h>
#include <string.h>
#include <unistd.h>

#define DEBUG 1

#include "macros.h"


pthread_cond_t c;
pthread_mutex_t m;

/**
 *  Subtract the ‘struct timeval’ values X and Y, (X-Y)
 *  storing the result in RESULT.
 *  Return 1 if the difference is negative, otherwise 0.
 *  Adapted from gnu.org and stackoverflow.com
 */
int timeval_subtract (struct timeval * result, struct timeval * x, struct timeval * y)
{
	result->tv_sec = x->tv_sec - y->tv_sec;

	if ((result->tv_usec = x->tv_usec - y->tv_usec) < 0)
	{
		result->tv_usec += 1000000;
		result->tv_sec--; // borrow
	}

	return result->tv_sec < 0;
}




// Seq_num
uint32_t sws; //  Send Window Size
uint32_t NAE; // Next ACK Expected
uint32_t NFS; // Next Frame to Sent
int RTT_MS;// Initial RTT = 100ms
char ** sentBuff;
int * sentBuffSize;
struct timeval * sentStamp;
unsigned long long int bytesTransferred;
int sock_fd;
uint32_t lastFrame;
int lastACKed;


// Listen for ACKs on another thread:
void * listenForAck(void * unused)
{
	while(1)
	{
		char buff[ACK_SIZE];
		int bytesRecvd;
		if(4 != (bytesRecvd = recv(sock_fd, buff, ACK_SIZE, 0)))
			continue; // Error with receive, just loop again

		uint32_t fACK;
		memcpy(&fACK, &buff[0], sizeof(fACK));
		fACK = ntohl(fACK);

		pthread_mutex_lock(&m);
		// GO-BACK-N
		if (fACK == NAE)
		{
			debug_print("Accepted ACK #%u\n", fACK);

			free(sentBuff[0]);
			bytesTransferred += sentBuffSize[0];
			// Shift window up:
			for (int i = 1; i < sws; i++)
			{
				sentBuff[i-1] = sentBuff[i];
				sentBuffSize[i-1] = sentBuffSize[i];
				sentStamp[i-1] = sentStamp[i];
			}

			// Clear new frame
			sentBuff[sws-1] = NULL;
			sentBuffSize[sws-1] = 0;
			memset(&sentStamp[sws-1], 0, sizeof(struct timeval));// Change Timestamp

			NAE++;

			if (lastFrame == fACK)
				lastACKed = 1;
		}
		else
		{
			debug_print("Rejected ACK #%u\n", fACK);

		}
		pthread_mutex_unlock(&m);

	}


}


void reliablyTransfer(char* hostname, unsigned short int hostUDPport, char* filename, unsigned long long int bytesToTransfer)
{


	struct addrinfo hints;
	struct addrinfo *res;
	int status;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_DGRAM;

	char portBuff[32];
	sprintf(portBuff, "%hu", hostUDPport);
	if ( 0 != (status = getaddrinfo(hostname, portBuff, &hints, &res)))
	{
		fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(status));
		freeaddrinfo(res);
		exit(1);
	}

	if ((sock_fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol)) == -1)
	{
		perror("socket error: ");
		freeaddrinfo(res);
		exit(1);
	}

	if (-1 == connect(sock_fd, res->ai_addr, res->ai_addrlen))
	{
		perror("connect error: ");
		freeaddrinfo(res);
		close(sock_fd);
		exit(1);
	}
	freeaddrinfo(res);



	// Open File //
	FILE * file;
	if (NULL == (file = fopen(filename, "r")))
	{
		perror("fopen error: ");
		exit(1);
	}

	// TODO: Handshake? SYN / ACK / SYN-ACK NOTE: Not necessary!



	sws = 5; // Initial Send Window Size
	NAE = 0; // Next ACK Expected
	NFS = 0; // Next Frame to Send
	RTT_MS = 500;// Initial RTT = 100ms

	sentBuff = malloc(sws * sizeof(char *));
	sentBuffSize = malloc(sws * sizeof(int));
	memset(sentBuffSize, 0, sws * sizeof(int));
	sentStamp = malloc(sws * sizeof(struct timeval)); // Track the time of sent packet
	memset(sentStamp, 0, sws * sizeof(struct timeval));

	lastFrame = -1;
	lastACKed = 0;
	int lastBuffered = 0;

	pthread_cond_init(&c, NULL);
	pthread_mutex_init(&m, NULL);

	bytesTransferred = 0;

	// Start ACK listen thread
	pthread_t ackThread;
	pthread_create(&ackThread, 0, listenForAck, (void*)0);

	int done = 0;
	while (!done)
	{
		// TODO Recompute SWS
		//sentBuff = realloc(sws * sizeof(char *));

		// NOTE: frames are cleared when ACK is heard

		// Fill send Window buffer if not full
		pthread_mutex_lock(&m);
		int newlyQueuedFrames = 0;
		for (int i = NFS; i < NAE + sws; i++)
		{
			if(lastBuffered)
				break;

			uint32_t frame = i;
			uint32_t frameIdx = i-NAE;

			newlyQueuedFrames++;

			char last = 0;
			if(DATA_SIZE * (newlyQueuedFrames + NFS ) >= bytesToTransfer)
				last = 1;

			sentBuff[frameIdx] = malloc(PACKET_SIZE);

			uint32_t net_frame = htonl(frame);
			// Add Header
			memcpy(&sentBuff[frameIdx][0], &net_frame, sizeof(net_frame));
			memcpy(&sentBuff[frameIdx][0]+sizeof(net_frame), &last, sizeof(last));
			int dataSize;
			if (last)
			{
				dataSize = bytesToTransfer - DATA_SIZE * (newlyQueuedFrames + NFS - 1);
				lastBuffered = 1;
				lastFrame = frame;
			}
			else
				dataSize = DATA_SIZE;

			uint32_t frame2;
			memcpy(&frame2, &sentBuff[frameIdx][0], sizeof(frame2));
			frame2 = ntohl(frame2);

			debug_print("Reading for frame #%u:%u:%u\n", frame, ntohl(net_frame), frame2);

			int readSize;
			if (0 == (readSize = read(fileno(file), &sentBuff[frameIdx][0] + TRIPP_P_HEADER_SIZE, dataSize)))
			{
				done = 1;
			}
			else if (-1 == readSize)
			{
				perror("read error: ");
				pthread_mutex_destroy(&m);
				pthread_cond_destroy(&c);
				for(int k = 0; k < sws; k++)
					if(sentBuff[k] != NULL)
						free(sentBuff[k]);
				free(sentBuff);
				free(sentBuffSize);
				free(sentStamp);
				close(sock_fd);
				fclose(file);
				exit(1);
			}

			if (dataSize != readSize)
			{
				debug_print("Expected fread size of (%d) != actual fread size of (%d)!\n", dataSize, readSize);
			}

			sentBuffSize[frameIdx] = readSize + TRIPP_P_HEADER_SIZE;
			memset(&sentStamp[frameIdx], 0, sizeof(struct timeval));// Change Timestamp


			if(last)
				break;
		}
		pthread_mutex_unlock(&m);


		// Send Frames in range(NAE, NAE+sws), if timestamp is expired
		pthread_mutex_lock(&m);
		for (int i = 0; i < sws; i++)
		{
			if (sentBuff[i] == NULL) // past Last frame; skip
			{
				debug_print("NULL frame #%d\n", NAE+i);
				break;
			}
			// Check if timestamp[i] is expired
			struct timeval now, diff;
			gettimeofday(&now,0);
			timeval_subtract(&diff, &now, &sentStamp[i]);

			debug_print("Do we send frame #%d?\n", NAE+i);
			// Resend frame if timestamp expired
			if (diff.tv_sec > 0 || diff.tv_usec > (RTT_MS * 1000) )
			{
				debug_print("Preparing to send frame #%d!\n", NAE+i);

				int j = 0;
				int bytesSent;
				while ( -1 == (bytesSent = send(sock_fd, sentBuff[i], sentBuffSize[i], 0)) )
				{
					if (j < 10) // Try 10 times
						j++;
					else // Error sending
					{
						pthread_mutex_unlock(&m);
						perror("send error: ");
						pthread_mutex_destroy(&m);
						pthread_cond_destroy(&c);
						for(int k = 0; k < sws; k++)
							if(sentBuff[k] != NULL)
								free(sentBuff[k]);
						free(sentBuff);
						free(sentBuffSize);
						free(sentStamp);
						close(sock_fd);
						fclose(file);
						exit(1);
					}
				}
				debug_print("Still Preparing to send frame #%d!\n", NAE+i);

				// Properly Sent
				uint32_t frame;
				memcpy(&frame, &sentBuff[i][0], sizeof(frame));
				frame = ntohl(frame);

				debug_print("Sending frame #%u:%u of size %d\n", NAE+i, frame, sentBuffSize[i]);
				if (sentBuffSize[i] != bytesSent)
				{
					debug_print("Frame #%u: Expected send size of (%d) != actual send size of (%d)!\n", frame, sentBuffSize[i], bytesSent);
				}

				gettimeofday(&sentStamp[i], 0); // Track time sent packet
				if (NFS == frame) // Advance NFS if need be
				{
					NFS = frame + 1;
					debug_print("The next frame to send is %u\n", NFS);
				}

			}
		}
		pthread_mutex_unlock(&m);

		// sendto(int socket, const void *buffer, size_t length, int flags, const struct sockaddr *dest_addr, socklen_t dest_len);

		// HANG until woken up (by ACK or timer) NOTE: not necessary
		if (!done)
			done = bytesTransferred >= bytesToTransfer || lastACKed;
	}

	debug_print("Tearing down sender:%d\n", 0);

	pthread_mutex_destroy(&m);
	pthread_cond_destroy(&c);
	for(int i = 0; i < sws; i++)
		if(sentBuff[i] != NULL)
			free(sentBuff[i]);
	free(sentBuff);
	free(sentBuffSize);
	free(sentStamp);
	close(sock_fd);
	fclose(file);
}







int main(int argc, char** argv)
{
	unsigned short int udpPort;
	unsigned long long int numBytes;

	if(argc != 5)
	{
		fprintf(stderr, "usage: %s receiver_hostname receiver_port filename_to_xfer bytes_to_xfer\n\n", argv[0]);
		exit(1);
	}
	udpPort = (unsigned short int)atoi(argv[2]);
	numBytes = atoll(argv[4]);

	reliablyTransfer(argv[1], udpPort, argv[3], numBytes);
	return 0;
}
