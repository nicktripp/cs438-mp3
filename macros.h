#ifndef MACROS_H
#define MACROS_H

#define _MTU 1500
#define _UDP_HEADER_SIZE 8
#define _IP_HEADER_SIZE 20
#define PACKET_SIZE (_MTU - _UDP_HEADER_SIZE - _IP_HEADER_SIZE)

// TRIPP-P: Tripp-Protocol
// TRIPP-P Header:
//  uint32 ack = frame #
//  uint32 seq_num = # of times retransmitted
//  byte last = 0 | 1.  // 1 if last frame
#define TRIPP_P_HEADER_SIZE 5
#define DATA_SIZE (PACKET_SIZE - TRIPP_P_HEADER_SIZE)
#define ACK_SIZE 4

#define debug_print(fmt, ...) \
            do { if (DEBUG) fprintf(stderr, fmt, __VA_ARGS__); } while (0)

#endif /* MACROS_H */