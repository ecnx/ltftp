/* ------------------------------------------------------------------
 * Little Tftp - Auxiliary Header
 * ------------------------------------------------------------------ */

#include "config.h"

#ifndef LTFTP_H
#define LTFTP_H

/* TFTP data block size */
#define TFTP_BLOCKSIZE 512

/* TFTP timeout settings */
#define TFTP_TIMEOUT_MSEC 1000

/* TFTP opcodes list */
#define TFTP_OPCODE_RRQ 1
#define TFTP_OPCODE_WRQ 2
#define TFTP_OPCODE_DATA 3
#define TFTP_OPCODE_ACK 4
#define TFTP_OPCODE_ERROR 5

/* TFTP error codes list */
#define TFTP_ERROR_NOT_DEFINED 0
#define TFTP_ERROR_FILE_NOT_FOUND 1
#define TFTP_ERROR_ACCESS_VIOLATION 2
#define TFTP_ERROR_DISK_FULL 3
#define TFTP_ERROR_ILLEGAL_OPERATION 4
#define TFTP_ERROR_UNKNOWN_TRANSFER_ID 5
#define TFTP_ERROR_FILE_ALREADY_EXISTS 6
#define TFTP_ERROR_NO_SUCH_USER 7

/* TFTP transfer modes */
#define TFTP_TRANSFER_MODE_NETASCII 0
#define TFTP_TRANSFER_MODE_OCTET 1

/* TFTP session structure */
struct tftp_sess
{
    int exit_flag;
    int sock;
    struct sockaddr_in saddr;
    const char *progname;
};

/* TFTP ACK packet structure */
struct ack_packet
{
    unsigned short opcode;
    unsigned short block;
};

/* TFTP ERROR packet structure */
struct error_packet
{
    unsigned short opcode;
    unsigned short block;
    char message[256];
};

/* Prepare tftp header */
extern ssize_t tftp_prepare_header ( unsigned char *header, size_t limit, unsigned short opcode,
    const char **params );

/* Validate packet length */
extern int tftp_packet_check_length ( const char *prefix, size_t expected, size_t got );

/* Load unsigned short in network system from byte array */
extern unsigned short tfp_load_ushort_ns ( const unsigned char *buffer );

/* Store unsigned short in network system into byte array */
extern void tfp_store_ushort_ns ( unsigned char *buffer, unsigned short value );

/* Dump tftp packet */
extern void tftp_dump_packet ( const char *prefix, const unsigned char *packet, size_t len );

/* Send ACK packet over tftp protocol */
extern int tftp_send_ack_packet ( struct tftp_sess *sess, unsigned short block );

/* Send ERROR packet over tftp protocol */
extern int tftp_send_error_packet ( struct tftp_sess *sess, unsigned short code );

/* Sendto with autoretry on timeout feature */
extern ssize_t sendto_autoretry ( int sockfd, const void *buf, size_t len, int flags,
    const struct sockaddr *dest_addr, socklen_t addrlen );

#endif
