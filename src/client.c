/* ------------------------------------------------------------------
 * Little Tftp Client - Main Program File
 * ------------------------------------------------------------------ */

#include "client.h"

/* Show program usage message */
static void show_usage ( void )
{
    fprintf ( stderr, "usage: tftp addr port [-c put|get filename]\n" );
}

/* Print available tftp commands */
static void print_help ( void )
{
    printf ( "[tftp] list of available commands\n\n"
        "       put file - upload file\n"
        "       get file - download file\n"
        "       help     - print help\n" "       exit     - quit session\n\n" );
}

/* Upload file over tftp protocol */
static int tftp_put_file ( struct tftp_sess *sess, const char *path )
{
    int fd;
    unsigned short opcode;
    unsigned short block;
    size_t len;
    size_t nblocks;
    size_t lastread;
    socklen_t slen;
    const char *params[] = {
        path,
        "octet",
        NULL
    };
    unsigned char buffer[4096];
    struct ack_packet ack;

    /* open file for reading */
    if ( ( fd = open ( path, O_RDONLY ) ) < 0 )
    {
        return errno;
    }

    /* prepare tftp packet */
    if ( ( ssize_t ) ( len =
            tftp_prepare_header ( buffer, sizeof ( buffer ), TFTP_OPCODE_WRQ, params ) ) < 0 )
    {
        return errno;
    }

    /* send WRQ packet */
    if ( sendto_autoretry ( sess->sock, buffer, len, 0, ( struct sockaddr * ) &sess->saddr,
            sizeof ( sess->saddr ) ) < 0 )
    {
        close ( fd );
        sess->exit_flag = 1;
        fprintf ( stderr, "[tftp] failed to send data: %i\n", errno );
        return errno;
    }

    printf ( "[tftp] write request sent.\n" );
    printf ( "[tftp] awaiting response ...\n" );

    /* await ACK packet */
    slen = sizeof ( sess->saddr );
    if ( ( ssize_t ) ( len =
            recvfrom ( sess->sock, buffer, sizeof ( buffer ), 0, ( struct sockaddr * ) &sess->saddr,
                &slen ) ) < 0 )
    {
        close ( fd );
        sess->exit_flag = 1;
        fprintf ( stderr, "[tftp] failed to receive data: %i\n", errno );
        return errno;
    }

    /* dump received packet */
    tftp_dump_packet ( sess->progname, buffer, len );

    /* assert packet size */
    if ( !tftp_packet_check_length ( sess->progname, 4, len ) )
    {
        close ( fd );
        return EMSGSIZE;
    }

    /* extract opcode value */
    opcode = tfp_load_ushort_ns ( buffer );

    /* opcode must be ACK */
    if ( opcode != TFTP_OPCODE_ACK )
    {
        close ( fd );
        fprintf ( stderr, "[tftp] expected an ACK packet.\n" );
        return EINVAL;
    }

    /* extract block value */
    block = tfp_load_ushort_ns ( buffer + 2 );

    /* validate block number */
    if ( block != 0 )
    {
        close ( fd );
        fprintf ( stderr, "[tftp] expected first block to be #0.\n" );
        return EINVAL;
    }

    /* send file data */
    for ( lastread = 0, nblocks = 0;
        ( ssize_t ) ( len = read ( fd, buffer + 4, TFTP_BLOCKSIZE ) ) > 0
        || lastread == TFTP_BLOCKSIZE; )
    {
        lastread = len;

        /* increment block number, allow overflow */
        block++;

        /* prepare data packet */
        tfp_store_ushort_ns ( buffer, TFTP_OPCODE_DATA );
        tfp_store_ushort_ns ( buffer + 2, block );

        /* send DATA pakcet */
        if ( sendto_autoretry ( sess->sock, buffer, 4 + len, 0, ( struct sockaddr * ) &sess->saddr,
                sizeof ( sess->saddr ) ) < 0 )
        {
            close ( fd );
            sess->exit_flag = 1;
            fprintf ( stderr, "\n[tftp] failed to send data: %i\n", errno );
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
            fprintf ( stderr, "\n[tftp] failed to receive data: %i\n", errno );
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
            fprintf ( stderr, "\n[tftp] expected an ACK packet.\n" );
            return EINVAL;
        }

        /* skip ACK for previous blocks */
        if ( ntohs ( ack.block ) != block )
        {
            fprintf ( stderr, "\n[tftp] ACK: expected block #%u, got #%u - ignored.\n", block,
                ntohs ( ack.block ) );
            goto recv_ack;
        }

        /* show progress */
        printf ( "\r[tftp] progress: sent %lu blocks", ( unsigned long ) ++nblocks );
    }

    /* put new line */
    putchar ( '\n' );

    /* close file fd */
    close ( fd );

    /* check for reading failure */
    if ( ( ssize_t ) len < 0 )
    {
        fprintf ( stderr, "[tftp] failed to read file: %i\n", errno );
        return EINVAL;
    }

    return 0;
}

/* Download file over tftp protocol */
static int tftp_get_file ( struct tftp_sess *sess, const char *path )
{
    int fd;
    unsigned short block = 1;
    size_t nblocks = 0;
    size_t len;
    socklen_t slen;
    const char *params[] = {
        path,
        "octet",
        NULL
    };
    unsigned char buffer[65536];

    /* open file for reading */
    if ( ( fd = open ( path, O_CREAT | O_WRONLY | O_TRUNC, 0644 ) ) < 0 )
    {
        return errno;
    }

    /* prepare tftp packet */
    if ( ( ssize_t ) ( len =
            tftp_prepare_header ( buffer, sizeof ( buffer ), TFTP_OPCODE_RRQ, params ) ) < 0 )
    {
        return errno;
    }

    /* send RRQ packet */
    if ( sendto_autoretry ( sess->sock, buffer, len, 0, ( struct sockaddr * ) &sess->saddr,
            sizeof ( sess->saddr ) ) < 0 )
    {
        close ( fd );
        sess->exit_flag = 1;
        fprintf ( stderr, "[tftp] failed to send data: %i\n", errno );
        return errno;
    }

    printf ( "[tftp] read request sent.\n" );
    printf ( "[tftp] awaiting response ...\n" );

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
            fprintf ( stderr, "\n[tftp] failed to receive data: %i\n", errno );
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
            fprintf ( stderr, "\n[tftp] expected an DATA packet.\n" );
            continue;
        }

        /* validate data block number */
        if ( tfp_load_ushort_ns ( buffer + 2 ) != block )
        {
            fprintf ( stderr, "\n[tftp] DATA: expected block #%u, got #%u - ignored.\n", block,
                tfp_load_ushort_ns ( buffer + 2 ) );
            continue;
        }

        /* write data to file */
        if ( write ( fd, buffer + 4, len - 4 ) < 0 )
        {
            close ( fd );
            fprintf ( stderr, "\n[tftp] failed to write file: %i\n", errno );
            return errno;
        }

        /* send ACK packet with block set to zero */
        if ( tftp_send_ack_packet ( sess, block++ ) < 0 )
        {
            close ( fd );
            return errno;
        }

        /* show progress */
        printf ( "\r[tftp] progress: received %lu blocks", ( unsigned long ) ++nblocks );

    } while ( len == 4 + TFTP_BLOCKSIZE );

    /* put new line */
    putchar ( '\n' );

    /* close file fd */
    close ( fd );

    return 0;
}

/* Perform single tftp operation */
static int tftp_operation ( struct tftp_sess *sess )
{
    size_t len;
    char buffer[4096];
    char command[4096];
    const char *agrument = NULL;
    const char *end;

    /* show prompt prefix */
    printf ( "> " );

    /* read input command */
    if ( ( ssize_t ) ( len = read ( 0, buffer, sizeof ( buffer ) - 1 ) ) < 0 )
    {
        sess->exit_flag = 1;
        fprintf ( stderr, "[tftp] failed to read console input: %i\n", errno );
        return errno;
    }

    /* put string terminator */
    buffer[len] = '\0';

    /* remove trailing new line character */
    if ( len > 0 && buffer[len - 1] == '\n' )
    {
        buffer[len - 1] = '\0';
    }

    /* lookup for space separator */
    if ( ( end = strchr ( buffer, '\x20' ) ) == NULL )
    {
        end = buffer + strlen ( buffer );
    } else
    {
        agrument = end + 1;
    }

    /* place command name into buffer */
    if ( ( len = end - buffer ) >= sizeof ( command ) )
    {
        return ENOBUFS;
    }

    memcpy ( command, buffer, len );
    command[len] = '\0';

    /* branch according to the command */
    if ( !strcmp ( command, "exit" ) || !strcmp ( command, "q" ) )
    {
        sess->exit_flag = 1;
    } else if ( !strcmp ( command, "put" ) )
    {
        return tftp_put_file ( sess, agrument );
    } else if ( !strcmp ( command, "get" ) )
    {
        return tftp_get_file ( sess, agrument );
    } else
    {
        print_help (  );
    }

    return 0;
}

/* Program main function */
int main ( int argc, char *argv[] )
{
    int status;
    unsigned int addr;
    unsigned int port;
    struct tftp_sess sess;
    const char* errmsg;

    setbuf ( stdout, NULL );
    printf ( "[tftp] Little Tftp Client - ver. 1.0.01\n" );

    /* validate arguments count */
    if ( argc < 3 )
    {
        show_usage (  );
        return 1;
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

    /* allocate cleitn socket */
    if ( ( sess.sock = socket ( AF_INET, SOCK_DGRAM, 0 ) ) < 0 )
    {
        fprintf ( stderr, "[tftp] failed to allocate socket: %i\n", errno );
        return 1;
    }

    printf ( "[tftp] socket allocated.\n" );

    /* prepare socket address */
    memset ( &sess.saddr, '\0', sizeof ( sess.saddr ) );
    sess.saddr.sin_family = AF_INET;
    sess.saddr.sin_addr.s_addr = addr;
    sess.saddr.sin_port = htons ( port );

    /* set exit flag to false */
    sess.exit_flag = 0;

    /* set program name */
    sess.progname = "tftp";

    /* perform command from command line if needed */
    if ( argc > 5 && !strcmp ( argv[3], "-c" ) )
    {
        if ( !strcmp ( argv[4], "put" ) )
        {
            status = tftp_put_file ( &sess, argv[5] );

        } else if ( !strcmp ( argv[4], "get" ) )
        {
            status = tftp_get_file ( &sess, argv[5] );

        } else
        {
            show_usage (  );
            return 1;
        }

        if ( status )
        {
            errmsg = strerror ( status );
            fprintf ( stderr, "[tftp] status: failure %i (%s)\n", status, errmsg );
        } else
        {
            fprintf ( stderr, "[tftp] status: success\n" );
        }

        /* close socket */
        close ( sess.sock );
        return 0;
    }

    /* perform tftp operations */
    while ( !sess.exit_flag )
    {
        status = tftp_operation ( &sess );
        if ( status )
        {
            errmsg = strerror ( status );
            fprintf ( stderr, "[tftp] status: failure %i (%s)\n", status, errmsg );
        } else
        {
            fprintf ( stderr, "[tftp] status: success\n" );
        }
    }

    /* close socket */
    close ( sess.sock );

    return 0;
}
