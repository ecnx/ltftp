/* ------------------------------------------------------------------
 * Little Tftp - Auxiliary Functions
 * ------------------------------------------------------------------ */

#include "tftp.h"

/* Prepare tftp header */
ssize_t tftp_prepare_header ( unsigned char *header, size_t limit, unsigned short opcode,
    const char **params )
{
    size_t i;
    size_t len;
    size_t offset = 2;

    if ( limit < sizeof ( unsigned short ) )
    {
        errno = ENOBUFS;
        return -1;
    }

    opcode = htons ( opcode );
    memcpy ( header, &opcode, sizeof ( unsigned short ) );

    if ( params == NULL )
    {
        return offset;
    }

    for ( i = 0; params[i] != NULL; i++, offset++ )
    {
        len = strlen ( params[i] );
        if ( offset + len >= limit )
        {
            return ENOBUFS;
        }
        memcpy ( header + offset, params[i], len );
        offset += len;
        header[offset] = '\0';
    }

    return offset;
}

/* Validate packet length */
int tftp_packet_check_length ( const char *prefix, size_t expected, size_t got )
{
    if ( got < expected )
    {
        fprintf ( stderr, "[%s] received %lu bytes, expected %lu bytes at least.\n", prefix,
            ( unsigned long ) got, ( unsigned long ) expected );
        return 0;
    }

    return 1;
}

/* Load unsigned short in network system from byte array */
unsigned short tfp_load_ushort_ns ( const unsigned char *buffer )
{
    return ntohs ( *( ( unsigned short * ) buffer ) );
}

/* Store unsigned short in network system into byte array */
void tfp_store_ushort_ns ( unsigned char *buffer, unsigned short value )
{
    *( ( unsigned short * ) buffer ) = htons ( value );
}

/* Get tftp error code description */
static const char *tftp_get_errmsg ( int code )
{
    switch ( code )
    {
    case TFTP_ERROR_NOT_DEFINED:
        return "Not defined, see error message (if any).";
    case TFTP_ERROR_FILE_NOT_FOUND:
        return "File not found.";
    case TFTP_ERROR_ACCESS_VIOLATION:
        return "Access violation.";
    case TFTP_ERROR_DISK_FULL:
        return "Disk full or allocation exceeded.";
    case TFTP_ERROR_ILLEGAL_OPERATION:
        return "Illegal TFTP operation.";
    case TFTP_ERROR_UNKNOWN_TRANSFER_ID:
        return "Unknown transfer ID.";
    case TFTP_ERROR_FILE_ALREADY_EXISTS:
        return "File already exists.";
    case TFTP_ERROR_NO_SUCH_USER:
        return "No such user.";
    default:
        return "Unknown";
    }
}

/* Dump tftp packet */
void tftp_dump_packet ( const char *prefix, const unsigned char *packet, size_t len )
{
    unsigned short opcode;

    if ( !tftp_packet_check_length ( prefix, 2, len ) )
    {
        return;
    }

    opcode = tfp_load_ushort_ns ( packet );

    switch ( opcode )
    {
    case TFTP_OPCODE_RRQ:
        printf ( "[%s] received packet: RRQ\n", prefix );
        break;
    case TFTP_OPCODE_WRQ:
        printf ( "[%s] received packet: WRQ\n", prefix );
        break;
    case TFTP_OPCODE_DATA:
        if ( !tftp_packet_check_length ( prefix, 4, len ) )
        {
            return;
        }
        printf ( "[%s] received packet: DATA\n       block : #%u\n       size  : %lu\n\n", prefix,
            tfp_load_ushort_ns ( packet + 2 ), len - 4 );
        break;
    case TFTP_OPCODE_ACK:
        if ( !tftp_packet_check_length ( prefix, 4, len ) )
        {
            return;
        }
        printf ( "[%s] received packet: ACK\n       block : #%u\n\n", prefix,
            tfp_load_ushort_ns ( packet + 2 ) );
        break;
    case TFTP_OPCODE_ERROR:
        if ( !tftp_packet_check_length ( prefix, 4, len ) )
        {
            return;
        }

        if ( len > 4 && packet[len - 1] == '\0' )
        {
            printf ( "[%s] received packet: ERROR\n       code : %u %s\n       desc : %s\n",
                prefix, tfp_load_ushort_ns ( packet + 2 ),
                tftp_get_errmsg ( tfp_load_ushort_ns ( packet + 2 ) ), packet + 4 );
        } else
        {
            printf ( "[%s] received packet: ERROR\n       code : %u %s\n\n", prefix,
                tfp_load_ushort_ns ( packet + 2 ),
                tftp_get_errmsg ( tfp_load_ushort_ns ( packet + 2 ) ) );
        }
        break;

    default:
        printf ( "[%s] received packet: UNKNOWN\n       opcode : %u\n       size   : %lu\n\n",
            prefix, opcode, ( unsigned long ) len );
    }
}

/* Send ACK packet over tftp protocol */
int tftp_send_ack_packet ( struct tftp_sess *sess, unsigned short block )
{
    struct ack_packet packet;

    /* prepare ACK packet */
    packet.opcode = htons ( TFTP_OPCODE_ACK );
    packet.block = htons ( block );

    /* send ACK packet */
    if ( sendto ( sess->sock, &packet, sizeof ( packet ), 0, ( struct sockaddr * ) &sess->saddr,
            sizeof ( sess->saddr ) ) < 0 )
    {
        sess->exit_flag = 1;
        fprintf ( stderr, "[%s] failed to send data: %i\n", sess->progname, errno );
        return -1;
    }

    return 0;
}

/* Send ERROR packet over tftp protocol */
int tftp_send_error_packet ( struct tftp_sess *sess, unsigned short code )
{
    size_t len;
    struct error_packet packet;
    const char *message;

    /* prepare ACK packet */
    packet.opcode = htons ( TFTP_OPCODE_ERROR );
    packet.block = htons ( code );

    /* obtain error message */
    message = tftp_get_errmsg ( code );

    /* validate error message length */
    if ( ( len = strlen ( message ) ) >= sizeof ( packet.message ) )
    {
        errno = E2BIG;
        return -1;
    }

    /* place error message into packet */
    memcpy ( packet.message, message, len );
    packet.message[len] = '\0';

    /* send ERROR packet */
    if ( sendto ( sess->sock, &packet, 4 + len + 1, 0, ( struct sockaddr * ) &sess->saddr,
            sizeof ( sess->saddr ) ) < 0 )
    {
        sess->exit_flag = 1;
        fprintf ( stderr, "[%s] failed to send data: %i\n", sess->progname, errno );
        return -1;
    }

    return 0;
}

/* Sendto with autoretry on timeout feature */
ssize_t sendto_autoretry ( int sockfd, const void *buf, size_t len, int flags,
    const struct sockaddr * dest_addr, socklen_t addrlen )
{
    int status;
    ssize_t ret = 0;
    struct pollfd fds[1];

    /* Preapre poll fd list */
    fds[0].fd = sockfd;
    fds[0].events = POLLIN;

  retry:

    /* Request sending data to remote peer */
    if ( ( ret = sendto ( sockfd, buf, len, flags, dest_addr, addrlen ) ) < 0 )
    {
        return ret;
    }

    /* Perform poll operation */
    if ((status = poll ( fds, sizeof ( fds ) / sizeof ( struct pollfd ), TFTP_TIMEOUT_MSEC )) < 0
        || fds[0].revents & POLLHUP)
    {
        return -1;

    } else if ( !status )
    {
        goto retry;
    }

    return ret;
}
