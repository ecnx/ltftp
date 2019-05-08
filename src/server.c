/* ------------------------------------------------------------------
 * Little Tftp Server - Main Program File
 * ------------------------------------------------------------------ */

#include "server.h"

/* Show program usage message */
static void show_usage ( void )
{
    fprintf ( stderr, "usage: tftpd addr port [root]\n" );
}

/* Format IPv4 address to string */
static void inet_ntoa_s ( struct in_addr in, char *buffer, size_t limit )
{
    snprintf ( buffer, limit, "%d.%d.%d.%d",
        ( ( unsigned char * ) &in )[0],
        ( ( unsigned char * ) &in )[1],
        ( ( unsigned char * ) &in )[2], ( ( unsigned char * ) &in )[3] );
}

/* Split tftp header params into array */
static ssize_t tftp_params_split ( const unsigned char *header, size_t len, char *params,
    size_t nlimit, size_t strlimit )
{
    size_t i;
    size_t sublen;
    size_t offset = 0;

    /* at least one zero character */
    for ( i = 0; i < len; i++ )
    {
        if ( header[i] == '\0' )
        {
            break;
        }
    }

    if ( i == len )
    {
        errno = EINVAL;
        return -1;
    }

    /* split parameters into strings table */
    for ( i = 0; offset < len; i++ )
    {
        if ( i >= nlimit )
        {
            errno = ENOBUFS;
            return -1;
        }

        sublen = strlen ( ( const char * ) ( header + offset ) );
        if ( sublen >= strlimit )
        {
            errno = ENOBUFS;
            return -1;
        }

        memcpy ( params + i * strlimit, header + offset, sublen );
        params[i * strlimit + sublen] = '\0';
        offset += sublen + 1;
    }

    return i;
}

/* Parse tftp transfer mode */
static int tftp_parse_transfer_mode ( char *mode )
{
    size_t i;
    size_t len;

    for ( i = 0, len = strlen ( mode ); i < len; i++ )
    {
        mode[i] = tolower ( mode[i] );
    }

    if ( !strcmp ( mode, "octet" ) )
    {
        return TFTP_TRANSFER_MODE_OCTET;

    } else if ( !strcmp ( mode, "netascii" ) )
    {
        return TFTP_TRANSFER_MODE_NETASCII;
    } else
    {
        return -1;
    }
}

#define TFTP_PARAMS_NLIMIT 16
#define TFTP_PARAMS_STRLIMIT 256

/* Validate path name */
static int tftp_validate_path ( const char *path )
{
    return strchr ( path, '/' ) != path && strstr ( path, "../" ) == NULL;
}

/* Handle write request */
static int tftp_handle_wrq ( struct tftp_sess *sess, const unsigned char *request, size_t len )
{
    int fd;
    int transfer_mode = TFTP_TRANSFER_MODE_OCTET;
    unsigned short block = 0;
    size_t nparams;
    size_t nblocks = 0;
    socklen_t slen;
    char params[TFTP_PARAMS_NLIMIT][TFTP_PARAMS_STRLIMIT];
    unsigned char buffer[65536];

    /* split parameters */
    if ( ( ssize_t ) ( nparams =
            tftp_params_split ( request + 2, len - 2, ( char * ) params, TFTP_PARAMS_NLIMIT,
                TFTP_PARAMS_STRLIMIT ) ) < 0 )
    {
        fprintf ( stderr, "[lsrv] failed to split params: %i\n", errno );
        return errno;
    }

    /* verify params count */
    if ( !nparams )
    {
        fprintf ( stderr, "[lsrv] file path not found in request.\n" );
        return ENODATA;
    }

    /* print file path */
    printf ( "[lsrv] path : %s\n", params[0] );

    /* parse transfer mode */
    if ( nparams == 1 )
    {
        printf ( "[lsrv] assuming octet mode\n" );

    } else
    {
        if ( ( transfer_mode = tftp_parse_transfer_mode ( params[1] ) ) < 0
            || transfer_mode != TFTP_TRANSFER_MODE_OCTET )
        {
            printf ( "[lsrv] unsupported mode: %s\n", params[1] );
            return EINVAL;
        }

        printf ( "[lsrv] mode : %s\n",
            transfer_mode == TFTP_TRANSFER_MODE_OCTET ? "octet" : "netascii" );
    }

    /* validate path */
    if ( !tftp_validate_path ( params[0] ) )
    {
        fprintf ( stderr, "[lsrv] path not allowed: %i\n", errno );
        return EACCES;
    }

    /* open file for writing */
    if ( ( fd = open ( params[0], O_CREAT | O_WRONLY | O_TRUNC, 0644 ) ) < 0 )
    {
        fprintf ( stderr, "[lsrv] failed to open file: %i\n", errno );
        return errno;
    }

    if ( tftp_send_ack_packet ( sess, block++ ) < 0 )
    {
        close ( fd );
        return errno;
    }

    printf ( "[lsrv] transfer acknowledged.\n" );

    do
    {
        /* await DATA packet */
        slen = sizeof ( sess->saddr );
        if ( ( ssize_t ) ( len =
                recvfrom ( sess->sock, buffer, sizeof ( buffer ), 0,
                    ( struct sockaddr * ) &sess->saddr, &slen ) ) < 0 )
        {
            close ( fd );
            sess->exit_flag = 1;
            fprintf ( stderr, "\n[lsrv] failed to receive data: %i\n", errno );
            return errno;
        }

        /* assert packet size */
        if ( !tftp_packet_check_length ( sess->progname, 4, len ) )
        {
            close ( fd );
            return errno;
        }

        /* opcode must be DATA */
        if ( tfp_load_ushort_ns ( buffer ) != TFTP_OPCODE_DATA )
        {
            tftp_dump_packet ( sess->progname, buffer, len );
            fprintf ( stderr, "\n[lsrv] expected an ACK packet.\n" );
            continue;
        }

        /* validate data block number */
        if ( tfp_load_ushort_ns ( buffer + 2 ) != block )
        {
            fprintf ( stderr, "\n[lsrv] DATA: expected block #%u, got #%u - ignored.\n", block,
                tfp_load_ushort_ns ( buffer + 2 ) );
            continue;
        }

        /* write data to file */
        if ( write ( fd, buffer + 4, len - 4 ) < 0 )
        {
            close ( fd );
            fprintf ( stderr, "\n[lsrv] failed to write file: %i\n", errno );
            return errno;
        }

        /* send ACK packet with block set to zero */
        if ( tftp_send_ack_packet ( sess, block++ ) < 0 )
        {
            close ( fd );
            return errno;
        }

        /* show progress */
        printf ( "\r[lsrv] progress: received %lu blocks", ( unsigned long ) ++nblocks );

    } while ( len == 4 + TFTP_BLOCKSIZE );

    /* put new line */
    putchar ( '\n' );

    /* close file fd */
    close ( fd );

    return 0;
}

/* Handle read request */
static int tftp_handle_rrq ( struct tftp_sess *sess, const unsigned char *request, size_t len )
{
    int fd;
    int transfer_mode = TFTP_TRANSFER_MODE_OCTET;
    unsigned short block = 1;
    size_t nparams;
    size_t nblocks = 0;
    size_t lastread;
    socklen_t slen;
    struct ack_packet ack;
    unsigned char buffer[4096];
    char params[TFTP_PARAMS_NLIMIT][TFTP_PARAMS_STRLIMIT];

    /* split parameters */
    if ( ( ssize_t ) ( nparams =
            tftp_params_split ( request + 2, len - 2, ( char * ) params, TFTP_PARAMS_NLIMIT,
                TFTP_PARAMS_STRLIMIT ) ) < 0 )
    {
        fprintf ( stderr, "[lsrv] failed to split params: %i\n", errno );
        return errno;
    }

    /* verify params count */
    if ( !nparams )
    {
        fprintf ( stderr, "[lsrv] file path not found in request.\n" );
        return ENODATA;
    }

    /* print file path */
    printf ( "[lsrv] path : %s\n", params[0] );

    /* parse transfer mode */
    if ( nparams == 1 )
    {
        printf ( "[lsrv] assuming octet mode\n" );

    } else
    {
        if ( ( transfer_mode = tftp_parse_transfer_mode ( params[1] ) ) < 0 )
        {
            printf ( "[lsrv] unsupported mode: %s\n",
                transfer_mode == TFTP_TRANSFER_MODE_OCTET ? "octet" : "netascii" );
            return EINVAL;
        }

        printf ( "[lsrv] mode : %s\n",
            transfer_mode == TFTP_TRANSFER_MODE_OCTET ? "octet" : "netascii" );
    }

    /* validate path */
    if ( !tftp_validate_path ( params[0] ) )
    {
        fprintf ( stderr, "[lsrv] path not allowed: %i\n", errno );
        return EACCES;
    }


    /* open file for reading */
    if ( ( fd = open ( params[0], O_RDONLY ) ) < 0 )
    {
        fprintf ( stderr, "[lsrv] failed to open file: %i\n", errno );
        return errno;
    }

    /* send file data */
    for ( lastread = 0, nblocks = 0;
        ( ssize_t ) ( len = read ( fd, buffer + 4, TFTP_BLOCKSIZE ) ) > 0
        || lastread == TFTP_BLOCKSIZE; block++ )
    {
        /* save last read data count */
        lastread = len;

        /* prepare data packet */
        tfp_store_ushort_ns ( buffer, TFTP_OPCODE_DATA );
        tfp_store_ushort_ns ( buffer + 2, block );

        /* send DATA pakcet */
        if ( sendto_autoretry ( sess->sock, buffer, 4 + len, 0, ( struct sockaddr * ) &sess->saddr,
                sizeof ( sess->saddr ) ) < 0 )
        {
            close ( fd );
            sess->exit_flag = 1;
            fprintf ( stderr, "\n[lsrv] failed to send data: %i\n", errno );
            return errno;
        }

      recv_ack:

        /* await ACK packet */
        slen = sizeof ( sess->saddr );
        if ( ( ssize_t ) ( len =
                recvfrom ( sess->sock, &ack, sizeof ( ack ), 0, ( struct sockaddr * ) &sess->saddr,
                    &slen ) ) < 0 )
        {
            close ( fd );
            sess->exit_flag = 1;
            fprintf ( stderr, "\n[lsrv] failed to receive data: %i\n", errno );
            return errno;
        }

        /* assert packet size */
        if ( !tftp_packet_check_length ( sess->progname, 4, len ) )
        {
            close ( fd );
            return EMSGSIZE;
        }

        /* opcode must be ACK */
        if ( ntohs ( ack.opcode ) != TFTP_OPCODE_ACK )
        {
            close ( fd );
            tftp_dump_packet ( sess->progname, buffer, len );
            fprintf ( stderr, "\n[lsrv] expected an ACK packet.\n" );
            return EINVAL;
        }

        /* skip ACK for previous blocks */
        if ( ntohs ( ack.block ) != block )
        {
            fprintf ( stderr, "\n[lsrv] ACK: expected block #%u, got #%u - ignored.\n", block,
                ntohs ( ack.block ) );
            goto recv_ack;
        }

        /* show progress */
        printf ( "\r[lsrv] progress: sent %lu blocks", ( unsigned long ) ++nblocks );
    }

    /* put new line */
    putchar ( '\n' );

    /* close file fd */
    close ( fd );

    /* check for reading failure */
    if ( ( ssize_t ) len < 0 )
    {
        fprintf ( stderr, "[lsrv] failed to read file: %i\n", errno );
        return EINVAL;
    }

    return 0;
}

/* Accept client peer and handle tftp operation */
static int tftp_handle_operation ( struct tftp_sess *sess )
{
    unsigned short opcode;
    size_t len;
    socklen_t slen;
    char addrbuf[32];
    unsigned char buffer[4096];

    /* receive datagram from remote peer */
    slen = sizeof ( sess->saddr );
    if ( ( ssize_t ) ( len =
            recvfrom ( sess->sock, buffer, sizeof ( buffer ), 0, ( struct sockaddr * ) &sess->saddr,
                &slen ) ) < 0 )
    {
        fprintf ( stderr, "[lsrv] failed to receive data: %i\n", errno );
        sess->exit_flag = 1;
    }

    /* validate client address length */
    if ( slen == sizeof ( sess->saddr ) )
    {
        inet_ntoa_s ( sess->saddr.sin_addr, addrbuf, sizeof ( addrbuf ) );
        printf ( "[lsrv] accepted peer %s\n", addrbuf );
    }

    /* assert packet size */
    if ( !tftp_packet_check_length ( sess->progname, 2, len ) )
    {
        return EMSGSIZE;
    }

    /* dump received packet */
    tftp_dump_packet ( sess->progname, buffer, len );

    /* extract opcode value */
    opcode = tfp_load_ushort_ns ( buffer );

    /* branch according to opcode */
    switch ( opcode )
    {
    case TFTP_OPCODE_WRQ:
        printf ( "[lsrv] handling write request ...\n" );
        return tftp_handle_wrq ( sess, buffer, len );
        break;
    case TFTP_OPCODE_RRQ:
        printf ( "[lsrv] handling read request ...\n" );
        return tftp_handle_rrq ( sess, buffer, len );
        break;
    default:
        fprintf ( stderr, "[lsrv] packet has been ignored.\n" );
        return EINVAL;
    }
}

/* Program main function */
int main ( int argc, char *argv[] )
{
    int status;
    unsigned int yes = 1;
    unsigned int addr;
    unsigned int port;
    struct tftp_sess sess;
    const char* errmsg;

    setbuf ( stdout, NULL );
    printf ( "[lsrv] Little Tftp Server - ver. 1.0.01\n" );

    /* validate arguments count */
    if ( argc < 3 )
    {
        show_usage (  );
        return 1;
    }

    /* change root if needed */
    if ( argc > 3 )
    {
        if ( chdir ( argv[3] ) < 0 )
        {
            fprintf ( stderr, "[lsrv] failed to change directory: %i\n", errno );
            return 1;
        }

        if ( chroot ( argv[3] ) < 0 )
        {
            fprintf ( stderr, "[lsrv] failed to change root: %i\n", errno );
            return 1;
        } else
        {
            printf ( "[lsrv] root changed to %s\n", argv[3] );
        }
    }

    /* parse IPv4 address */
    if ( inet_pton ( AF_INET, argv[1], &addr ) <= 0 )
    {
        show_usage (  );
        return 1;
    }

    /* parse port number */
    if ( sscanf ( argv[2], "%u", &port ) <= 0 || port >= 65536 )
    {
        show_usage (  );
        return 1;
    }

    /* allocate server socket */
    if ( ( sess.sock = socket ( AF_INET, SOCK_DGRAM, 0 ) ) < 0 )
    {
        fprintf ( stderr, "[lsrv] failed to allocate socket: %i\n", errno );
        return 1;
    }

    printf ( "[lsrv] socket allocated.\n" );

    /* allow reusing socket address */
    setsockopt ( sess.sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof ( yes ) );

    /* prepare socket address */
    memset ( &sess.saddr, '\0', sizeof ( sess.saddr ) );
    sess.saddr.sin_family = AF_INET;
    sess.saddr.sin_addr.s_addr = addr;
    sess.saddr.sin_port = htons ( port );

    /* bind socket to address */
    if ( bind ( sess.sock, ( struct sockaddr * ) &sess.saddr, sizeof ( sess.saddr ) ) < 0 )
    {
        close ( sess.sock );
        fprintf ( stderr, "[lsrv] failed to bind socket: %i\n", errno );
        return 1;
    }

    printf ( "[lsrv] listenning on socket ...\n" );

    /* set exit flag to false */
    sess.exit_flag = 0;

    /* set program name */
    sess.progname = "lsrv";

    /* reset session address */
    memset ( &sess.saddr, '\0', sizeof ( sess.saddr ) );

    /* accept and handle peers */
    while ( !sess.exit_flag )
    {
        status = tftp_handle_operation ( &sess );
        if ( !status )
        {
            fprintf ( stderr, "[lsrv] status: success\n" );
            continue;
        }

        errmsg = strerror ( status );
        fprintf ( stderr, "[lsrv] status: failure %i (%s)\n", status, errmsg );

        switch ( status )
        {
        case EINVAL:
            tftp_send_error_packet ( &sess, TFTP_ERROR_ILLEGAL_OPERATION );
            break;
        case EPERM:
        case EACCES:
            tftp_send_error_packet ( &sess, TFTP_ERROR_ACCESS_VIOLATION );
            break;
        case EDQUOT:
            tftp_send_error_packet ( &sess, TFTP_ERROR_DISK_FULL );
            break;
        default:
            tftp_send_error_packet ( &sess, TFTP_ERROR_NOT_DEFINED );
        }
    }

    /* close socket */
    close ( sess.sock );

    printf ( "[lsrv] server stopped.\n" );

    return 0;
}
