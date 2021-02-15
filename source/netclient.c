/*
 * netclient.h
 *
 * qodem - Qodem Terminal Emulator
 *
 * Written 2003-2021 by Autumn Lamonte ⚧ Trans Liberation Now
 *
 * To the extent possible under law, the author(s) have dedicated all
 * copyright and related and neighboring rights to this software to the
 * public domain worldwide. This software is distributed without any
 * warranty.
 *
 * You should have received a copy of the CC0 Public Domain Dedication along
 * with this software. If not, see
 * <http://creativecommons.org/publicdomain/zero/1.0/>.
 */

/*
 * This contains general-purpose clients for telnet, rlogin and ssh.  Other
 * than a few references to some qodem variables and functions, it is
 * basically standalone and could easily be incorporated in another
 * application.
 *
 * The API is:
 *
 *    net_connect_start(host, port) - provide a raw TCP connection to
 *        host:port and return the socket fd
 *
 *    net_connect_finish(host, port) - complete the protocol setup for the
 *        socket
 *
 *    net_close() - close the TCP port nicely (use shutdown())
 *
 *    net_force_close() - close the TCP port not nicely (use close(), the
 *        other side might see RST)
 *
 *    telnet_read() / rlogin_read() / ssh_read() - same API as read()
 *
 *    telnet_write() / rlogin_write() / ssh_write() - same API as write()
 *
 *    telnet_resize_screen(lines, columns) (and similar for rlogin and ssh) -
 *        send the (new) screen dimensions to the remote side
 *
 * Its intended use is for the calling code to call net_connect_start(), and
 * then poll() or select() on the returned fd.
 *
 * When poll/select indicates bytes are ready for read, call X_read();
 * X_read() will handle all of the protocol initial negotiation and return
 * only the data payload bytes to the caller.  NOTE: It IS possible for
 * poll/select to report ready data but for X_read() to return nothing!  This
 * is due to all of the bytes read()able by the socket being consumed by the
 * telnet/rlogin/ssh protocol itself.  In such cases X_read() returns -1 and
 * sets errno to EAGAIN.  This is similar to but unlike a true socket or pty,
 * which will always have read() return 0 or more if the poll/select
 * indicates readable.
 *
 * Similarly call X_write() if the calling code wants to emit bytes to
 * the remote end.
 *
 * Telnet Notes
 * ------------
 *
 * The telnet options that are negotiated are:
 *     Binary Transmission      RFC 856
 *     Suppress Go-Ahead        RFC 858
 *     Negotiate Window Size    RFC 1073
 *     Terminal Type            RFC 1091
 *     Terminal Speed           RFC 1079
 *     New Environment          RFC 1572
 *     Echo                     RFC 857
 *
 * It also sends the environment variables TERM and LANG.  Not every telnetd
 * understands these variables, so this client breaks the telnet protocol
 * specification and sends the TERM variable as the telnet terminal type.
 * This is incorrect behavior according to RFC 1091: the only terminal types
 * officially supported are those listed in RFC 1010 Assigned Numbers
 * (available at http://tools.ietf.org/rfc/rfc1010.txt).  Of the officially
 * supported terminal types, only the following correspond to any of the
 * qodem terminals: DEC-VT52, DEC-VT100.  The missing ones are ANSI(.SYS),
 * Avatar, VT220, Linux, TTY, and Xterm.  In qodem's defense, netkit-telnet
 * does the same thing, and nearly all of the RFC 1010 terminal types are now
 * defunct.
 *
 * Rlogin Notes
 * ------------
 *
 * Rlogin can only connect to port 513.
 *
 * rlogin_read() has same API as read() EXCEPT that it also has an OOB flag.
 *
 * OOB data must be looked for by the caller (the exceptfds parameter to
 * select()) and if some is present the oob parameter to rlogin_read() must
 * be set.
 *
 * SSH Notes
 * ---------
 *
 *
 */

#include "qcurses.h"

#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#ifdef Q_PDCURSES_WIN32
#  if defined(__BORLANDC__) || defined(_MSC_VER)
#    include <winsock2.h>
#    include <ws2tcpip.h>
#    include <wspiapi.h>
#    define UNLEN 256
#  else
#    ifndef _WIN32_WINNT
#      define _WIN32_WINNT 0x0501
#      include <ws2tcpip.h>
#      include <lmcons.h>
#    endif /* _WIN32_WINNT */
#  endif /* __BORLANDC__ */
#else
#  include <sys/types.h>
#  include <sys/socket.h>
#  include <sys/select.h>
#  include <pwd.h>
#  include <netdb.h>
#endif /* Q_PDCURSES_WIN32 */

#include "common.h"
#include "dialer.h"
#include "qodem.h"
#include "console.h"
#include "forms.h"
#include "states.h"

#ifdef Q_UPNP
#if defined(HAVE_MINIUPNPC_MINIUPNPC_H) && defined(Q_USE_SYSTEM_UPNP)
/*
 * Use the system provided version of miniupnpc.
 */
#include <miniupnpc/miniupnpc.h>
#include <miniupnpc/miniwget.h>
#include <miniupnpc/upnpcommands.h>
#else
/*
 * Use my local version of miniupnpc.
 */
#include "miniupnpc.h"
#include "miniwget.h"
#include "upnpcommands.h"
#endif /* HAVE_MINIUPNPC_MINIUPNPC_H */
#endif /* Q_UPNP */

#include "netclient.h"

/* Set this to a not-NULL value to enable debug log. */
/* static const char * DLOGNAME = "netclient"; */
static const char * DLOGNAME = NULL;

/**
 * Negotiation states that lead through login up to an 8-bit channel.
 */
typedef enum {
    INIT,                       /* Raw connection established */

    SENT_OPTIONS,               /* Sent all the desired telnet options */

    SENT_LOGIN,                 /* Sent the rlogin login data */

    ESTABLISHED                 /* In 8-bit streaming mode */
} STATE;

/**
 * State of the session negotiation.
 */
static STATE state = INIT;

/**
 * If true, a net_connect_start() / net_connect_finish() has completed
 * successfully.
 */
static Q_BOOL connected = Q_FALSE;

/**
 * If true, net_listen() has set up a server socket and it is listening for
 * connections.
 */
static Q_BOOL listening = Q_FALSE;

/**
 * If true, we are in the middle of a connection attempt,
 * i.e. net_connect_start() has happened but not yet net_connect_finish().
 */
static Q_BOOL pending = Q_FALSE;

/**
 * The IP address of the remote side after the connection has been
 * established.
 */
static char remote_host[NI_MAXHOST];

/**
 * The IP port of the remote side after the connection has been established.
 */
static char remote_port[NI_MAXSERV];

/**
 * The IP address of the local side's listening server socket.
 */
static char local_host[NI_MAXHOST];

/**
 * A nice reasable string version of local_host that includes both IP and
 * port.
 */
static char local_host_full[NI_MAXHOST + NI_MAXSERV + 4];

/**
 * The server socket descriptor.
 */
static int listen_fd = -1;

/**
 * The host being connect()'d to via net_connect_start().
 */
static const char * connect_host = NULL;

/**
 * The port being connect()'d to via net_connect_start().
 */
static const char * connect_port = NULL;

/* Raw input buffer */
static unsigned char read_buffer[Q_BUFFER_SIZE];
static int read_buffer_n = 0;

/* Raw output buffer */
static unsigned char write_buffer[Q_BUFFER_SIZE];
static unsigned int write_buffer_n = 0;

/* Forward references needed by net_X() methods */
static void rlogin_send_login(const int fd);
#ifdef Q_SSH_CRYPTLIB
static int ssh_setup_connection(int fd, const char * host, const char * port);
static void ssh_close();
static int ssh_accept(int fd);
#endif

#ifdef Q_UPNP

static Q_BOOL upnp_is_initted = Q_FALSE;

static Q_BOOL upnp_forwarded = Q_FALSE;

static struct UPNPUrls upnp_urls;

static struct IGDdatas upnp_igd_datas;

static char upnp_local_port[NI_MAXSERV];

static char upnp_external_address[NI_MAXHOST];

static char local_host_external_full[NI_MAXHOST + NI_MAXSERV + 4];

#endif

/* Telnet protocol special characters */
#define TELNET_SE               240     /* f0 */
#define TELNET_NOP              241     /* f1 */
#define TELNET_DM               242     /* f2 */
#define TELNET_BRK              243     /* f3 */
#define TELNET_IP               244     /* f4 */
#define TELNET_AO               245     /* f5 */
#define TELNET_AYT              246     /* f6 */
#define TELNET_EC               247     /* f7 */
#define TELNET_EL               248     /* f8 */
#define TELNET_GA               249     /* f9 */
#define TELNET_SB               250     /* fa */
#define TELNET_WILL             251     /* fb */
#define TELNET_WONT             252     /* fc */
#define TELNET_DO               253     /* fd */
#define TELNET_DONT             254     /* fe */
#define TELNET_IAC              255     /* ff */

/* The telnet sub-negotiation data buffer */
#define SUBNEG_BUFFER_MAX 128
static unsigned char subneg_buffer[SUBNEG_BUFFER_MAX];
static int subneg_buffer_n;

/**
 * The telnet protocol state.
 */
struct telnet_state {

    /*
     * Network Virtual Terminal flags
     */
    Q_BOOL echo_mode;
    Q_BOOL binary_mode;
    Q_BOOL go_ahead;
    Q_BOOL do_naws;
    Q_BOOL do_term_type;
    Q_BOOL do_term_speed;
    Q_BOOL do_environment;

    /*
     * telnet_read() flags
     */
    Q_BOOL iac;
    Q_BOOL dowill;
    unsigned char dowill_type;
    Q_BOOL subneg_end;
    Q_BOOL is_eof;
    Q_BOOL eof_msg;
    Q_BOOL read_cr;

    /*
     * telnet_write() flags
     */
    int write_rc;
    int write_last_errno;
    Q_BOOL write_last_error;
    Q_BOOL write_cr;
};

/**
 * The telnet state.
 */
static struct telnet_state nvt;

/**
 * Reset the telnet NVT to default state as per RFC 854.
 */
static void reset_nvt() {
    nvt.echo_mode               = Q_FALSE;
    nvt.binary_mode             = Q_FALSE;
    nvt.go_ahead                = Q_TRUE;
    nvt.do_naws                 = Q_FALSE;
    nvt.do_term_type            = Q_FALSE;
    nvt.do_term_speed           = Q_FALSE;
    nvt.do_environment          = Q_FALSE;

    nvt.iac                     = Q_FALSE;
    nvt.dowill                  = Q_FALSE;
    nvt.subneg_end              = Q_FALSE;
    nvt.is_eof                  = Q_FALSE;
    nvt.eof_msg                 = Q_FALSE;
    nvt.read_cr                 = Q_FALSE;

    nvt.write_rc                = 0;
    nvt.write_last_errno        = 0;
    nvt.write_last_error        = Q_FALSE;
    nvt.write_cr                = Q_FALSE;
}

/* -------------------------------------------------------------------------- */
/* Getters ------------------------------------------------------------------ */
/* -------------------------------------------------------------------------- */

/**
 * Whether or not we are connected.
 *
 * @return if true, q_child_tty_fd is connected to a remote system
 */
Q_BOOL net_is_connected() {
    return connected;
}

/**
 * Whether or not a connect() is pending.
 *
 * @return if true, a connect() call is waiting to complete
 */
Q_BOOL net_connect_pending() {
    return pending;
}

/**
 * Whether or not we are listening for a connection.
 *
 * @return if true, the host mode is listening
 */
Q_BOOL net_is_listening() {
    return listening;
}

/**
 * Get the TCP listener address/port in a human-readable form.  Note that the
 * string returned is a single static buffer, i.e. this is NOT thread-safe.
 *
 * @return a string like "[1.2.3.4]:23"
 */
const char * net_listen_string() {
    return local_host_full;
}

#ifdef Q_UPNP

/**
 * Get the TCP listener address/port for the external gateway interface in
 * human-readable form.  Note that the string returned is a single static
 * buffer, i.e. this is NOT thread-safe.
 *
 * @return a string like "[1.2.3.4]:23"
 */
const char * net_listen_external_string() {
    return local_host_external_full;
}

#endif

/**
 * Return the actual IP address of the remote system.
 *
 * @return a string, or "Unknown" if not connected
 */
char * net_ip_address() {
    if (connected == Q_TRUE) {
        return remote_host;
    }
    return _("Unknown");
}

/**
 * Return the actual port number of the remote system.
 *
 * @return a string, or "Unknown" if not connected
 */
char * net_port() {
    if (connected == Q_TRUE) {
        return remote_port;
    }
    return _("Unknown");
}

/* -------------------------------------------------------------------------- */
/* UPnP --------------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */

#ifdef Q_UPNP

/**
 * Setup the UPnP system so that we can do port forwarding.
 *
 * @return true if successful
 */
static Q_BOOL upnp_init() {
    struct UPNPDev * device_list;
    int rc;

    DLOG(("upnp_init() : upnp_is_initted = %s\n",
            (upnp_is_initted == Q_TRUE ? "true" : "false")));

    if (upnp_is_initted == Q_TRUE) {
        return Q_TRUE;
    }

    memset(&upnp_urls, 0, sizeof(struct UPNPUrls));
    memset(&upnp_igd_datas, 0, sizeof(struct IGDdatas));

    DLOG(("upnp_init() : upnpDiscover()\n"));

#if (MINIUPNPC_API_VERSION == 10) || !defined(MINIUPNPC_API_VERSION)
    /*
     * Version 10 API call:
     *
     * upnpDiscover(int delay, const char * multicastif,
     *              const char * minissdpdsock, int sameport,
     *              int ipv6,
     *              int * error);
     *
     */
    device_list = upnpDiscover(2000, NULL, NULL, 0, 0, NULL);
#else
#  if (MINIUPNPC_API_VERSION == 14)
    /*
     * Version 14
     *
     * upnpDiscover(int delay, const char * multicastif,
     *              const char * minissdpdsock, int sameport,
     *              int ipv6, unsigned char ttl,
     *              int * error);
     *
     */
    device_list = upnpDiscover(2000, NULL, NULL, 0, 0, 2, NULL);
#  else
#    if (MINIUPNPC_API_VERSION == 16)
    /*
     * Version 16
     *
     * upnpDiscover(int delay, const char * multicastif,
     *              const char * minissdpdsock, int sameport,
     *              int ipv6, unsigned char ttl,
     *              int * error);
     *
     */
    device_list = upnpDiscover(2000, NULL, NULL, 0, 0, 2, NULL);
#    else
#      if (MINIUPNPC_API_VERSION == 17)
    /*
     * Version 17
     *
     * upnpDiscover(int delay, const char * multicastif,
     *              const char * minissdpdsock, int localport,
     *              int ipv6, unsigned char ttl,
     *              int * error);
     *
     */
    device_list = upnpDiscover(2000, NULL, NULL, 0, 0, 2, NULL);
#    else
#      error "Unsupported miniupnpc API version.  Consider disabling UPNP (--disable-upnp), or file an issue at https://github.com/klamonte/qodem/issues" .
#      endif
#    endif
#  endif
#endif

    if (device_list != NULL) {

        rc = UPNP_GetValidIGD(device_list, &upnp_urls, &upnp_igd_datas,
                              local_host, sizeof(local_host));

        switch (rc) {

        case 1:
            DLOG(("Found valid IGD : %s\n", upnp_urls.controlURL));
            break;

        case 2:

            DLOG(("Found a (not connected?) IGD : %s\n", upnp_urls.controlURL));
            DLOG(("Trying to continue anyway\n"));
            break;

        case 3:
            DLOG(("UPnP device found. Is it an IGD ? : %s\n",
                    upnp_urls.controlURL));
            DLOG(("Trying to continue anyway\n"));
            break;

        default:
            DLOG(("Found device (igd ?) : %s\n", upnp_urls.controlURL));
            DLOG(("Trying to continue anyway\n"));
            break;
        }

        DLOG(("Local LAN ip address : %s\n", local_host));

        /*
         * Grab the external interface
         */
        rc = UPNP_GetExternalIPAddress(upnp_urls.controlURL,
                                       upnp_igd_datas.first.servicetype,
                                       upnp_external_address);
        if (rc != 0) {
            DLOG(("upnp_init(): failed to discover external IP address: %d\n",
                    rc));
            freeUPNPDevlist(device_list);
            upnp_is_initted = Q_FALSE;
            return Q_FALSE;
        }

        DLOG(("upnp_init(): external address is %s\n", upnp_external_address));
        freeUPNPDevlist(device_list);
        upnp_is_initted = Q_TRUE;
        return Q_TRUE;

    } /* if (device_list != NULL) */

    /*
     * No UPnP devices found
     */
    DLOG(("upnp_init() : no UPnP devices found\n"));
    freeUPNPDevlist(device_list);
    return Q_FALSE;
}

/**
 * Shutdown UPnP system.
 */
static void upnp_teardown() {
    DLOG(("upnp_teardown()\n"));

    if (upnp_forwarded == Q_TRUE) {
        DLOG(("upnp_teardown(): remove port forward for IP %s port %s\n",
                local_host, upnp_local_port));

        UPNP_DeletePortMapping(upnp_urls.controlURL,
                               upnp_igd_datas.first.servicetype,
                               upnp_local_port, "TCP", NULL);
        upnp_forwarded = Q_FALSE;
    }
}

/**
 * Forward a port through a NAT using UPnP.
 *
 * @param fd a listening server socket descriptor
 * @return true if the port was forwarded successfully
 */
static Q_BOOL upnp_forward_port(int fd) {
    struct sockaddr local_sockaddr;
    socklen_t local_sockaddr_length = sizeof(struct sockaddr);
    char my_local_host[NI_MAXHOST];
    int rc;

    DLOG(("upnp_forward_port()\n"));

    /*
     * Because this is a listening socket, my_local_host field will end up
     * pointing to 0.0.0.0.  I still need to call it though to get the
     * upnp_local_port set correctly.
     */
    getsockname(fd, &local_sockaddr, &local_sockaddr_length);
    getnameinfo(&local_sockaddr, local_sockaddr_length,
                my_local_host, sizeof(my_local_host),
                upnp_local_port, sizeof(upnp_local_port),
                NI_NUMERICHOST | NI_NUMERICSERV);

    /*
     * Bring UPnP up as needed
     */
    if (upnp_init() != Q_TRUE) {
        DLOG(("upnp_forward_port(): failed to init UPnP\n"));
        return Q_FALSE;
    }

    DLOG(("upnp_forward_port(): local interface IP %s port %s\n", local_host,
            upnp_local_port));

    rc = UPNP_AddPortMapping(upnp_urls.controlURL,
                             upnp_igd_datas.first.servicetype,
                             upnp_local_port, upnp_local_port, local_host,
                             "qodem", "TCP", NULL, NULL);

    if (rc != 0) {
        DLOG(("upnp_forward_port(): port forward failed: %d\n", rc));
        return Q_FALSE;
    }

    DLOG(("upnp_forward_port(): port forward OK\n"));

    /*
     * Save this string
     */
    sprintf(local_host_external_full, "[%s]:%s", upnp_external_address,
            upnp_local_port);

    /*
     * Port forwarded OK
     */
    upnp_forwarded = Q_TRUE;
    return Q_TRUE;
}

#endif /* Q_UPNP */

/* -------------------------------------------------------------------------- */
/* Winsock ------------------------------------------------------------------ */
/* -------------------------------------------------------------------------- */

#ifdef Q_PDCURSES_WIN32

/**
 * If true, WSAStartup() was called succesfully.
 */
static Q_BOOL wsaStarted = Q_FALSE;

/**
 * Start the Winsock system.
 *
 * @return true if successful
 */
static Q_BOOL start_winsock() {
    int rc;
    char notify_message[DIALOG_MESSAGE_SIZE];
    WSADATA wsaData;

    if (wsaStarted == Q_TRUE) {
        return Q_TRUE;
    }

    /*
     * Ask for Winsock 2.2
     */
    rc = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (rc != 0) {
        snprintf(notify_message, sizeof(notify_message),
                 _("Error calling WSAStartup(): %d (%s)"), rc, strerror(rc));
        notify_form(notify_message, 0);
        return Q_FALSE;
    }

    DLOG(("start_winsock() got version %d.%d\n", HIBYTE(wsaData.wVersion),
            LOBYTE(wsaData.wVersion)));

    /*
     * All OK
     */
    wsaStarted = Q_TRUE;
    return Q_TRUE;
}

/**
 * Shutdown Winsock.
 */
void stop_winsock() {
    /*
     * Ignore the return from WSACleanup()
     */
    WSACleanup();
    wsaStarted = Q_FALSE;
}

/**
 * Thanks to Winsock I need to check for either errno or WSAGetLastError().
 *
 * @return the appropriate error value after a network call
 */
int get_errno() {
    return WSAGetLastError();
}

/**
 * Set the value returned by get_errno().  This is used to make higher-level
 * protocol (telnet/rlogin/ssh) errors mimic the low-level I/O errors,
 * e.g. to be able to return EAGAIN.
 *
 * @param x the new error value
 */
void set_errno(int x) {
    WSASetLastError(x);
}

/**
 * A list of descriptions to match Winsock error codes.
 */
struct winsock_error_text {
        int winsock_errno;
        const char * label;
        const char * description;
};

struct winsock_error_text winsock_errors[] = {
    {
        WSA_E_CANCELLED, "WSA_E_CANCELLED", "Lookup cancelled."
    },
    {
        WSA_E_NO_MORE, "WSA_E_NO_MORE", "No more data available."
    },
    {
        WSAEACCES, "WSAEACCES", "Permission denied."
    },
    {
        WSAEADDRINUSE, "WSAEADDRINUSE", "Address already in use."
    },
    {
        WSAEADDRNOTAVAIL, "WSAEADDRNOTAVAIL", "Cannot assign requested address."
    },
    {
        WSAEAFNOSUPPORT, "WSAEAFNOSUPPORT",
        "Address family not supported by protocol family."
    },
    {
        WSAEALREADY, "WSAEALREADY", "Operation already in progress."
    },
    {
        WSAEBADF, "WSAEBADF", "Bad file number."
    },
    {
        WSAECANCELLED, "WSAECANCELLED", "Operation cancelled."
    },
    {
        WSAECONNABORTED, "WSAECONNABORTED", "Software caused connection abort."
    },
    {
        WSAECONNREFUSED, "WSAECONNREFUSED", "Connection refused."
    },
    {
        WSAECONNRESET, "WSAECONNRESET", "Connection reset by peer."
    },
    {
        WSAEDESTADDRREQ, "WSAEDESTADDRREQ", "Destination address required."
    },
    {
        WSAEDQUOT, "WSAEDQUOT", "Disk quota exceeded."
    },
    {
        WSAEFAULT, "WSAEFAULT", "Bad address."
    },
    {
        WSAEHOSTDOWN, "WSAEHOSTDOWN", "Host is down."
    },
    {
        WSAEHOSTUNREACH, "WSAEHOSTUNREACH", "No route to host."
    },
    {
        WSAEINPROGRESS, "WSAEINPROGRESS", "Operation now in progress."
    },
    {
        WSAEINTR, "WSAEINTR", "Interrupted function call."
    },
    {
        WSAEINVAL, "WSAEINVAL", "Invalid argument."
    },
    {
        WSAEINVALIDPROCTABLE, "WSAEINVALIDPROCTABLE",
        "Invalid procedure table from service provider."
    },
    {
        WSAEINVALIDPROVIDER, "WSAEINVALIDPROVIDER",
        "Invalid service provider version number."
    },
    {
        WSAEISCONN, "WSAEISCONN", "Socket is already connected."
    },
    {
        WSAELOOP, "WSAELOOP", "Too many levels of symbolic links."
    },
    {
        WSAEMFILE, "WSAEMFILE", "Too many open files."
    },
    {
        WSAEMSGSIZE, "WSAEMSGSIZE", "Message too long."
    },
    {
        WSAENAMETOOLONG, "WSAENAMETOOLONG", "File name is too long."
    },
    {
        WSAENETDOWN, "WSAENETDOWN", "Network is down."
    },
    {
        WSAENETRESET, "WSAENETRESET", "Network dropped connection on reset."
    },
    {
        WSAENETUNREACH, "WSAENETUNREACH", "Network is unreachable."
    },
    {
        WSAENOBUFS, "WSAENOBUFS", "No buffer space available."
    },
    {
        WSAENOMORE, "WSAENOMORE", "No more data available."
    },
    {
        WSAENOPROTOOPT, "WSAENOPROTOOPT", "Bad protocol option."
    },
    {
        WSAENOTCONN, "WSAENOTCONN", "Socket is not connected."
    },
    {
        WSAENOTEMPTY, "WSAENOTEMPTY", "Directory is not empty."
    },
    {
        WSAENOTSOCK, "WSAENOTSOCK", "Socket operation on nonsocket."
    },
    {
        WSAEOPNOTSUPP, "WSAEOPNOTSUPP", "Operation not supported."
    },
    {
        WSAEPFNOSUPPORT, "WSAEPFNOSUPPORT", "Protocol family not supported."
    },
    {
        WSAEPROCLIM, "WSAEPROCLIM", "Too many processes."
    },
    {
        WSAEPROTONOSUPPORT, "WSAEPROTONOSUPPORT", "Protocol not supported."
    },
    {
        WSAEPROTOTYPE, "WSAEPROTOTYPE", "Protocol wrong type for socket."
    },
    {
        WSAEPROVIDERFAILEDINIT, "WSAEPROVIDERFAILEDINIT",
        "Unable to initialise a service provider."
    },
    {
        WSAEREFUSED, "WSAEREFUSED", "Refused."
    },
    {
        WSAEREMOTE, "WSAEREMOTE", "Too many levels of remote in path."
    },
    {
        WSAESHUTDOWN, "WSAESHUTDOWN", "Cannot send after socket shutdown."
    },
    {
        WSAESOCKTNOSUPPORT, "WSAESOCKTNOSUPPORT", "Socket type not supported."
    },
    {
        WSAESTALE, "WSAESTALE", "Stale NFS file handle."
    },
    {
        WSAETIMEDOUT, "WSAETIMEDOUT", "Connection timed out."
    },
    {
        WSAETOOMANYREFS, "WSAETOOMANYREFS", "Too many references."
    },
    {
        WSAEUSERS, "WSAEUSERS", "Too many users."
    },
    {
        WSAEWOULDBLOCK, "WSAEWOULDBLOCK", "Resource temporarily unavailable."
    },
    {
        WSANOTINITIALISED, "WSANOTINITIALISED",
        "Successful WSAStartup not yet performed."
    },
    {
        WSASERVICE_NOT_FOUND, "WSASERVICE_NOT_FOUND", "Service not found."
    },
    {
        WSASYSCALLFAILURE, "WSASYSCALLFAILURE", "System call failure."
    },
    {
        WSASYSNOTREADY, "WSASYSNOTREADY", "Network subsystem is unavailable."
    },
    {
        WSATYPE_NOT_FOUND, "WSATYPE_NOT_FOUND", "Class type not found."
    },
    {
        WSAVERNOTSUPPORTED, "WSAVERNOTSUPPORTED",
        "Winsock.dll version out of range."
    },
    {
        WSAEDISCON, "WSAEDISCON", "Graceful shutdown in progress."
    },
    {
        0, "WSAENONE", "No error."
    },
    {
        -1, "Unknown", "Unknown error."
    }
};

/**
 * The message returned by get_strerror().
 */
static char winsock_error_message[DIALOG_MESSAGE_SIZE];

/**
 * Get the error message that goes with get_errno().
 *
 * @param error_number the errno value
 * @return the appropriate error message for a network error value
 */
const char * get_strerror(int error_number) {
    int i = 0;
    while (winsock_errors[i].winsock_errno != -1) {
        if (winsock_errors[i].winsock_errno == error_number) {
            sprintf(winsock_error_message, "%s: %s", winsock_errors[i].label,
                winsock_errors[i].description);
            return winsock_error_message;
        }
        i++;
    }

    /*
     * We fell through to the "Unknown error" case.
     */
    sprintf(winsock_error_message, "Unknown error.");
    return winsock_error_message;
}

#else

/**
 * Thanks to Winsock I need to check for either errno or WSAGetLastError().
 *
 * @return the appropriate error value after a network call
 */
int get_errno() {
    return errno;
}

/**
 * Set the value returned by get_errno().  This is used to make higher-level
 * protocol (telnet/rlogin/ssh) errors mimic the low-level I/O errors,
 * e.g. to be able to return EAGAIN.
 *
 * @param x the new error value
 */
void set_errno(int x) {
    errno = x;
}

/**
 * Get the error message that goes with get_errno().
 *
 * @return the appropriate error message for a network error value
 */
const char * get_strerror(int x) {
    return strerror(x);
}

#endif /* Q_PDCURSES_WIN32 */

/* -------------------------------------------------------------------------- */
/* Network connect/listen --------------------------------------------------- */
/* -------------------------------------------------------------------------- */

/**
 * Connect to a remote system over TCP.  This performs the first part of a
 * non-blocking connect() sequence.  net_connect_pending() will return true
 * between the calls to net_connect_start() and net_connect_finish().
 *
 * @param host the hostname.  This can be either a numeric string or a name
 * for DNS lookup.
 * @param the port, for example "23"
 * @return the descriptor for the socket, or -1 if there was an error
 */
int net_connect_start(const char * host, const char * port) {
    char notify_message[DIALOG_MESSAGE_SIZE];
    int rc;
    int fd = -1;
    struct addrinfo hints;
    struct addrinfo * address;
    struct addrinfo * local_address;
    struct addrinfo * p;
    int i;
    char local_port[NI_MAXSERV];
    char * message[2];
    int local_errno;

    DLOG(("net_connect_start() : %s %s\n", host, port));

    assert(connected == Q_FALSE);

    /*
     * Hang onto these for the call to net_connect_finish()
     */
    connect_host = host;
    connect_port = port;

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
#if defined(Q_PDCURSES_WIN32)
    hints.ai_flags = AI_CANONNAME;
    start_winsock();
#else
#  if defined(__APPLE__)
    hints.ai_flags = AI_CANONNAME;
#  else
    hints.ai_flags = AI_NUMERICSERV | AI_CANONNAME;
#  endif
#endif

    /*
     * Pop up connection notice, since this could take a while...
     */
    snprintf(notify_message, sizeof(notify_message),
             _("Looking up IP address for %s port %s..."), host, port);
    snprintf(q_dialer_modem_message, sizeof(q_dialer_modem_message),
             "%s", notify_message);
    q_screen_dirty = Q_TRUE;
    refresh_handler();

    /*
     * Get the remote IP address
     */
    rc = getaddrinfo(host, port, &hints, &address);

    DLOG(("net_connect_start() : getaddrinfo() rc %d\n", rc));

    if (rc != 0) {
        /*
         * Error resolving name
         */
        snprintf(notify_message, sizeof(notify_message), _("Error: %s"),
#if defined(__BORLANDC__) || defined(_MSC_VER)
                 get_strerror(rc));
#else
                 gai_strerror(rc));
#endif
        snprintf(q_dialer_modem_message, sizeof(q_dialer_modem_message),
                 "%s", notify_message);

        /*
         * We failed to connect, cycle to the next phonebook entry.
         */
        q_dial_state = Q_DIAL_LINE_BUSY;
        time(&q_dialer_cycle_start_time);
        q_screen_dirty = Q_TRUE;
        refresh_handler();
        return -1;
    }

    /*
     * Loop through the results
     */
    for (p = address; p != NULL; p = p->ai_next) {
        DLOG(("net_connect_start() : p %p\n", p));

        fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        DLOG(("net_connect_start() : socket() fd %d\n", fd));

        if (fd == -1) {
            continue;
        }

        if (q_status.dial_method == Q_DIAL_METHOD_RLOGIN) {
            /*
             * Rlogin only: bind to a "privileged" port (between 512 and
             * 1023, inclusive).
             */
            for (i = 1023; i >= 512; i--) {
                snprintf(local_port, sizeof(local_port), "%d", i);
                hints.ai_family = p->ai_family;
                hints.ai_socktype = SOCK_STREAM;
                hints.ai_flags = AI_PASSIVE;
                rc = getaddrinfo(NULL, local_port, &hints, &local_address);
                if (rc != 0) {
                    /*
                     * Can't lookup on this local interface ?
                     */
                    goto try_next_interface;
                }
                rc = bind(fd, local_address->ai_addr,
                    local_address->ai_addrlen);
                freeaddrinfo(local_address);

                if (rc != 0) {
                    /*
                     * Can't bind to this port
                     */
                    continue;
                } else {
                    DLOG(("net_connect_start() : rlogin bound to port %d\n",
                            i));

                    /*
                     * We found a privileged port, break out of the inner
                     * loop.
                     */
                    goto rlogin_bound_ok;
                }
            }

            /*
             * This is rlogin, and we failed to bind to the local privileged
             * port
             */
            message[0] =
                _("Rlogin was unable to bind to a local privileged port.  Consider");
            message[1] =
                _("setting use_external_rlogin=true in qodem configuration file.");
            notify_form_long(message, 0, 2);
            freeaddrinfo(address);

            /*
             * We failed to connect, cycle to the next phonebook entry.
             */
            q_dial_state = Q_DIAL_LINE_BUSY;
            time(&q_dialer_cycle_start_time);
            return -1;
        }

        /*
         * Verify that the socket could be created
         */
        if (fd == -1) {
            snprintf(notify_message, sizeof(notify_message),
                     _("Error: %s"), get_strerror(get_errno()));
            snprintf(q_dialer_modem_message, sizeof(q_dialer_modem_message),
                     "%s", notify_message);
            freeaddrinfo(address);

            /*
             * We failed to connect, cycle to the next phonebook entry.
             */
            q_dial_state = Q_DIAL_LINE_BUSY;
            time(&q_dialer_cycle_start_time);
            q_screen_dirty = Q_TRUE;
            refresh_handler();
            return -1;
        }

rlogin_bound_ok:
        /*
         * Attempt the connection
         */
        snprintf(notify_message, sizeof(notify_message),
                 _("Connecting to %s port %s..."), host, port);
        snprintf(q_dialer_modem_message, sizeof(q_dialer_modem_message),
                 "%s", notify_message);
        q_screen_dirty = Q_TRUE;
        refresh_handler();
        pending = Q_TRUE;

        /*
         * Make fd non-blocking.  Note do this LAST so that net_is_pending()
         * is true inside set_nonblock().
         */
        set_nonblock(fd);
        rc = connect(fd, p->ai_addr, p->ai_addrlen);

        local_errno = get_errno();
        DLOG(("connect() rc %d errno %d\n", rc, local_errno));

#ifdef Q_PDCURSES_WIN32
        if ((rc == -1) && (local_errno != WSAEINPROGRESS) &&
            (local_errno != WSAEWOULDBLOCK)) {
#else
        if ((rc == -1) && (local_errno != EINPROGRESS)) {
#endif
            snprintf(notify_message, sizeof(notify_message),
                     _("Error: %s"), get_strerror(local_errno));
            snprintf(q_dialer_modem_message, sizeof(q_dialer_modem_message),
                     "%s", notify_message);
            freeaddrinfo(address);

            /*
             * We failed to connect, cycle to the next phonebook entry.
             */
            q_dial_state = Q_DIAL_LINE_BUSY;
            time(&q_dialer_cycle_start_time);
            q_screen_dirty = Q_TRUE;
            refresh_handler();
            pending = Q_FALSE;
            return -1;
        }
        break;

try_next_interface:
        ;

    } /* for (p = address; p != NULL; p = p->ai_next) */

    /*
     * At this point, fd is connect()'ing or failed.
     */
    freeaddrinfo(address);
    return fd;
}

/**
 * Complete the connection logic when connecting to a remote system over TCP.
 * If using a layer that has further work such as rlogin or ssh, start that
 * session negotiation.
 *
 * @return true if the connection was established successfully.  If false,
 * the socket has already been closed and q_child_tty_fd is -1.
 */
Q_BOOL net_connect_finish() {
    struct sockaddr remote_sockaddr;
    socklen_t remote_sockaddr_length = sizeof(struct sockaddr);
    int socket_errno;
    socklen_t socket_errno_length = sizeof(socket_errno);
    char notify_message[DIALOG_MESSAGE_SIZE];
    int rc;

#ifdef Q_PDCURSES_WIN32
    rc = getsockopt(q_child_tty_fd, SOL_SOCKET, SO_ERROR,
                    (char *) &socket_errno, &socket_errno_length);
#else
    rc = getsockopt(q_child_tty_fd, SOL_SOCKET, SO_ERROR,
                    &socket_errno, &socket_errno_length);
#endif

    DLOG(("net_connect_finish() : getsockopt() rc %d\n", rc));

    if ((rc < 0) || (socket_errno != 0)) {
        /*
         * Error connecting
         */
        if (rc == 0) {
            /*
             * If getsockopt() worked, report the socket error
             */
            set_errno(socket_errno);
        }

        /*
         * The last connection attempt failed
         */
        snprintf(notify_message, sizeof(notify_message),
                 _("Error: %s"), get_strerror(get_errno()));
        snprintf(q_dialer_modem_message, sizeof(q_dialer_modem_message),
                 "%s", notify_message);
        q_screen_dirty = Q_TRUE;
        refresh_handler();

#ifdef Q_PDCURSES_WIN32
        closesocket(q_child_tty_fd);
#else
        close(q_child_tty_fd);
#endif
        q_child_tty_fd = -1;

        /*
         * We failed to connect, cycle to the next phonebook entry.
         */
        q_dial_state = Q_DIAL_LINE_BUSY;
        time(&q_dialer_cycle_start_time);
        /*
         * Don't call net_connect_finish() again.
         */
        pending = Q_FALSE;
        return Q_FALSE;
    }

    /*
     * We connected ok.
     */
    getpeername(q_child_tty_fd, &remote_sockaddr, &remote_sockaddr_length);
    getnameinfo(&remote_sockaddr, remote_sockaddr_length,
                remote_host, sizeof(remote_host),
                remote_port, sizeof(remote_port),
                NI_NUMERICHOST | NI_NUMERICSERV);

    DLOG(("net_connect_finish() : connected.  Remote host is %s %s\n",
            remote_host, remote_port));

#ifdef Q_SSH_CRYPTLIB

    /*
     * SSH has its session management here
     */
    if (q_status.dial_method == Q_DIAL_METHOD_SSH) {
        rc = ssh_setup_connection(q_child_tty_fd, connect_host, connect_port);
        if (rc == -1) {
            /*
             * There was an error connecting, break out
             */
#ifdef Q_PDCURSES_WIN32
            closesocket(q_child_tty_fd);
#else
            close(q_child_tty_fd);
#endif
            q_child_tty_fd = -1;

            /*
             * We failed to connect, cycle to the next phonebook entry.
             */
            q_dial_state = Q_DIAL_LINE_BUSY;
            time(&q_dialer_cycle_start_time);
            q_screen_dirty = Q_TRUE;
            refresh_handler();

            /*
             * Don't call net_connect_finish() again.
             */
            pending = Q_FALSE;
            return Q_FALSE;
        }
    }
#endif /* Q_SSH_CRYPTLIB */

    /*
     * Reset connection state machine
     */
    state = INIT;
    memset(read_buffer, 0, sizeof(read_buffer));
    read_buffer_n = 0;
    memset(write_buffer, 0, sizeof(write_buffer));
    write_buffer_n = 0;
    reset_nvt();

    /*
     * Drop the connected message on the receive buffer.  We explicitly do
     * CRLF here.
     */
    snprintf((char *) read_buffer, sizeof(read_buffer),
             _("Connected to %s:%s...\r\n"), remote_host, remote_port);
    read_buffer_n = strlen((char *) read_buffer);

    DLOG(("net_connect_finish() : CONNECTED OK\n"));
    connected = Q_TRUE;

    if (q_status.dial_method == Q_DIAL_METHOD_RLOGIN) {
        /*
         * Rlogin special case: immediately send login header.
         */
        rlogin_send_login(q_child_tty_fd);
        state = SENT_LOGIN;
    }

    /*
     * Don't call net_connect_finish() again.  Note this needs to be BEFORE
     * the call to dial_success() so that it will switch program state to
     * console.
     */
    pending = Q_FALSE;

    /*
     * Wrap up the connection logic
     */
    dial_success();

    /*
     * Cheat on the dialer time so we only display the CONNECTED message for
     * 1 second instead of 3.
     */
    q_dialer_cycle_start_time -= 2;

    return Q_TRUE;
}

/**
 * Listen for a remote connection over TCP.
 *
 * @param the port.  This can be a number, NEXT_AVAILABLE_PORT_STRING, or
 * UPNP_PORT_STRING.
 * @return the listening socket descriptor, or -1 if there was an error.
 */
int net_listen(const char * port) {
    char notify_message[DIALOG_MESSAGE_SIZE];
    int rc;
    int fd = -1;
    struct addrinfo hints;
    struct addrinfo * address;
    struct addrinfo * local_address;
    struct addrinfo * p;
    struct sockaddr local_sockaddr;
    socklen_t local_sockaddr_length = sizeof(struct sockaddr);
    char local_port[NI_MAXSERV];
    Q_BOOL find_port_number = Q_FALSE;
    int port_number;

#ifdef Q_UPNP
    Q_BOOL upnp = Q_FALSE;

    /*
     * Try up to three times to find an open port
     */
    int upnp_tries = 3;
#endif

    DLOG(("net_listen() : %s\n", port));

    assert(listening == Q_FALSE);
    assert(connected == Q_FALSE);

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

#ifdef Q_PDCURSES_WIN32
    start_winsock();
#endif

    if (strcmp(port, NEXT_AVAILABLE_PORT_STRING) == 0) {
        /*
         * Find the next available port number.  We'll count down from the
         * top (65535).
         */
        port_number = 65535;
        find_port_number = Q_TRUE;
        sprintf(local_port, "%d", port_number);
        rc = getaddrinfo(NULL, local_port, &hints, &address);

#ifdef Q_UPNP
    } else if (strcmp(port, UPNP_PORT_STRING) == 0) {
        /*
         * Find an available port number from UPnP
         */
        port_number = 65535;
        find_port_number = Q_TRUE;
        upnp = Q_TRUE;
        sprintf(local_port, "%d", port_number);
        rc = getaddrinfo(NULL, local_port, &hints, &address);
#endif

    } else {
        memset(local_port, 0, sizeof(local_port));
        snprintf(local_port, sizeof(local_port) - 1, "%s", port);
        rc = getaddrinfo(NULL, port, &hints, &address);
    }

    if (rc < 0) {
        /*
         * Error resolving port information
         */
        snprintf(notify_message, sizeof(notify_message),
                 _("Error converting port string %s to socket: %s"),
#if defined(__BORLANDC__) || defined(_MSC_VER)
                 port, get_strerror(get_errno()));
#else
                 port, gai_strerror(get_errno()));
#endif
        notify_form(notify_message, 0);
        return -1;
    }

    /*
     * Loop through the results
     */
    rc = 0;
    for (p = address; p != NULL; p = p->ai_next) {
        fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd == -1) {
            continue;
        }

        if (find_port_number == Q_TRUE) {
            for (;;) {
                /*
                 * Pick a random port between 2048 and 65535
                 */
#ifdef Q_PDCURSES_WIN32
                port_number = (rand() % (65535 - 2048)) + 2048;
#else
                port_number = (random() % (65535 - 2048)) + 2048;
#endif

                DLOG(("net_listen() : attempt to bind to port %d\n",
                        port_number));

                snprintf(local_port, sizeof(local_port), "%d", port_number);
                hints.ai_family = p->ai_family;
                hints.ai_socktype = SOCK_STREAM;
                hints.ai_flags = AI_PASSIVE;
                rc = getaddrinfo(NULL, local_port, &hints, &local_address);

                if (rc != 0) {
#if defined(__BORLANDC__) || defined(_MSC_VER)
                    DLOG(("net_listen() : getaddrinfo() error: %d %s\n",
                            get_errno(), get_strerror(get_errno())));
#else
                    DLOG(("net_listen() : getaddrinfo() error: %d %s\n",
                            get_errno(), gai_strerror(get_errno())));
#endif

                    /*
                     * Can't lookup on this local interface ?
                     */
                    freeaddrinfo(local_address);
                    goto try_next_interface;
                }

                rc = bind(fd, local_address->ai_addr,
                    local_address->ai_addrlen);
                freeaddrinfo(local_address);

                if (rc != 0) {
                    DLOG(("net_listen() : bind() error: %d %s\n", get_errno(),
                            get_strerror(get_errno())));

#ifdef Q_PDCURSES_WIN32
                    if (get_errno() == WSAEADDRINUSE) {
#else
                    if (get_errno() == EADDRINUSE) {
#endif
                        /*
                         * Can't bind to this port, look for another one
                         */
                        continue;
                    } else {
                        /*
                         * Another error, bail out of this interface
                         */
                        goto try_next_interface;
                    }
                } else {
                        DLOG(("net_listen() : bound to port %d\n",
                                port_number));

#ifdef Q_UPNP
                    if (upnp == Q_TRUE) {
                        /*
                         * Try to open this port on the NAT remote side.
                         */
                        if (upnp_forward_port(fd) == Q_FALSE) {
                            upnp_tries--;
                            if (upnp_tries == 0) {
                                /*
                                 * Hit max retries
                                 */
                                snprintf(notify_message, sizeof(notify_message),
                                         _("Cannot open a port through UPnP"));
                                notify_form(notify_message, 0);
                                return -1;
                            }

                            /*
                             * See if UPnP is working at all
                             */
                            if (upnp_is_initted == Q_FALSE) {
                                snprintf(notify_message, sizeof(notify_message),
                                         _("Cannot communicate with gateway through UPnP"));
                                notify_form(notify_message, 0);
                                return -1;
                            }

                            /*
                             * Try another port.  We have to close and
                             * re-open the socket though because the bind()
                             * call earlier was successful.
                             */
#ifdef Q_PDCURSES_WIN32
                            closesocket(fd);
#else
                            close(fd);
#endif
                            fd = socket(p->ai_family, p->ai_socktype,
                                        p->ai_protocol);
                            if (fd == -1) {
                                goto try_next_interface;
                            }
                            continue;
                        }

                        DLOG(("net_listen() : UPnP OK\n"));
                    }
#endif /* Q_UPNP */

                    /*
                     * We found an open port, break out of the inner loop.
                     */
                    goto listen_info;
                }
            } /* for (;;) */

        } else {
            /*
             * Try the port they asked for, if it didn't work then tough.
             */
            rc = bind(fd, p->ai_addr, p->ai_addrlen);
        }

        if (rc != 0) {
            /*
             * Can't bind to this port
             */
            continue;
        }

listen_info:

        getsockname(fd, &local_sockaddr, &local_sockaddr_length);
        getnameinfo(&local_sockaddr, local_sockaddr_length,
                    local_host, sizeof(local_host),
                    local_port, sizeof(local_port),
                    NI_NUMERICHOST | NI_NUMERICSERV);

        DLOG(("net_listen() : bound to IP %s port %s\n", local_host,
                local_port));

        goto listen_bound_ok;

try_next_interface:

        DLOG(("net_listen() : try next interface\n"));

        ;
    } /* for (p = address; p != NULL; p = p->ai_next) */

    if (rc < 0) {
        /*
         * The last bind attempt failed
         */
        snprintf(notify_message, sizeof(notify_message),
            _("Error bind()'ing to port %s: %s"), port,
            get_strerror(get_errno()));
        notify_form(notify_message, 0);
        return -1;
    }

listen_bound_ok:

    /*
     * At this point, fd is bound.
     */
    freeaddrinfo(address);

    /*
     * Now make fd listen()
     */
    rc = listen(fd, 5);
    if (rc < 0) {
        /*
         * The last bind attempt failed
         */
        snprintf(notify_message, sizeof(notify_message),
            _("Error listen()'ing on port %s: %s"), local_port,
            get_strerror(get_errno()));
        notify_form(notify_message, 0);
        return -1;
    }

    /*
     * Save this string
     */
    sprintf(local_host_full, "[%s]:%s", local_host, local_port);

    DLOG(("net_listen() : return fd = %d\n", fd));
    listening = Q_TRUE;
    listen_fd = fd;

    /*
     * Note that fd comes out of this as a blocking socket.  On Linux, the
     * accept()'d child socket will always be blocking too, but on Windows
     * the accept()'d socket inherits the behavior of the listen()'ing
     * socket, hence if we set non-blocking now then we will need to set
     * blocking before cryptlib can do anything with it.  The better solution
     * is just to leave it blocking all the way through, and set the
     * accept()'d socket to non-blocking only at the end of net_accept() and
     * only for non-ssh sockets.
     */
    return fd;
}

/**
 * See if we have a new connection.
 *
 * @return true if a new connection is available.
 */
static Q_BOOL has_connection() {
    /*
     * select() on listen_fd for read.  If it comes back, then a connection
     * is ready to be accept()'ed.
     */

    char notify_message[DIALOG_MESSAGE_SIZE];
    fd_set readfds;
    fd_set writefds;
    fd_set exceptfds;
    int select_fd_max;
    struct timeval listen_timeout;
    int rc;

    /* Initialize select() structures */
    FD_ZERO(&readfds);
    FD_ZERO(&writefds);
    FD_ZERO(&exceptfds);

    /* Add listen_fd */
    select_fd_max = listen_fd + 1;
    FD_SET(listen_fd, &readfds);

    /* Perform a polling check */
    listen_timeout.tv_sec = 0;
    listen_timeout.tv_usec = 0;

    rc = select(select_fd_max, &readfds, &writefds, &exceptfds,
                &listen_timeout);

    if (rc > 0) {
        /*
         * There is only one event to look for, so this means a connection is
         * here.
         */
        return Q_TRUE;
    }

    if (rc < 0) {
        snprintf(notify_message, sizeof(notify_message),
                 _("Error in select() on listening socket: %s"),
                 get_strerror(get_errno()));
        notify_form(notify_message, 3.0);
        return Q_FALSE;
    }

    /*
     * This is a timeout, do nothing.
     */
    return Q_FALSE;
}

/**
 * See if we have a new connection.
 *
 * @return the accepted socket descriptor, or -1 if no new connection is
 * available.
 */
int net_accept() {
    char notify_message[DIALOG_MESSAGE_SIZE];
    int fd;
#ifdef Q_PDCURSES_WIN32
    /*
     * Microsoft decided that sockaddr's would only support IPv4, despite
     * being able to see other sane systems that use a single sockaddr that
     * is big enough for both IPv4 and IPv6.  They introduced
     * SOCKADDR_STORAGE in Windows Vista (!) meaning that IPv4/v6-agnostic
     * code can only be written for Vista and above, despite XP having IPv6
     * support.  Soo...  since I would like to support Win2k and XP, let's
     * ASSUME that enough space for 4x sockaddr will be sufficient.
     */
    struct sockaddr remote_sockaddr[4];
    struct sockaddr local_sockaddr[4];
#else
    struct sockaddr remote_sockaddr;
    struct sockaddr local_sockaddr;
#endif
    socklen_t remote_sockaddr_length;
    socklen_t local_sockaddr_length;
    char local_port[NI_MAXSERV];
    int local_errno;

    if (has_connection() == Q_FALSE) {
        return -1;
    }

    remote_sockaddr_length = sizeof(remote_sockaddr);
    local_sockaddr_length = sizeof(local_sockaddr);

#ifdef Q_PDCURSES_WIN32
    memset(&remote_sockaddr[0], 0, remote_sockaddr_length);
    memset(&local_sockaddr[0], 0, local_sockaddr_length);
    fd = accept(listen_fd, &remote_sockaddr[0], &remote_sockaddr_length);
#else
    memset(&remote_sockaddr, 0, remote_sockaddr_length);
    memset(&local_sockaddr, 0, local_sockaddr_length);
    fd = accept(listen_fd, &remote_sockaddr, &remote_sockaddr_length);
#endif

    local_errno = get_errno();
    if (fd < 0) {

        if ((local_errno == EAGAIN) ||
#ifdef Q_PDCURSES_WIN32
            (local_errno == WSAEWOULDBLOCK)
#else
            (local_errno == EWOULDBLOCK)
#endif
        ) {
            /*
             * No one is there, return cleanly
             */
            return -1;
        }

        /*
         * The last bind attempt failed
         */
        snprintf(notify_message, sizeof(notify_message),
                 _("Error in accept(): %s"), get_strerror(local_errno));
        notify_form(notify_message, 3.0);
        return -1;
    }

    /*
     * We connected ok.
     */
#ifdef Q_PDCURSES_WIN32
    getpeername(fd, &remote_sockaddr[0], &remote_sockaddr_length);
    getnameinfo(&remote_sockaddr[0], remote_sockaddr_length,
                remote_host, sizeof(remote_host),
                remote_port, sizeof(remote_port),
                NI_NUMERICHOST | NI_NUMERICSERV);

    getsockname(fd, &local_sockaddr[0], &local_sockaddr_length);
    getnameinfo(&local_sockaddr[0], local_sockaddr_length,
                local_host, sizeof(local_host),
                local_port, sizeof(local_port),
                NI_NUMERICHOST | NI_NUMERICSERV);
#else
    getpeername(fd, &remote_sockaddr, &remote_sockaddr_length);
    getnameinfo(&remote_sockaddr, remote_sockaddr_length,
                remote_host, sizeof(remote_host),
                remote_port, sizeof(remote_port),
                NI_NUMERICHOST | NI_NUMERICSERV);

    getsockname(fd, &local_sockaddr, &local_sockaddr_length);
    getnameinfo(&local_sockaddr, local_sockaddr_length,
                local_host, sizeof(local_host),
                local_port, sizeof(local_port),
                NI_NUMERICHOST | NI_NUMERICSERV);
#endif

    DLOG(("net_accept() : connected.\n"));
    DLOG(("             Remote host is %s %s\n", remote_host, remote_port));
    DLOG(("             Local host is  %s %s\n", local_host, local_port));

    connected = Q_TRUE;

    /*
     * Reset connection state machine
     */
    state = INIT;
    memset(read_buffer, 0, sizeof(read_buffer));
    read_buffer_n = 0;
    memset(write_buffer, 0, sizeof(write_buffer));
    write_buffer_n = 0;
    reset_nvt();

#ifdef Q_SSH_CRYPTLIB
    /* SSH has its session management here */
    if (q_host_type == Q_HOST_TYPE_SSHD) {
        return ssh_accept(fd);
    } else {
        /*
         * Make fd non-blocking.  Note do this LAST so that
         * net_is_connected() is true inside set_nonblock().
         */
        set_nonblock(fd);
    }
#else
    /*
     * Make fd non-blocking.  Note do this LAST so that net_is_connected() is
     * true inside set_nonblock().
     */
    set_nonblock(fd);
#endif

    DLOG(("net_accept() : return fd = %d\n", fd));
    return fd;
}

/**
 * Close the TCP connection nicely.
 */
void net_close() {

    DLOG(("net_close()\n"));

    if (connected == Q_FALSE) {
        return;
    }

    assert(q_child_tty_fd != -1);

#ifdef Q_SSH_CRYPTLIB
    /*
     * SSH needs to destroy the crypto session.
     */
    if (q_status.dial_method == Q_DIAL_METHOD_SSH) {
        ssh_close();
    }
#endif /* Q_SSH_CRYPTLIB */

    DLOG(("net_close() : shutdown(q_child_tty_fd, SHUT_WR)\n"));

    /*
     * All we do is shutdown().  read() will return 0 when the remote side
     * close()s.
     */
#ifdef Q_PDCURSES_WIN32
    shutdown(q_child_tty_fd, SD_SEND);
#else
    shutdown(q_child_tty_fd, SHUT_WR);
#endif

}

/**
 * Close the TCP connection not nicely.
 */
void net_force_close() {

    DLOG(("net_force_close()\n"));

    if (connected == Q_FALSE) {
        return;
    }
    connected = Q_FALSE;

    if (q_child_tty_fd == -1) {
        return;
    }

#ifdef Q_SSH_CRYPTLIB
    /*
     * SSH needs to destroy the crypto session.
     */
    if (q_status.dial_method == Q_DIAL_METHOD_SSH) {
        ssh_close();
    }
#endif /* Q_SSH_CRYPTLIB */

    DLOG(("net_force_close() : close(q_child_tty_fd)\n"));

    /*
     * We close().  The other side might see a RST.
     */
#ifdef Q_PDCURSES_WIN32
    closesocket(q_child_tty_fd);
#else
    close(q_child_tty_fd);
#endif
    q_child_tty_fd = -1;

}

/**
 * Close TCP listener socket.
 */
void net_listen_close() {

    DLOG(("net_listen_close()\n"));

    if (listening == Q_FALSE) {
        return;
    }

    assert(listen_fd != -1);
    DLOG(("net_listen_close() : close(listen_fd)\n"));

#ifdef Q_PDCURSES_WIN32
    closesocket(listen_fd);
#else
    close(listen_fd);
#endif

    listening = Q_FALSE;

#ifdef Q_UPNP
    if (upnp_is_initted == Q_TRUE) {
        upnp_teardown();
    }
#endif
}

/* -------------------------------------------------------------------------- */
/* RAW write ---------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */

/**
 * Write data from a buffer to the remote system.  This performs a busy wait
 * loop until all of the bytes are written!  It is only used for telnet
 * negotiation to get an 8-bit clean channel.
 *
 * @param fd the socket descriptor
 * @param buf the buffer to read from
 * @param count the number of bytes to write to the remote side
 * @return the number of bytes written
 */
static ssize_t raw_write(const int fd, void * buf, size_t count) {
    int count_original = count;
    int rc;
    do {
        rc = send(fd, (const char *) buf, count, 0);

        if (rc <= 0) {
            DLOG(("raw_write() : error on write(): %s\n",
                    get_strerror(get_errno())));

            switch (get_errno()) {
            case EAGAIN:
                /*
                 * Keep trying, this is a busy spin loop
                 */
                continue;
            default:
                /*
                 * Unknown, bail out
                 */
                return rc;
            }
        } else {
            int i;
            DLOG(("raw_write() : sent %d bytes: ", rc));
            for (i = 0; i < rc; i++) {
                DLOG2(("%02x ", ((char *) buf)[i]));
            }
            DLOG2(("\n"));
            DLOG(("              "));
            for (i = 0; i < rc; i++) {
                DLOG2(("%c  ", ((char *) buf)[i]));
            }
            DLOG2(("\n"));

            count -= rc;
        }
    } while (count > 0);

    /*
     * Everything got pushed out successfully
     */
    return count_original;
}

/* -------------------------------------------------------------------------- */
/* TELNET read/write -------------------------------------------------------- */
/* -------------------------------------------------------------------------- */

/**
 * For debugging, return a descriptive string for this telnet option.  These
 * are pulled from: http://www.iana.org/assignments/telnet-options
 *
 * @param option the telnet numeric option
 * @return the descriptive string
 */
static const char * telnet_option_string(const unsigned char option) {
    switch (option) {
    case 0:
        return "Binary Transmission";
    case 1:
        return "Echo";
    case 2:
        return "Reconnection";
    case 3:
        return "Suppress Go Ahead";
    case 4:
        return "Approx Message Size Negotiation";
    case 5:
        return "Status";
    case 6:
        return "Timing Mark";
    case 7:
        return "Remote Controlled Trans and Echo";
    case 8:
        return "Output Line Width";
    case 9:
        return "Output Page Size";
    case 10:
        return "Output Carriage-Return Disposition";
    case 11:
        return "Output Horizontal Tab Stops";
    case 12:
        return "Output Horizontal Tab Disposition";
    case 13:
        return "Output Formfeed Disposition";
    case 14:
        return "Output Vertical Tabstops";
    case 15:
        return "Output Vertical Tab Disposition";
    case 16:
        return "Output Linefeed Disposition";
    case 17:
        return "Extended ASCII";
    case 18:
        return "Logout";
    case 19:
        return "Byte Macro";
    case 20:
        return "Data Entry Terminal";
    case 21:
        return "SUPDUP";
    case 22:
        return "SUPDUP Output";
    case 23:
        return "Send Location";
    case 24:
        return "Terminal Type";
    case 25:
        return "End of Record";
    case 26:
        return "TACACS User Identification";
    case 27:
        return "Output Marking";
    case 28:
        return "Terminal Location Number";
    case 29:
        return "Telnet 3270 Regime";
    case 30:
        return "X.3 PAD";
    case 31:
        return "Negotiate About Window Size";
    case 32:
        return "Terminal Speed";
    case 33:
        return "Remote Flow Control";
    case 34:
        return "Linemode";
    case 35:
        return "X Display Location";
    case 36:
        return "Environment Option";
    case 37:
        return "Authentication Option";
    case 38:
        return "Encryption Option";
    case 39:
        return "New Environment Option";
    case 40:
        return "TN3270E";
    case 41:
        return "XAUTH";
    case 42:
        return "CHARSET";
    case 43:
        return "Telnet Remote Serial Port (RSP)";
    case 44:
        return "Com Port Control Option";
    case 45:
        return "Telnet Suppress Local Echo";
    case 46:
        return "Telnet Start TLS";
    case 47:
        return "KERMIT";
    case 48:
        return "SEND-URL";
    case 49:
        return "FORWARD_X";
    case 138:
        return "TELOPT PRAGMA LOGON";
    case 139:
        return "TELOPT SSPI LOGON";
    case 140:
        return "TELOPT PRAGMA HEARTBEAT";
    case 255:
        return "Extended-Options-List";
    default:
        if ((option >= 50) && (option <= 137)) {
            return "Unassigned";
        }
        return "UNKNOWN - OTHER";
    }

}

/**
 * See if the telnet session is in ASCII mode.
 *
 * @return if true, the session is in ASCII mode
 */
Q_BOOL telnet_is_ascii() {
    if (net_is_connected() == Q_FALSE) {
        return Q_FALSE;
    }
    if (q_status.dial_method != Q_DIAL_METHOD_TELNET) {
        return Q_FALSE;
    }
    if (nvt.binary_mode == Q_TRUE) {
        return Q_FALSE;
    }
    return Q_TRUE;
}

/**
 * Send a DO/DON'T/WILL/WON'T response to the remote side.  Response is one
 * of TELNET_DO, TELNET_DON'T, TELNET_WILL, or TELNET_WON'T.  Option is the
 * telnet option in question.
 *
 * @param fd the socket descriptor
 * @param the response byte
 * @param option the telnet option number
 */
static void telnet_respond(const int fd, unsigned char response,
                           unsigned char option) {
    int n;
    unsigned char buffer[3];
    buffer[0] = TELNET_IAC;
    buffer[1] = response;
    buffer[2] = option;
    n = 3;

    DLOG(("telnet_respond() : "));
    switch (response) {
    case TELNET_DO:
        DLOG2(("DO %s\n", telnet_option_string(option)));
        break;
    case TELNET_DONT:
        DLOG2(("DONT %s\n", telnet_option_string(option)));
        break;
    case TELNET_WILL:
        DLOG2(("WILL %s\n", telnet_option_string(option)));
        break;
    case TELNET_WONT:
        DLOG2(("WONT %s\n", telnet_option_string(option)));
        break;
    }
    DLOG2(("\n"));

    raw_write(fd, buffer, n);
}

/**
 * Tell the remote side we WILL support an option.
 *
 * @param fd the socket descriptor
 * @param option the telnet option number
 */
static void telnet_will(const int fd, unsigned char option) {
    telnet_respond(fd, TELNET_WILL, option);
}

/**
 * Tell the remote side we WON'T support an option.
 *
 * @param fd the socket descriptor
 * @param option the telnet option number
 */
static void telnet_wont(const int fd, unsigned char option) {
    telnet_respond(fd, TELNET_WONT, option);
}

/**
 * Tell the remote side we DO support an option.
 *
 * @param fd the socket descriptor
 * @param option the telnet option number
 */
static void telnet_do(const int fd, unsigned char option) {
    telnet_respond(fd, TELNET_DO, option);
}

/**
 * Tell the remote side we DON'T support an option.
 *
 * @param fd the socket descriptor
 * @param option the telnet option number
 */
static void telnet_dont(const int fd, unsigned char option) {
    telnet_respond(fd, TELNET_DONT, option);
}

/**
 * Tell the remote side we WON't or DON'T support an option.
 *
 * @param the remote query byte (WILL/DO)
 * @param fd the socket descriptor
 * @param option the telnet option number
 */
static void telnet_refuse(unsigned char remote_query, const int fd,
                          unsigned char option) {

    if (remote_query == TELNET_DO) {
        telnet_wont(fd, option);
    } else {
        telnet_dont(fd, option);
    }
}

/**
 * Telnet option: Terminal Speed
 *
 * @param fd the socket descriptor
 * @param option the telnet option number
 * @param response the option data bytes (subneg payload)
 * @param response_n the number of bytes in response
 */
static void telnet_send_subneg_response(const int fd,
                                        const unsigned char option,
                                        const unsigned char * response,
                                        const int response_n) {

    unsigned char buffer[SUBNEG_BUFFER_MAX + 5];
    assert(response_n <= SUBNEG_BUFFER_MAX);
    buffer[0] = TELNET_IAC;
    buffer[1] = TELNET_SB;
    buffer[2] = option;
    memcpy(buffer + 3, response, response_n);
    buffer[response_n + 3] = TELNET_IAC;
    buffer[response_n + 4] = TELNET_SE;
    raw_write(fd, buffer, response_n + 5);
}

/**
 * Telnet option: Terminal Speed
 *
 * @param fd the socket descriptor
 */
static void telnet_send_terminal_speed(const int fd) {
    char * response = "\0" "38400,38400";
    telnet_send_subneg_response(fd, 32, (unsigned char *) response, 12);
}

/**
 * Telnet option: Terminal Type
 *
 * @param fd the socket descriptor
 */
static void telnet_send_terminal_type(const int fd) {
    char response[SUBNEG_BUFFER_MAX];
    int response_n = 0;

    /*
     * "IS"
     */
    response[response_n] = 0;
    response_n++;

    /*
     * TERM
     */
    snprintf(response + response_n, sizeof(response) - response_n, "%s",
             emulation_term(q_status.emulation));
    response_n += strlen(emulation_term(q_status.emulation));

    telnet_send_subneg_response(fd, 24, (unsigned char *) response, response_n);
}

/**
 * Telnet option: New Environment.  We send:
 *     TERM
 *     LANG
 *
 * @param fd the socket descriptor
 */
static void telnet_send_environment(const int fd) {
    char response[SUBNEG_BUFFER_MAX];
    int response_n = 0;

    /*
     * "IS"
     */
    response[response_n] = 0;
    response_n++;

    /*
     * TERM
     */
    response[response_n] = 0;   /* "VAR" */
    response_n++;
    snprintf(response + response_n, sizeof(response) - response_n, "TERM");
    response_n += 4;
    response[response_n] = 1;   /* "VALUE" */
    response_n++;
    snprintf(response + response_n, sizeof(response) - response_n,
             "%s", emulation_term(q_status.emulation));
    response_n += strlen(emulation_term(q_status.emulation));

    /*
     * LANG
     */
    response[response_n] = 0;   /* "VAR" */
    response_n++;
#if 1
    /* Normal case: try to pass LANG even though no one will listen to it. */
    snprintf(response + response_n, sizeof(response) - response_n, "LANG");
    response_n += 4;
#else
    /*
     * Testing case: pass the LANG data through a variable that
     * netkit-telnetd will actually honor.  As of netkit-0.17 those variables
     * are:
     *
     *   TERM
     *   DISPLAY
     *   USER
     *   LOGNAME
     *   POSIXLY_CORRECT
     *
     */
    snprintf(response + response_n, sizeof(response) - response_n, "POSIXLY_CORRECT");
    response_n += 15;
#endif
    response[response_n] = 1;   /* "VALUE" */
    response_n++;
    snprintf(response + response_n, sizeof(response) - response_n,
             "%s", emulation_lang(q_status.emulation));
    response_n += strlen(emulation_lang(q_status.emulation));

    telnet_send_subneg_response(fd, 39, (unsigned char *) response, response_n);
}

/**
 * Send the options we want to negotiate on.  For all links:
 *     Binary Transmission
 *     Suppress Go Ahead
 *
 * When run as a server:
 *     Echo
 *
 * When run as a client:
 *     Negotiate About Window Size
 *     Terminal Type
 *     New Environment
 *
 * @param fd the socket descriptor
 */
static void telnet_send_options(const int fd) {
    if (nvt.binary_mode == Q_FALSE) {
        /*
         * Binary Transmission: must ask both do and will
         */
        telnet_do(fd, 0);
        telnet_will(fd, 0);
    }

    if (nvt.go_ahead == Q_TRUE) {
        /*
         * Suppress Go Ahead
         */
        telnet_do(fd, 3);
        telnet_will(fd, 3);
    }

    if (q_program_state == Q_STATE_HOST) {
        /*
         * Enable Echo - I echo to them, they do not echo back to me.
         */
        telnet_dont(fd, 1);
        telnet_will(fd, 1);
        return;
    }

    /*
     * Client only options
     */
    if (nvt.do_naws == Q_FALSE) {
        /*
         * NAWS - we need to use WILL instead of DO
         */
        telnet_will(fd, 31);
    }

    if (nvt.do_term_type == Q_FALSE) {
        /*
         * Terminal Type - we need to use WILL instead of DO
         */
        telnet_will(fd, 24);
    }

    if (nvt.do_environment == Q_FALSE) {
        /*
         * New Environment - we need to use WILL instead of DO
         */
        telnet_will(fd, 39);
    }
}

/**
 * Send our window size to the remote side.
 *
 * @param fd the socket descriptor
 * @param lines the number of rows (height) on the terminal
 * @param columns the number of columns (width) on the terminal
 */
static void telnet_send_naws(const int fd, const int lines, const int columns) {
    unsigned char buffer[16];
    unsigned int n = 0;
    buffer[n] = TELNET_IAC;
    n++;
    buffer[n] = TELNET_SB;
    n++;
    buffer[n] = 31;
    n++;
    buffer[n] = columns / 256;
    n++;
    if (buffer[n - 1] == TELNET_IAC) {
        buffer[n] = TELNET_IAC;
        n++;
    }
    buffer[n] = columns % 256;
    n++;
    if (buffer[n - 1] == TELNET_IAC) {
        buffer[n] = TELNET_IAC;
        n++;
    }

    buffer[n] = lines / 256;
    n++;
    if (buffer[n - 1] == TELNET_IAC) {
        buffer[n] = TELNET_IAC;
        n++;
    }
    buffer[n] = lines % 256;
    n++;
    if (buffer[n - 1] == TELNET_IAC) {
        buffer[n] = TELNET_IAC;
        n++;
    }
    buffer[n] = TELNET_IAC;
    n++;
    buffer[n] = TELNET_SE;
    n++;
    raw_write(fd, buffer, n);
}

/**
 * Send new screen dimensions to the remote side.  This uses the Negotiate
 * About Window Size telnet option (RFC 1073).
 *
 * @param lines the number of screen rows
 * @param columns the number of screen columns
 */
void telnet_resize_screen(const int lines, const int columns) {

    if (connected == Q_FALSE) {
        return;
    }

    if (nvt.do_naws == Q_FALSE) {
        /*
         * We can't do this because the server refuses to handle it
         */
        return;
    }

    /*
     * Send the new dimensions
     */
    assert(q_child_tty_fd != -1);

    telnet_send_naws(q_child_tty_fd, lines, columns);
}

/**
 * Handle an option "sub-negotiation".
 *
 * @param fd the socket descriptor
 */
static void handle_subneg(const int fd) {
    unsigned char option;

    /*
     * Sanity check: there must be at least 1 byte in subneg_buffer
     */
    if (subneg_buffer_n < 1) {
        DLOG(("handle_subneg() : BUFFER TOO SMALL!  The other side is a broken telnetd, it did not send the right sub-negotiation data.\n"));
        return;
    }
    option = subneg_buffer[0];

    DLOG(("handle_subneg() : %s\n", telnet_option_string(option)));

    switch (option) {
    case 24:
        /*
         * Terminal Type
         */
        if ((subneg_buffer_n > 1) && (subneg_buffer[1] == 1)) {
            /*
             * Server sent "SEND", we say "IS"
             */
            telnet_send_terminal_type(fd);
        }
        break;
    case 32:
        /*
         * Terminal Speed
         */
        if ((subneg_buffer_n > 1) && (subneg_buffer[1] == 1)) {
            /*
             * Server sent "SEND", we say "IS"
             */
            telnet_send_terminal_speed(fd);
        }
        break;
    case 39:
        /*
         * New Environment Option
         */
        if ((subneg_buffer_n > 1) && (subneg_buffer[1] == 1)) {
            /*
             * Server sent "SEND", we send the environment (ignoring any
             * specific variables it asked for).
             */
            telnet_send_environment(fd);
        }
        break;
    }

}

/**
 * Read data from remote system to a buffer, via an 8-bit clean channel
 * through the telnet protocol.
 *
 * @param fd the socket descriptor
 * @param buf the buffer to write to
 * @param count the number of bytes requested
 * @return the number of bytes read into buf
 */
ssize_t telnet_read(const int fd, void * buf, size_t count) {
    unsigned char ch;
    int i;
    int rc;
    int total = 0;
    size_t max_read;

    DLOG(("telnet_read() : %d bytes in read_buffer:\n", read_buffer_n));
    for (i = 0; i < read_buffer_n; i++) {
        if ((read_buffer[i] & 0xFF) >= 0x80) {
            DLOG2((" %02x", (read_buffer[i] & 0xFF)));
        } else {
            DLOG2((" %c ", (read_buffer[i] & 0xFF)));
        }
    }
    DLOG2(("\n"));

    if (state == INIT) {
        /*
         * Start the telnet protocol negotiation.
         */
        telnet_send_options(fd);
        state = SENT_OPTIONS;
    }

    /*
     * Perform the raw read
     */
    if (count == 0) {
        /*
         * NOP
         */
        return 0;
    }

    if (nvt.is_eof == Q_TRUE) {
        DLOG(("telnet_read() : no read because EOF\n"));
        /*
         * Do nothing
         */

    } else {

        max_read = sizeof(read_buffer) - read_buffer_n;
        if (max_read > count) {
            max_read = count;
        }

        /*
         * Read some data from the other end
         */
        rc = recv(fd, (char *) read_buffer + read_buffer_n, max_read, 0);

        DLOG(("telnet_read() : read %d bytes:\n", rc));
        for (i = 0; i < rc; i++) {
            DLOG2((" %02x", (read_buffer[read_buffer_n + i] & 0xFF)));
        }
        DLOG2(("\n"));
        for (i = 0; i < rc; i++) {
            if ((read_buffer[read_buffer_n + i] & 0xFF) >= 0x80) {
                DLOG2((" %02x", (read_buffer[read_buffer_n + i] & 0xFF)));
            } else {
                DLOG2((" %c ", (read_buffer[read_buffer_n + i] & 0xFF)));
            }
        }
        DLOG2(("\n"));

        /*
         * Check for EOF or error
         */
        if (rc < 0) {
            if (read_buffer_n == 0) {
                /*
                 * Something bad happened, just return it
                 */
                return rc;
            }
        } else if (rc == 0) {
            /*
             * EOF - Drop a connection close message
             */
            nvt.is_eof = Q_TRUE;
        } else {
            /*
             * More data came in
             */
            read_buffer_n += rc;
        }
    } /* if (nvt.is_eof == Q_TRUE) */

    if ((read_buffer_n == 0) && (nvt.eof_msg == Q_TRUE)) {
        /*
         * We are done, return the final EOF and do not permit further reads.
         */
        connected = Q_FALSE;
        return 0;
    }

    if ((read_buffer_n == 0) && (nvt.is_eof == Q_TRUE)) {
        /*
         * EOF - Drop "Connection closed."
         */
        if (q_program_state != Q_STATE_HOST) {
            snprintf((char *) read_buffer, sizeof(read_buffer), "%s",
                     _("Connection closed.\r\n"));
            read_buffer_n = strlen((char *) read_buffer);
        }
        nvt.eof_msg = Q_TRUE;
    }

    /*
     * Loop through the read bytes
     */
    for (i = 0; i < read_buffer_n; i++) {
        ch = read_buffer[i];

        /*
         DLOG(("  ch: %d \\%03o 0x%02x '%c'\n", ch, ch, ch, ch));
         */

        if (nvt.subneg_end == Q_TRUE) {
            /*
             * Looking for IAC SE to end this subnegotiation
             */
            if (ch == TELNET_SE) {
                if (nvt.iac == Q_TRUE) {
                    /*
                     * IAC SE
                     */
                    DLOG((" <--> End Subnegotiation <-->\n"));
                    nvt.iac = Q_FALSE;
                    nvt.subneg_end = Q_FALSE;
                    handle_subneg(fd);
                }
            } else if (ch == TELNET_IAC) {
                if (nvt.iac == Q_TRUE) {
                    DLOG((" - IAC within subneg -\n"));
                    /*
                     * An argument to the subnegotiation option
                     */
                    subneg_buffer[subneg_buffer_n] = TELNET_IAC;
                    subneg_buffer_n++;
                } else {
                    nvt.iac = Q_TRUE;
                }
            } else {
                /*
                 * An argument to the subnegotiation option
                 */
                subneg_buffer[subneg_buffer_n] = ch;
                subneg_buffer_n++;
            }
            continue;
        }

        /*
         * Look for DO/DON'T/WILL/WON'T option
         */
        if (nvt.dowill == Q_TRUE) {
            DLOG(("   OPTION: %s\n", telnet_option_string(ch)));

            /*
             * Look for option
             */
            switch (ch) {

            case 0:
                if (nvt.dowill_type == TELNET_WILL) {
                    DLOG(("** BINARY TRANSMISSION ON (we initiated) **\n"));
                    nvt.binary_mode = Q_TRUE;
                } else if (nvt.dowill_type == TELNET_DO) {
                    telnet_will(fd, ch);
                    DLOG(("** BINARY TRANSMISSION ON (they initiated) **\n"));
                    nvt.binary_mode = Q_TRUE;
                } else if (nvt.dowill_type == TELNET_WONT) {
                    DLOG(("Asked for binary, server refused.\n"));
                    nvt.binary_mode = Q_FALSE;
                } else {
                    DLOG(("Server demands NVT ASCII mode.\n"));
                    nvt.binary_mode = Q_FALSE;
                }
                break;

            case 1:
                if (nvt.dowill_type == TELNET_WILL) {
                    DLOG(("** ECHO ON (we initiated) **\n"));
                    nvt.echo_mode = Q_TRUE;
                } else if (nvt.dowill_type == TELNET_DO) {
                    telnet_will(fd, ch);
                    DLOG(("** ECHO ON (they initiated) **\n"));
                    nvt.echo_mode = Q_TRUE;
                } else if (nvt.dowill_type == TELNET_WONT) {
                    DLOG(("Asked for echo, server refused.\n"));
                    nvt.echo_mode = Q_FALSE;
                } else {
                    DLOG(("Server demands no echo.\n"));
                    nvt.echo_mode = Q_FALSE;
                }
                break;

            case 3:
                if (nvt.dowill_type == TELNET_WILL) {
                    DLOG(("** SUPPRESS GO-AHEAD ON (we initiated) **\n"));
                    nvt.go_ahead = Q_FALSE;
                } else if (nvt.dowill_type == TELNET_DO) {
                    telnet_will(fd, ch);
                    DLOG(("** SUPPRESS GO-AHEAD ON (they initiated) **\n"));
                    nvt.go_ahead = Q_FALSE;
                } else if (nvt.dowill_type == TELNET_WONT) {
                    DLOG(("Asked for Suppress Go-Ahead, server refused.\n"));
                    nvt.go_ahead = Q_TRUE;
                } else {
                    DLOG(("Server demands Go-Ahead mode.\n"));
                    nvt.go_ahead = Q_TRUE;
                }
                break;

            case 24:
                if (nvt.dowill_type == TELNET_WILL) {
                    DLOG(("** TERMINAL TYPE ON (we initiated) **\n"));
                    nvt.do_term_type = Q_TRUE;
                } else if (nvt.dowill_type == TELNET_DO) {
                    telnet_will(fd, ch);
                    DLOG(("** TERMINAL TYPE ON (they initiated) **\n"));
                    nvt.do_term_type = Q_TRUE;
                } else if (nvt.dowill_type == TELNET_WONT) {
                    DLOG(("Asked for Terminal Type, server refused.\n"));
                    nvt.do_term_type = Q_FALSE;
                } else {
                    DLOG(("Server will not listen to terminal type.\n"));
                    nvt.do_term_type = Q_FALSE;
                }
                break;

            case 31:
                if (nvt.dowill_type == TELNET_WILL) {
                    telnet_will(fd, ch);
                    DLOG(("** Negotiate About Window Size ON (they initiated) **\n"));
                    nvt.do_naws = Q_TRUE;
                    /*
                     * Send our window size
                     */
                    telnet_send_naws(fd, HEIGHT - STATUS_HEIGHT, WIDTH);
                } else if (nvt.dowill_type == TELNET_DO) {
                    /*
                     * Server will use NAWS, yay
                     */
                    nvt.do_naws = Q_TRUE;

                    /*
                     * Send our window size
                     */
                    telnet_send_naws(fd, HEIGHT - STATUS_HEIGHT, WIDTH);
                    DLOG(("** Negotiate About Window Size ON (we initiated) **\n"));

                } else if (nvt.dowill_type == TELNET_WONT) {
                    DLOG(("Asked for Negotiate About Window Size, server refused.\n"));
                    nvt.do_naws = Q_FALSE;
                } else {
                    DLOG(("Asked for Negotiate About Window Size, server refused.\n"));
                    nvt.do_naws = Q_FALSE;
                }
                break;

            case 32:
                if (nvt.dowill_type == TELNET_WILL) {
                    DLOG(("** TERMINAL SPEED ON (we initiated) **\n"));
                    nvt.do_term_speed = Q_TRUE;
                } else if (nvt.dowill_type == TELNET_DO) {
                    telnet_will(fd, ch);
                    DLOG(("** TERMINAL SPEED ON (they initiated) **\n"));
                    nvt.do_term_speed = Q_TRUE;
                } else if (nvt.dowill_type == TELNET_WONT) {
                    DLOG(("Asked for Terminal Speed, server refused.\n"));
                    nvt.do_term_speed = Q_FALSE;
                } else {
                    DLOG(("Server will not listen to terminal speed.\n"));
                    nvt.do_term_speed = Q_FALSE;
                }
                break;

            case 35:
                /*
                 * X Display Location - don't do this option
                 */
                telnet_refuse(nvt.dowill_type, fd, ch);
                break;

            case 39:
                if (nvt.dowill_type == TELNET_WILL) {
                    DLOG(("** NEW ENVIRONMENT ON (we initiated) **\n"));
                    nvt.do_environment = Q_TRUE;
                } else if (nvt.dowill_type == TELNET_DO) {
                    telnet_will(fd, ch);
                    DLOG(("** NEW ENVIRONMENT ON (they initiated) **\n"));
                    nvt.do_environment = Q_TRUE;
                } else if (nvt.dowill_type == TELNET_WONT) {
                    DLOG(("Asked for New Environment, server refused.\n"));
                    nvt.do_environment = Q_FALSE;
                } else {
                    DLOG(("Server will not listen to new environment.\n"));
                    nvt.do_environment = Q_FALSE;
                }
                break;

            default:

                DLOG(("   OTHER: %d \\%3o 0x%02x\n", ch, ch, ch));

                /*
                 * Don't do this option
                 */
                telnet_refuse(nvt.dowill_type, fd, ch);
                break;
            }
            nvt.dowill = Q_FALSE;
            continue;

        } /* if (nvt.dowill == Q_TRUE) */

        /*
         * Perform read processing
         */
        if (ch == TELNET_IAC) {

            /*
             * Telnet command
             */
            if (nvt.iac == Q_TRUE) {
                /*
                 * IAC IAC -> IAC
                 */

                DLOG(("IAC IAC --> IAC\n"));

                ((char *) buf)[total] = TELNET_IAC;
                total++;
                nvt.iac = Q_FALSE;
            } else {
                nvt.iac = Q_TRUE;
            }
            continue;
        }

        /*
         * ch is not IAC.
         */
        if (nvt.iac == Q_TRUE) {

            DLOG(("Telnet command: "));

            switch (ch) {

            case TELNET_SE:
                DLOG2((" END Sub-Negotiation\n"));
                break;
            case TELNET_NOP:
                DLOG2((" NOP\n"));
                break;
            case TELNET_DM:
                DLOG2((" Data Mark\n"));
                break;
            case TELNET_BRK:
                DLOG2((" Break\n"));
                break;
            case TELNET_IP:
                DLOG2((" Interrupt Process\n"));
                break;
            case TELNET_AO:
                DLOG2((" Abort Output\n"));
                break;
            case TELNET_AYT:
                DLOG2((" Are You There?\n"));
                break;
            case TELNET_EC:
                DLOG2((" Erase Character\n"));
                break;
            case TELNET_EL:
                DLOG2((" Erase Line\n"));
                break;
            case TELNET_GA:
                DLOG2((" Go Ahead\n"));
                break;
            case TELNET_SB:
                DLOG2((" START Sub-Negotiation\n"));
                /*
                 * From here we wait for the IAC SE
                 */
                nvt.subneg_end = Q_TRUE;
                subneg_buffer_n = 0;
                break;
            case TELNET_WILL:
                DLOG2((" WILL\n"));
                nvt.dowill = Q_TRUE;
                nvt.dowill_type = ch;
                break;
            case TELNET_WONT:
                DLOG2((" WON'T\n"));
                nvt.dowill = Q_TRUE;
                nvt.dowill_type = ch;
                break;
            case TELNET_DO:
                DLOG2((" DO\n"));
                nvt.dowill = Q_TRUE;
                nvt.dowill_type = ch;

                if (nvt.binary_mode == Q_TRUE) {
                    DLOG(("Telnet DO in binary mode\n"));
                }

                break;
            case TELNET_DONT:
                DLOG2((" DON'T\n"));
                nvt.dowill = Q_TRUE;
                nvt.dowill_type = ch;
                break;
            default:

                /*
                 * This should be equivalent to IAC NOP
                 */
                DLOG2((" Unknown: %d \\%03d 0x%02x %c\n", ch, ch, ch, ch));
                DLOG(("Will treat as IAC NOP\n"));
                break;
            }

            nvt.iac = Q_FALSE;
            continue;

        } /* if (nvt.iac == Q_TRUE) */

        /*
         * All of the regular IAC processing is completed at this point.  Now
         * we need to handle the CR and CR LF cases.
         *
         * According to RFC 854, in NVT ASCII mode:
         *     Bare CR -> CR NUL
         *     CR LF -> CR LF
         *
         */
        if (nvt.binary_mode == Q_FALSE) {

            if (ch == C_LF) {
                if (nvt.read_cr == Q_TRUE) {
                    DLOG(("CRLF\n"));
                    /*
                     * This is CR LF.  Send CR LF and turn the cr flag off.
                     */
                    ((char *) buf)[total] = C_CR;
                    total++;
                    ((char *) buf)[total] = C_LF;
                    total++;
                    nvt.read_cr = Q_FALSE;
                    continue;
                }

                DLOG(("Bare LF\n"));

                /*
                 * This is bare LF.  Send LF.
                 */
                ((char *) buf)[total] = C_LF;
                total++;
                continue;
            }

            if (ch == C_NUL) {
                if (nvt.read_cr == Q_TRUE) {
                    DLOG(("CR NUL\n"));
                    /*
                     * This is CR NUL.  Send CR and turn the cr flag off.
                     */
                    ((char *) buf)[total] = C_CR;
                    total++;
                    nvt.read_cr = Q_FALSE;
                    continue;
                }

                DLOG(("Bare NUL\n"));

                /*
                 * This is bare NUL.  As per RFC 854 this is a NOP for the
                 * printer.  Strip it out.
                 */
                continue;
            }

            if (ch == C_CR) {
                if (nvt.read_cr == Q_TRUE) {
                    DLOG(("CR CR\n"));
                    /*
                     * This is CR CR.  Send a CR NUL and leave the cr flag
                     * on.
                     */
                    ((char *) buf)[total] = C_CR;
                    total++;
                    ((char *) buf)[total] = C_NUL;
                    total++;
                    continue;
                }
                /*
                 * This is the first CR.  Set the cr flag.
                 */
                nvt.read_cr = Q_TRUE;
                continue;
            }

            if (nvt.read_cr == Q_TRUE) {
                DLOG(("Bare CR\n"));
                /*
                 * This was a bare CR in the stream.
                 */
                ((char *) buf)[total] = C_CR;
                total++;
                nvt.read_cr = Q_FALSE;
            }

        } /* if (nvt.binary_mode == Q_FALSE) */

        /*
         * This is the case for any of:
         *
         *     1) A NVT ASCII character that isn't CR, LF,
         *        or NUL.
         *
         *     2) A NVT binary character.
         *
         * For all of these cases, we just pass the character on.
         */
        ((char *) buf)[total] = ch;
        total++;

    } /* for (i = 0; i < read_buffer_n; i++) */

    /*
     * Return bytes read
     */
    DLOG(("telnet_read() : send %d bytes to caller:\n", (int) total));
    for (i = 0; i < total; i++) {
        DLOG2((" %02x", (((char *) buf)[i] & 0xFF)));
    }
    DLOG2(("\n"));
    for (i = 0; i < total; i++) {
        if ((((char *) buf)[i] & 0xFF) >= 0x80) {
            DLOG2((" %02x", (((char *) buf)[i] & 0xFF)));
        } else {
            DLOG2((" %c ", (((char *) buf)[i] & 0xFF)));
        }
    }
    DLOG2(("\n"));

    /*
     * read_buffer is always fully consumed
     */
    read_buffer_n = 0;

    if (total == 0) {
        /*
         * We consumed everything, but it's not EOF.  Return EAGAIN.
         */
        DLOG(("telnet_read() : EAGAIN\n"));

#ifdef Q_PDCURSES_WIN32
        set_errno(WSAEWOULDBLOCK);
#else
        set_errno(EAGAIN);
#endif
        return -1;
    }

    return total;
}

/**
 * Write data from a buffer to the remote system, via an 8-bit clean channel
 * through the telnet protocol.
 *
 * @param fd the socket descriptor
 * @param buf the buffer to read from
 * @param count the number of bytes to write to the remote side
 * @return the number of bytes written
 */
ssize_t telnet_write(const int fd, void * buf, size_t count) {
    unsigned char ch;
    unsigned int i;
    int sent = 0;
    Q_BOOL flush = Q_FALSE;

    if (state == INIT) {
        /*
         * Start the telnet protocol negotiation.
         */
        telnet_send_options(fd);
        state = SENT_OPTIONS;
    }

    DLOG(("telnet_write() : write %d bytes:\n", (int) count));
    for (i = 0; i < count; i++) {
        DLOG2((" %02x", (((char *) buf)[i] & 0xFF)));
    }
    DLOG2(("\n"));

    /*
     * If we had an error last time, return that
     */
    if (nvt.write_last_error == Q_TRUE) {
        set_errno(nvt.write_last_errno);
        nvt.write_last_error = Q_FALSE;
        return nvt.write_rc;
    }

    if (count == 0) {
        /*
         * NOP
         */
        return 0;
    }

    /*
     * Flush whatever we didn't send last time
     */
    if (write_buffer_n > 0) {
        flush = Q_TRUE;
    }

    /*
     * Setup for loop
     */
    i = 0;

write_flush:

    /*
     * See if we need to sync with the remote side now
     */
    if (flush == Q_TRUE) {
        unsigned int j;
        DLOG(("telnet_write() : write to remote side %d bytes:\n",
                write_buffer_n));
        for (j = 0; j < write_buffer_n; j++) {
            DLOG2((" %02x", (write_buffer[j] & 0xFF)));
        }
        DLOG2(("\n"));

        nvt.write_rc = send(fd, (const char *) write_buffer, write_buffer_n, 0);
        if (nvt.write_rc <= 0) {
            /*
             * Encountered an error
             */
            nvt.write_last_errno = get_errno();
            if ((get_errno() == EAGAIN) ||
#ifdef Q_PDCURSES_WIN32
                 (get_errno() == WSAEWOULDBLOCK)
#else
                (get_errno() == EWOULDBLOCK)
#endif
            ) {
                /*
                 * We filled up the other side, bail out.  Don't flag this as
                 * an error to the caller unless no data got out.
                 */
                nvt.write_last_error = Q_FALSE;
                if (sent > 0) {
                    /*
                     * Something good got out.
                     */
                    return sent;
                } else {
                    /*
                     * Let the caller see the original EAGAIN.  errno is
                     * already set.
                     */
                    return -1;
                }
            }

            /*
             * This is either another error or EOF.
             */
            if (sent > 0) {
                /*
                 * We've sent good stuff before.  Return the known good sent
                 * bytes, then return the error on the next call to
                 * telnet_write().
                 */
                nvt.write_last_error = Q_TRUE;
                return sent;
            } else {
                /*
                 * This is the first error, just return it.
                 */
                nvt.write_last_error = Q_FALSE;
                return nvt.write_rc;
            }
        } else {
            /*
             * Note: sent is following the _input_ count, not the actual
             * output count.
             */
            sent = i;
            memmove(write_buffer, write_buffer + nvt.write_rc,
                    write_buffer_n - nvt.write_rc);
            write_buffer_n -= nvt.write_rc;
        }
        flush = Q_FALSE;
    }

    while (i < count) {

        /*
         * We must have at least 2 bytes free in write_buffer
         */
        if (sizeof(write_buffer) - write_buffer_n < 4) {
            break;
        }

        /*
         * Pull the next character
         */
        ch = ((unsigned char *) buf)[i];
        i++;

        if (nvt.binary_mode == Q_TRUE) {
            DLOG(("telnet_write() : BINARY: %c \\%o %02x\n", ch, ch, ch));

            if (ch == TELNET_IAC) {
                /*
                 * IAC -> IAC IAC
                 */
                write_buffer[write_buffer_n] = TELNET_IAC;
                write_buffer_n++;
                write_buffer[write_buffer_n] = TELNET_IAC;
                write_buffer_n++;
                flush = Q_TRUE;
                goto write_flush;
            } else {
                /*
                 * Anything else -> just send
                 */
                write_buffer[write_buffer_n] = ch;
                write_buffer_n++;
                continue;
            }
        }

        /*
         * Non-binary mode: more complicated
         */
        DLOG(("telnet_write() : ASCII: %c \\%o %02x\n", ch, ch, ch));

        /*
         * Bare carriage return -> CR NUL
         */
        if (ch == C_CR) {
            if (nvt.write_cr == Q_TRUE) {
                /*
                 * CR <anything> -> CR NULL
                 */
                write_buffer[write_buffer_n] = C_CR;
                write_buffer_n++;
                write_buffer[write_buffer_n] = C_NUL;
                write_buffer_n++;
                flush = Q_TRUE;
            }
            nvt.write_cr = Q_TRUE;
        } else if (ch == C_LF) {
            if (nvt.write_cr == Q_TRUE) {
                /*
                 * CR LF -> CR LF
                 */
                write_buffer[write_buffer_n] = C_CR;
                write_buffer_n++;
                write_buffer[write_buffer_n] = C_LF;
                write_buffer_n++;
                flush = Q_TRUE;
            } else {
                /*
                 * Bare LF -> LF
                 */
                write_buffer[write_buffer_n] = ch;
                write_buffer_n++;
            }
            nvt.write_cr = Q_FALSE;
        } else if (ch == TELNET_IAC) {
            if (nvt.write_cr == Q_TRUE) {
                /*
                 * CR <anything> -> CR NULL
                 */
                write_buffer[write_buffer_n] = C_CR;
                write_buffer_n++;
                write_buffer[write_buffer_n] = C_NUL;
                write_buffer_n++;
            }
            /*
             * IAC -> IAC IAC
             */
            write_buffer[write_buffer_n] = TELNET_IAC;
            write_buffer_n++;
            write_buffer[write_buffer_n] = TELNET_IAC;
            write_buffer_n++;

            nvt.write_cr = Q_FALSE;
            flush = Q_TRUE;
        } else {
            /*
             * Normal character
             */
            write_buffer[write_buffer_n] = ch;
            write_buffer_n++;
        }

        if (flush == Q_TRUE) {
            goto write_flush;
        }

    } /* while (i < count) */

    if ((nvt.write_cr == Q_TRUE) &&
        ((q_program_state == Q_STATE_CONSOLE) ||
            (q_program_state == Q_STATE_HOST))
    ) {
        /*
         * Assume that any bare CR sent from the console needs to go out.
         */
        write_buffer[write_buffer_n] = C_CR;
        write_buffer_n++;
        nvt.write_cr = Q_FALSE;
    }

    if ((write_buffer_n > 0) && (flush == Q_FALSE)) {
        /*
         * We've got more data, push it out.  If we hit EAGAIN or some other
         * error the flush block will do the exit.
         */
        flush = Q_TRUE;
        goto write_flush;
    }

    /*
     * Return total bytes sent.
     */
    return sent;
}

/* -------------------------------------------------------------------------- */
/* RLOGIN read/write -------------------------------------------------------- */
/* -------------------------------------------------------------------------- */

/**
 * Send new screen dimensions to the remote side.
 *
 * @param lines the number of screen rows
 * @param columns the number of screen columns
 */
void rlogin_resize_screen(const int lines, const int columns) {
    unsigned char buffer[12];
    buffer[0] = 0xFF;
    buffer[1] = 0xFF;
    buffer[2] = 's';
    buffer[3] = 's';
    buffer[4] = lines / 256;
    buffer[5] = lines % 256;
    buffer[6] = columns / 256;
    buffer[7] = columns % 256;
    /*
     * Assume 9 x 16 characters ?
     */
    buffer[8] = (columns * 9) / 256;
    buffer[9] = (columns * 9) % 256;
    buffer[10] = (lines * 16) / 256;
    buffer[11] = (lines * 16) % 256;
    raw_write(q_child_tty_fd, buffer, 12);
}

/**
 * Send the rlogin header as per RFC 1258.
 *
 * @param fd the socket descriptor
 */
static void rlogin_send_login(const int fd) {
    unsigned char buffer[128];
#ifdef Q_PDCURSES_WIN32
    char username[UNLEN + 1];
    DWORD username_n = sizeof(username) - 1;
    char notify_message[DIALOG_MESSAGE_SIZE];
#endif

    /*
     * UTF-8 encode support for passing the username.  snprintf() has
     * "issues" on Windows.  Rather than have two code paths, just explicitly
     * convert the username to UTF-8 in this function.
     */
    char utf8_buffer[6];
    int rc;
    int i;

    /*
     * Empty string
     */
    buffer[0] = 0;
    raw_write(fd, buffer, 1);

    /*
     * Local username
     */
#ifdef Q_PDCURSES_WIN32
    memset(username, 0, sizeof(username));
    if (GetUserNameA(username, &username_n) == FALSE) {
        /*
         * Error: can't get local username
         */
        snprintf(notify_message, sizeof(notify_message),
                 _("Error getting local username: %d %s"),
                 GetLastError(), strerror(GetLastError()));
        notify_form(notify_message, 0);
    } else {
        snprintf((char *) buffer, sizeof(buffer) - 1, "%s", username);
    }
#else
    snprintf((char *) buffer, sizeof(buffer) - 1, "%s",
             getpwuid(geteuid())->pw_name);
#endif
    raw_write(fd, buffer, strlen((char *) buffer) + 1);

    /*
     * Remote username
     */
    if ((q_status.current_username != NULL) &&
        (wcslen(q_status.current_username) > 0)
    ) {
        /* Explicitly convert to UTF-8 here. */
        for (i = 0; i < wcslen(q_status.current_username); i++) {
            rc = utf8_encode(q_status.current_username[i], utf8_buffer);
            utf8_buffer[rc] = 0;
            raw_write(fd, utf8_buffer, rc);
        }
        /* The terminating NUL. */
        utf8_buffer[0] = 0;
        raw_write(fd, utf8_buffer, 1);
    } else {
#ifdef Q_PDCURSES_WIN32
        snprintf((char *) buffer, sizeof(buffer) - 1, "%s", username);
#else
        snprintf((char *) buffer, sizeof(buffer) - 1, "%s",
                 getpwuid(geteuid())->pw_name);
#endif
        raw_write(fd, buffer, strlen((char *) buffer) + 1);
    }

    /*
     * terminal/speed
     */
    snprintf((char *) buffer, sizeof(buffer) - 1, "%s/38400",
             emulation_term(q_status.emulation));
    raw_write(fd, buffer, strlen((char *) buffer) + 1);
}

/**
 * Read data from remote system to a buffer, via an 8-bit clean channel
 * through the rlogin protocol.
 *
 * @param fd the socket descriptor
 * @param buf the buffer to write to
 * @param count the number of bytes requested
 * @param oob if true, read out-of-band data
 * @return the number of bytes read into buf
 */
ssize_t rlogin_read(const int fd, void * buf, size_t count, Q_BOOL oob) {
    unsigned char ch;
    int rc;
    int total;
    size_t max_read;
    int i;

    DLOG(("rlogin_read() : %d bytes in read_buffer\n", read_buffer_n));

    /*
     * Perform the raw read
     */
    if (count == 0) {
        /*
         * NOP
         */
        return 0;
    }

    if (nvt.is_eof == Q_TRUE) {
        DLOG(("rlogin_read() : no read because EOF\n"));
        /*
         * Do nothing
         */

    } else {
        if (oob == Q_TRUE) {
            /*
             * Look for OOB data
             */
#ifdef Q_PDCURSES_WIN32
            rc = recv(fd, (char *) &ch, 1, MSG_OOB);
#else
            rc = recv(fd, &ch, 1, MSG_OOB);
#endif
            if (rc == 1) {
                DLOG(("rlogin_read() : OOB DATA: 0x%02x\n", ch));

                /*
                 * An OOB message came through
                 */
                if (ch == 0x80) {
                    /*
                     * Resize screen
                     */
                    rlogin_resize_screen(HEIGHT - STATUS_HEIGHT, WIDTH);
                    state = ESTABLISHED;
                } else if (ch == 0x02) {
                    /*
                     * Discard unprocessed screen data
                     */
                } else if (ch == 0x10) {
                    /*
                     * Switch to "raw" mode (pass XON/XOFF to remote side)
                     */
                } else if (ch == 0x20) {
                    /*
                     * Switch to "cooked" mode (handle XON/XOFF locally)
                     */
                }
            }
#ifdef Q_PDCURSES_WIN32
            set_errno(WSAEWOULDBLOCK);
#else
            set_errno(EAGAIN);
#endif
            return -1;

        } /* if (oob == Q_TRUE) */

        max_read = sizeof(read_buffer) - read_buffer_n;
        if (max_read > count) {
            max_read = count;
        }

        /*
         * Read some data from the other end
         */
        rc = recv(fd, (char *) read_buffer + read_buffer_n, max_read, 0);

        DLOG(("rlogin_read() : read %d bytes:\n", rc));
        for (i = 0; i < rc; i++) {
            DLOG2((" %02x", (read_buffer[read_buffer_n + i] & 0xFF)));
        }
        DLOG2(("\n"));
        for (i = 0; i < rc; i++) {
            if ((read_buffer[read_buffer_n + i] & 0xFF) >= 0x80) {
                DLOG2((" %02x", (read_buffer[read_buffer_n + i] & 0xFF)));
            } else {
                DLOG2((" %c ", (read_buffer[read_buffer_n + i] & 0xFF)));
            }
        }
        DLOG2(("\n"));

        /*
         * Check for EOF or error
         */
        if (rc < 0) {
            if (read_buffer_n == 0) {
                /*
                 * Something bad happened, just return it
                 */
                return rc;
            }
        } else if (rc == 0) {
            DLOG(("rlogin_read() : rc = 0 errno = %d (%s)\n", get_errno(),
                    get_strerror(get_errno())));


            /*
             * EOF - Drop a connection close message
             */
            nvt.is_eof = Q_TRUE;
        } else {
            /*
             * More data came in
             */
            read_buffer_n += rc;
        }
    } /* if (nvt.is_eof == Q_TRUE) */

    if ((read_buffer_n == 0) && (nvt.eof_msg == Q_TRUE)) {
        /*
         * We are done, return the final EOF and do not permit further reads.
         */
        connected = Q_FALSE;
        return 0;
    }

    if ((read_buffer_n == 0) && (nvt.is_eof == Q_TRUE)) {
        /*
         * EOF - Drop "Connection closed."  Note that we don't check for host
         * mode because we do not support rlogin host.
         */
        snprintf((char *) read_buffer, sizeof(read_buffer), "%s",
                 _("Connection closed.\r\n"));
        read_buffer_n = strlen((char *) read_buffer);
        nvt.eof_msg = Q_TRUE;
    }

    /*
     * Copy the bytes raw to the other side
     */
    memcpy(buf, read_buffer, read_buffer_n);
    total = read_buffer_n;

    /*
     * Return bytes read
     */
    DLOG(("rlogin_read() : send %d bytes to caller:\n", (int) total));
    for (i = 0; i < total; i++) {
        DLOG2((" %02x", (((char *) buf)[i] & 0xFF)));
    }
    DLOG2(("\n"));
    for (i = 0; i < total; i++) {
        if ((((char *) buf)[i] & 0xFF) >= 0x80) {
            DLOG2((" %02x", (((char *) buf)[i] & 0xFF)));
        } else {
            DLOG2((" %c ", (((char *) buf)[i] & 0xFF)));
        }
    }
    DLOG2(("\n"));

    /*
     * read_buffer is always fully consumed
     */
    read_buffer_n = 0;

    if (total == 0) {
        DLOG(("rlogin_read() : EAGAIN\n"));
        /*
         * We consumed everything, but it's not EOF.  Return EAGAIN.
         */
#ifdef Q_PDCURSES_WIN32
        set_errno(WSAEWOULDBLOCK);
#else
        set_errno(EAGAIN);
#endif
        return -1;
    }

    return total;
}

/**
 * Write data from a buffer to the remote system, via an 8-bit clean channel
 * through the rlogin protocol.
 *
 * @param fd the socket descriptor
 * @param buf the buffer to read from
 * @param count the number of bytes to write to the remote side
 * @return the number of bytes written
 */
ssize_t rlogin_write(const int fd, void * buf, size_t count) {
    return send(fd, (const char *) buf, count, 0);
}

#ifdef Q_SSH_CRYPTLIB

/* -------------------------------------------------------------------------- */
/* SSH connect/read/write --------------------------------------------------- */
/* -------------------------------------------------------------------------- */

/*
 * SSH uses cryptlib, it's a very straightforward library.  We need to define
 * __WINDOWS__ or __UNIX__ before loading crypt.h.
 */
#ifdef Q_PDCURSES_WIN32
#define __WINDOWS__
#else
#define __UNIX__
#endif

#ifdef __cplusplus
extern "C" {
#endif

#include <crypt.h>

#ifdef __cplusplus
}

#endif

/*
 * cryptlib says to set TCP_NODELAY
 */
#ifndef Q_PDCURSES_WIN32
#include <netinet/tcp.h>
#endif

#include "input.h"
#include "options.h"

/**
 * Everything is done through the crypt session interface.
 */
static CRYPT_SESSION cryptSession;

/**
 * cryptlib RBAC parameter.  We will never use it.
 */
static int cryptUser = CRYPT_UNUSED;

/**
 * The human-readable SHA1 key for a ssh server.
 */
static char * ssh_server_key = NULL;

/**
 * The filename in the working directory to store a qodem ssh server key.
 */
#define HOST_SSH_SERVER_KEY_FILENAME "ssh_server_key.p15"

/**
 * If true, new screen dimensions need to be sent to the remote side.
 */
Q_BOOL ssh_send_window_change = Q_FALSE;

/**
 * One entry in the known_hosts file.
 */
struct known_host_entry {
    /**
     * The hostname string.  This is usually the DNS name, but could be the
     * IP address.
     */
    char * host;

    /**
     * The port number as a string.
     */
    char * port;

    /**
     * The host key fingerprint as a raw byte array.
     */
    char fingerprint[CRYPT_MAX_HASHSIZE + 1];

    /**
     * The length of the host key fingerprint.
     */
    int fingerprint_n;

    /**
     * The next entry in the list.
     */
    struct known_host_entry * next;
};

/**
 * Flag to indicate some more data MIGHT be ready to read.
 */
static Q_BOOL maybe_readable = Q_FALSE;

/**
 * Flag to indicate some more data MIGHT be ready to read.  This can happen
 * if the last call to ssh_read() resulted in a length of 0 or EAGAIN.  The
 * socket will not be readable to select(), but another call to ssh_read()
 * could read some data.
 *
 * @return true if there might be data to read from the ssh session
 */
Q_BOOL ssh_maybe_readable() {
    return maybe_readable;
}

/**
 * Convert a raw SHA1 hash value to a human-readable ASCII string.
 *
 * @param sha1 the hash value
 * @param dest the string to write
 * @param n the maximum number of bytes to write to dest
 */
static void sha1_to_string(const char * sha1, char * dest, const int n) {
    const int len = 20;
    int i;
    int j = 0;
    if (n < len * 3 + 2) {
        memset(dest, 0, n);
    } else {
        memset(dest, 0, len * 3 + 2);
    }
    for (i = 0; i < (len / 2) && (i < n); i++) {
        sprintf(&dest[j++], "%x", (sha1[i] >> 4) & 0x0F);
        sprintf(&dest[j++], "%x",  sha1[i]       & 0x0F);
        sprintf(&dest[j++], ":");
    }
    for (; (i < len) & (i < n); i++) {
        sprintf(&dest[j++], "%x", (sha1[i] >> 4) & 0x0F);
        sprintf(&dest[j++], "%x",  sha1[i]       & 0x0F);
        sprintf(&dest[j++], ":");
    }
    dest[j - 1] = 0;
}

/**
 * Load all entries from the known_hosts file.
 *
 * @param known_hosts the list of known_host_entry's to create
 */
static void load_knownhosts(struct known_host_entry ** known_hosts) {
    char * filename;
    FILE * file;
    char * begin;
    char * end;
    int i;
    char ch;
    /*
     * Hostname is limited to the size of a phonebook entry host name, so use
     * the same size here as the phonebook.
     */
    char line[PHONEBOOK_LINE_SIZE];
    char buffer[PHONEBOOK_LINE_SIZE];
    struct known_host_entry * old_entry;
    struct known_host_entry * new_entry;

    enum SCAN_STATES {
        SCAN_STATE_NONE,        /* Between entries */
        SCAN_STATE_ENTRY,       /* Scanning for single-line fields */
        SCAN_STATE_NOTES        /* Reading the notes */
    };

    int scan_state;

    DLOG(("load_knownhosts()\n"));

    filename = get_option(Q_OPTION_SSH_KNOWNHOSTS);
    file = fopen(filename, "r");
    if (file == NULL) {
        /* Error, just quietly bail out. */
        DLOG(("Can't open %s for reading: %s\n", filename, strerror(errno)));
        return;
    }

    old_entry = NULL;
    new_entry = NULL;
    scan_state = SCAN_STATE_NONE;
    while (!feof(file)) {

        if (fgets(line, sizeof(line), file) == NULL) {
            /*
             * This will cause the outer while's feof() check to fail and
             * smoothly exit the while loop.
             */
            continue;
        }
        begin = line;

        if (scan_state == SCAN_STATE_NONE) {
            while ((strlen(line) > 0) && q_isspace(line[strlen(line) - 1])) {
                /*
                 * Trim trailing whitespace
                 */
                line[strlen(line) - 1] = '\0';
            }
            while (q_isspace(*begin)) {
                /*
                 * Trim leading whitespace
                 */
                begin++;
            }

            if ((strlen(begin) == 0) || (*begin == '#')) {
                /*
                 * Ignore blank lines and commented lines between entries
                 */
                continue;
            }

            if (strncmp(begin, "[entry]", sizeof("[entry]")) == 0) {
                /*
                 * Beginning of an entry found
                 */
                scan_state = SCAN_STATE_ENTRY;
                new_entry = (struct known_host_entry *)
                    Xmalloc(sizeof(struct known_host_entry),
                    __FILE__, __LINE__);
                memset(new_entry, 0, sizeof(struct known_host_entry));
                if (*known_hosts == NULL) {
                    *known_hosts = new_entry;
                } else {
                    old_entry->next = new_entry;
                }
                /*
                 * Save entry
                 */
                old_entry = new_entry;
                continue;
            }
        } /* if (scan_state == SCAN_STATE_NONE) */

        if (scan_state == SCAN_STATE_ENTRY) {
            while ((strlen(line) > 0) && q_isspace(line[strlen(line) - 1])) {
                /*
                 * Trim trailing whitespace
                 */
                line[strlen(line) - 1] = '\0';
            }
            while (q_isspace(*begin)) {
                /*
                 * Trim leading whitespace
                 */
                begin++;
            }

            if ((strlen(begin) == 0) || (*begin == '#')) {
                /*
                 * Ignore blank lines and commented lines between entries
                 */
                continue;
            }

            end = strchr(begin, '=');
            if (end == NULL) {
                /*
                 * Ignore this line.
                 */
                continue;
            }
            memset(buffer, 0, sizeof(buffer));
            strncpy(buffer, begin, end - begin);
            begin = end + 1;

            if (strncmp(buffer, "host", strlen("host")) == 0) {
                /*
                 * HOST
                 */
                new_entry->host = Xstrdup(begin, __FILE__, __LINE__);
            } else if (strncmp(buffer, "port", strlen("port")) == 0) {
                /*
                 * PORT
                 */
                new_entry->port = Xstrdup(begin, __FILE__, __LINE__);
            } else if (strncmp(buffer, "fingerprint",
                               strlen("fingerprint")) == 0) {
                /*
                 * FINGERPRINT
                 */
                new_entry->fingerprint_n = strlen(begin) / 4;
                for (i = 0; i < new_entry->fingerprint_n; i++) {
                    ch = tolower(begin[i * 4 + 2]);
                    if ((ch >= '0') && (ch <= '9')) {
                        ch = ch - '0';
                    } else if ((ch >= 'a') && (ch <= 'f')) {
                        ch = ch - 'a' + 10;
                    }
                    new_entry->fingerprint[i] = ch * 16;
                    ch = tolower(begin[i * 4 + 3]);
                    if ((ch >= '0') && (ch <= '9')) {
                        ch = ch - '0';
                    } else if ((ch >= 'a') && (ch <= 'f')) {
                        ch = ch - 'a' + 10;
                    }
                    new_entry->fingerprint[i] += ch;
                }

                /*
                 * -------------------
                 * ---- LAST ITEM ----
                 * -------------------
                 *
                 * There are no more supported options, switch state.
                 */
                scan_state = SCAN_STATE_NONE;
                DLOG(("Read known_hosts entry:\n", new_entry->host));
                DLOG(("    host %s\n", new_entry->host));
                DLOG(("    port %s\n", new_entry->port));
                DLOG(("    fingerprint (%d) ", new_entry->fingerprint_n));
                for (i = 0; i < new_entry->fingerprint_n; i++) {
                    DLOG2(("\\x%02x", (new_entry->fingerprint[i] & 0xFF)));
                }
                DLOG2(("\n"));
            }

        } /* if (scan_state == SCAN_STATE_ENTRY) */

    } /* while (!feof(file)) */

}

/**
 * Save all entries in a known_hosts list to the known_hosts file.
 *
 * @param known_hosts the list of known_host_entry's to save
 */
static void save_knownhosts(struct known_host_entry * known_hosts) {
    FILE * file;
    char * filename;
    struct known_host_entry * entry;
    int i;

    DLOG(("save_knownhosts()\n"));

    filename = get_option(Q_OPTION_SSH_KNOWNHOSTS);
    file = fopen(filename, "w");
    if (file == NULL) {
        /* Error, just quietly bail out. */
        DLOG(("Can't open %s for writing: %s\n", filename, strerror(errno)));
        return;
    }

    fprintf(file, "# Qodem Known Hosts File\n");
    fprintf(file, "#\n");

    for (entry = known_hosts; entry != NULL; entry = entry->next) {
        fprintf(file, "[entry]\n");
        fprintf(file, "host=%s\n", entry->host);
        fprintf(file, "port=%s\n", entry->port);
        fprintf(file, "fingerprint=");
        for (i = 0; i < entry->fingerprint_n; i++) {
            fprintf(file, "\\x%02x", (entry->fingerprint[i] & 0xFF));
        }
        fprintf(file, "\n\n");
    }
    fclose(file);
}

/**
 * Free all entries in a known_hosts list.
 *
 * @param known_hosts the list of known_host_entry's to free
 */
static void free_knownhosts(struct known_host_entry * known_hosts) {
    struct known_host_entry * next = NULL;

    DLOG(("free_knownhosts()\n"));

    while (known_hosts != NULL) {
        next = known_hosts->next;
        Xfree(known_hosts, __FILE__, __LINE__);
        known_hosts = next;
    }
}

/**
 * Retrieve an entry from the known_hosts file.
 *
 * @param host the host's DNS name or IP address
 * @param port the host's port
 * @param fingerprint where to write the host fingerprint as a raw byte array
 * @param fingerprint_n the number of bytes written to fingerprint
 * @return true if the entry was found, false otherwise
 */
static Q_BOOL get_knownhost_entry(const char * host, const char * port,
                                  char * fingerprint, int * fingerprint_n) {

    struct known_host_entry * known_hosts = NULL;
    int i;

    DLOG(("get_knownhost_entry()\n"));

    load_knownhosts(&known_hosts);

    while (known_hosts != NULL) {
        DLOG(("Scan known_hosts entry:\n", known_hosts->host));
        DLOG(("    host '%s'\n", known_hosts->host));
        DLOG(("    port '%s'\n", known_hosts->port));
        if ((strcmp(known_hosts->host, host) == 0) &&
            (strcmp(known_hosts->port, port) == 0)
        ) {
            /* Found a match, return it */
            DLOG(("-- MATCH --\n"));
            DLOG(("    fingerprint (%d) ", known_hosts->fingerprint_n));
            for (i = 0; i < known_hosts->fingerprint_n; i++) {
                DLOG2(("\\x%02x", (known_hosts->fingerprint[i] & 0xFF)));
            }
            DLOG2(("\n"));

            memcpy(fingerprint, known_hosts->fingerprint,
                known_hosts->fingerprint_n);
            *fingerprint_n = known_hosts->fingerprint_n;
            return Q_TRUE;
        }
        known_hosts = known_hosts->next;
    }

    free_knownhosts(known_hosts);
    return Q_FALSE;
}

/**
 * Create a new entry for the known_hosts file.
 *
 * @param host the host's DNS name or IP address
 * @param port the host's port
 * @param fingerprint the host fingerprint as a raw byte array
 * @param fingerprint_n the number of bytes in fingerprint
 */
static void create_knownhost_entry(const char * host, const char * port,
                                   char * fingerprint,
                                   const int fingerprint_n) {

    struct known_host_entry * known_hosts = NULL;
    struct known_host_entry * entry = NULL;

    DLOG(("create_knownhost_entry()\n"));

    load_knownhosts(&known_hosts);
    entry = (struct known_host_entry *) Xmalloc(sizeof(struct known_host_entry),
                                                __FILE__, __LINE__);
    memset(entry, 0, sizeof(struct known_host_entry));
    entry->host = (char *) host;
    entry->port = (char *) port;
    memcpy(entry->fingerprint, fingerprint, fingerprint_n);
    entry->fingerprint_n = fingerprint_n;
    entry->next = known_hosts;
    save_knownhosts(entry);
    free_knownhosts(entry);
}

/**
 * Delete an entry from the known_hosts file.
 *
 * @param host the host's DNS name or IP address
 * @param port the host's port
 */
static void delete_knownhost_entry(const char * host, const char * port) {

    struct known_host_entry * head = NULL;
    struct known_host_entry * p = NULL;
    struct known_host_entry * q = NULL;
    load_knownhosts(&head);
    p = head;

    DLOG(("delete_knownhost_entry()\n"));

    while (p != NULL) {
        DLOG(("Scan known_hosts entry:\n", p->host));
        DLOG(("    host '%s'\n", p->host));
        DLOG(("    port '%s'\n", p->port));
        if ((strcmp(p->host, host) == 0) &&
            (strcmp(p->port, port) == 0)
        ) {
            /* Found a match, delete it */
            DLOG(("-- MATCH --\n"));
            if (q != NULL) {
                /*
                 * Deleting in the middle or the end.  Remove p from the
                 * linked list.
                 */
                q->next = p->next;
            } else {
                /* Deleting at the head.  Just point the head to next. */
                head = p->next;
            }
            Xfree(p, __FILE__, __LINE__);
            save_knownhosts(head);
            free_knownhosts(head);
            return;
        }
        q = p;
        p = p->next;
    }
    /* No match found, do nothing. */
    DLOG(("delete_knownhost_entry() : no match found for host %s port %s\n",
            host, port));

}

/**
 * Write the cryptlib error message to the debug log.
 *
 * @param status the return code from a cryptlib call
 * @param object the cryptlib object that reported the error
 */
static void emit_crypto_error(int status, CRYPT_HANDLE object) {
    int errorLocus;
    int errorType;
    char errorMessage[1024];
    int errorMessage_n;

    if (DLOGNAME != NULL) {
        cryptGetAttribute(object, CRYPT_ATTRIBUTE_ERRORLOCUS, &errorLocus);
        cryptGetAttribute(object, CRYPT_ATTRIBUTE_ERRORTYPE, &errorType);
        cryptGetAttributeString(object, CRYPT_ATTRIBUTE_ERRORMESSAGE,
                                errorMessage, &errorMessage_n);

        DLOG(("cryptlib ERROR: status %d\n", status));
        DLOG(("                errorType %d\n", errorType));
        DLOG(("                errorLocus %d\n", errorLocus));
        DLOG(("                errorMessage %s\n", errorMessage));
    }
}

/**
 * Perform SSH protocol negotiation for a new TCP connection.
 *
 * @param fd a socket that is already connected
 * @param host the hostname
 * @param the port, for example "22"
 * @return the descriptor for the socket, or -1 if there was an error
 */
static int ssh_setup_connection(int fd, const char * host, const char * port) {
    char notify_message[DIALOG_MESSAGE_SIZE];
    char errorMessage[DIALOG_MESSAGE_SIZE];
    int errorMessage_n;
    int cryptStatus;
    char buffer[64];
    int tcp_flag = 1;
    Q_BOOL has_previous_fingerprint = Q_FALSE;
    char * message_lines[10];
    int keystroke;
    char fingerprint[CRYPT_MAX_HASHSIZE + 1];
    int fingerprint_n = 0;

    /* Make sure we have a socket */
    assert (fd != -1);

    /*
     * cryptlib needs the socket to be blocking.  We can do this now because
     * net_connect_finish() is done.
     */
    set_blocking(fd);

    DLOG(("ssh_setup_connection(): user %ls\n", q_status.current_username));

    /* Setup the crypt session */
    cryptStatus = cryptCreateSession(&cryptSession, cryptUser,
                                     CRYPT_SESSION_SSH);
    if (cryptStatusError(cryptStatus)) {
        DLOG(("ERROR cryptCreateSession()\n"));
        goto crypt_error;
    }

    /*
     * If we have the server fingerprint from a previous connection, set it
     * now so that cryptlib can abort the connection if the fingerprint
     * doesn't match.
     */
    if (get_knownhost_entry(host, port, fingerprint,
                            &fingerprint_n) == Q_TRUE) {

        cryptStatus = cryptSetAttributeString(cryptSession,
            CRYPT_SESSINFO_SERVER_FINGERPRINT_SHA1,
            fingerprint, fingerprint_n);

        if (cryptStatusError(cryptStatus)) {
            DLOG(("ERROR cryptSetAttribute(CRYPT_SESSINFO_SERVER_FINGERPRINT_SHA1)\n"));
            goto crypt_error;
        }
        has_previous_fingerprint = Q_TRUE;
    }

    /* Username and password need to be converted to char, not wchar_t */
    sprintf(buffer, "%ls", q_status.current_username);
    cryptStatus = cryptSetAttributeString(cryptSession, CRYPT_SESSINFO_USERNAME,
                                          buffer, strlen(buffer));
    if (cryptStatusError(cryptStatus)) {
        DLOG(("ERROR cryptSetAttribute(CRYPT_SESSINFO_USERNAME)\n"));
        goto crypt_error;
    }

    DLOG(("ssh_setup_connection(): setting password...\n"));

    sprintf(buffer, "%ls", q_status.current_password);
    cryptStatus = cryptSetAttributeString(cryptSession, CRYPT_SESSINFO_PASSWORD,
                                          buffer, strlen(buffer));
    if (cryptStatusError(cryptStatus)) {
        DLOG(("ERROR cryptSetAttribute(CRYPT_SESSINFO_PASSWORD)\n"));
        goto crypt_error;
    }

    /* Disable Nagle's algorithm as per cryptlib docs */
#ifdef Q_PDCURSES_WIN32
    if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (const char *)&tcp_flag,
                   sizeof(tcp_flag)) != 0) {
#else
    if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (void *)&tcp_flag,
                   sizeof(tcp_flag)) != 0) {
#endif
        DLOG(("Unable to set TCP_NODELAY\n"));
    } else {
        DLOG(("Disabled Nagle's algorithm\n"));
    }

    /* Pass in the network socket */
    DLOG(("Passing network socket to cryptlib\n"));
    cryptSetAttribute(cryptSession, CRYPT_SESSINFO_NETWORKSOCKET, fd);
    if (cryptStatusError(cryptStatus)) {
        DLOG(("ERROR cryptSetAttribute(CRYPT_SESSINFO_NETWORKSOCKET)\n"));
        goto crypt_error;
    }

    /* Activate the session */
    DLOG(("Activating SSH session\n"));
    cryptStatus = cryptSetAttribute(cryptSession, CRYPT_SESSINFO_ACTIVE, 1);
    if (cryptStatusError(cryptStatus)) {
        DLOG(("ERROR cryptSetAttribute(CRYPT_SESSINFO_ACTIVE)\n"));
        goto crypt_error;
    }

    /* Check the server fingerprint */
    DLOG(("SSH server key fingerprint: %s\n", ssh_server_key_str()));

    /*
     * If we just got a new fingerprint, ask if the user wants to add it.
     */
    if (has_previous_fingerprint == Q_FALSE) {

        sprintf(notify_message, _("Host key for %s:%s not found: "),
            host, port);
        message_lines[0] = notify_message;
        message_lines[1] = (char *) ssh_server_key_str();
        message_lines[2] = "";
        message_lines[3] = _("   Add to known hosts?  [Y/n/z] ");
        keystroke = q_tolower(notify_prompt_form_long(
            message_lines, _("Host Key Not Found"),
            _(" Y-Connect And Add Key   N-Connect   Z-Disconnect "),
            Q_TRUE, 0.0, "YyNnZz\r", 4));
        q_cursor_off();

        if ((keystroke == 'y') || (keystroke == Q_KEY_ENTER)) {
            cryptStatus = cryptGetAttributeString(cryptSession,
                CRYPT_SESSINFO_SERVER_FINGERPRINT_SHA1,
                fingerprint, &fingerprint_n);
            if (cryptStatusError(cryptStatus)) {
                DLOG(("ERROR cryptGetAttribute(CRYPT_SESSINFO_SERVER_FINGERPRINT_SHA1)\n"));
            } else {
                create_knownhost_entry(host, port, fingerprint, fingerprint_n);
            }
        } else if (keystroke == 'n') {
            /* Do nothing */
        } else {
            /* The user said disconnect */
            return -1;
        }
    }

    /* All good, connection is OK */
    DLOG(("SSH connection established: fd = %d\n", fd));

    /* Set the network timeouts to mimic non-blocking behavior */
    cryptSetAttribute(cryptSession, CRYPT_OPTION_NET_READTIMEOUT, 0);
    cryptSetAttribute(cryptSession, CRYPT_OPTION_NET_WRITETIMEOUT, 0);

    /* All done, let's get to moving data! */
    return fd;

crypt_error:

    /* Could not establish session */
    emit_crypto_error(cryptStatus, cryptSession);

    if (cryptStatus == CRYPT_ERROR_WRONGKEY) {
        char * host_server_key;
        int n;

        assert(has_previous_fingerprint == Q_TRUE);
        assert(fingerprint_n > 0);

        n = (fingerprint_n * 3 + 2);
        host_server_key = (char *) Xmalloc(n * sizeof(char), __FILE__,
                                           __LINE__);
        sha1_to_string(fingerprint, host_server_key, n);

        /*
         * SSH server key fingerprint has changed!  Show the user the old and
         * new and ask if they want to update the key.
         */
        sprintf(notify_message, _("Host key for %s:%s has changed! "),
            host, port);
        message_lines[0] = notify_message;
        message_lines[1] = _("Connection will be terminated to avoid possible");
        message_lines[2] = _("man-in-the-middle attack.");
        message_lines[3] = "";
        message_lines[4] = _("If you *KNOW* the server has been updated, then");
        message_lines[5] = _("you may remove the saved key and connect again.");
        message_lines[6] = _("The saved key for this server is:");
        message_lines[7] = host_server_key;
        message_lines[8] = "";
        message_lines[9] = _("   Remove saved key from known hosts?  [y/N] ");
        keystroke = q_tolower(notify_prompt_form_long(
            message_lines,
            _("Host Key Has Changed!"),
            _(" Y-Remove Key   N-Keep Old Key "),
            Q_TRUE, 0.0, "YyNnZz\r", 10));
        q_cursor_off();

        if (keystroke == 'y') {
            /* Delete the old key */
            delete_knownhost_entry(host, port);
        }

        Xfree(host_server_key, __FILE__, __LINE__);
    }

    memset(errorMessage, 0, sizeof(errorMessage));
    cryptGetAttributeString(cryptSession, CRYPT_ATTRIBUTE_ERRORMESSAGE,
                            errorMessage, &errorMessage_n);

    /* Error establishing session */
    snprintf(notify_message, sizeof(notify_message),
        _("Error: %s"), errorMessage);
    snprintf(q_dialer_modem_message,
        sizeof(q_dialer_modem_message), "%s", notify_message);

    return -1;
}

/**
 * Close SSH session.
 */
static void ssh_close() {
    int cryptStatus;
    cryptStatus = cryptDestroySession(cryptSession);
    if (cryptStatusError(cryptStatus)) {
        /*
         * cryptlib failed to close session.  Just log it, we will be closing
         * the network socket anyway.
         */
        emit_crypto_error(cryptStatus, cryptSession);
    }

    if (ssh_server_key != NULL) {
        Xfree(ssh_server_key, __FILE__, __LINE__);
        ssh_server_key = NULL;
    }

    ssh_send_window_change = Q_FALSE;
}

/**
 * Read data from remote system to a buffer, via an 8-bit clean channel
 * through the ssh protocol.
 *
 * @param fd the socket descriptor
 * @param buf the buffer to write to
 * @param count the number of bytes requested
 * @return the number of bytes read into buf
 */
ssize_t ssh_read(const int fd, void * buf, size_t count) {
    int cryptStatus;
    int readBytes;
    int i;

    DLOG(("ssh_read()\n"));

    /* Return read_buffer first - it has the connect message */
    if (read_buffer_n > 0) {
        DLOG(("ssh_read(): direct string bypass: %s\n", read_buffer));
        memcpy(buf, read_buffer, read_buffer_n);
        readBytes = read_buffer_n;
        read_buffer_n = 0;
        return readBytes;
    }

    if (nvt.is_eof == Q_TRUE) {
        DLOG(("ssh_read() : no read because EOF\n"));
        /*
         * We are done, return the final EOF and do not permit further reads.
         */
        connected = Q_FALSE;
        return 0;
    }

    /* Read some more bytes from the remote side */
    cryptStatus = cryptPopData(cryptSession, buf, count, &readBytes);
    if (cryptStatusError(cryptStatus)) {
        DLOG(("ERROR cryptPopData()\n"));
        emit_crypto_error(cryptStatus, cryptSession);

        /* cryptlib error */
        if ((cryptStatus == CRYPT_ERROR_COMPLETE) ||
            (cryptStatus == CRYPT_ERROR_READ)
        ) {
            DLOG(("EOF EOF EOF\n"));

            /* Remote end has closed connection */
            if (q_program_state != Q_STATE_HOST) {
                snprintf((char *) read_buffer, sizeof(read_buffer), "%s",
                    _("Connection closed.\r\n"));
                read_buffer_n = strlen((char *) read_buffer);
            }
            nvt.is_eof = Q_TRUE;
            /*
             * The message will be returned on the next ssh_read().
             */
            maybe_readable = Q_TRUE;
#ifdef Q_PDCURSES_WIN32
            set_errno(WSAEWOULDBLOCK);
#else
            set_errno(EAGAIN);
#endif
            return -1;
        } else {
            /* This will be an error */
            emit_crypto_error(cryptStatus, cryptSession);
            set_errno(EIO);
            return -1;
        }
    }

    DLOG(("ssh_read() : read %d bytes (count = %d):\n", readBytes, (int)count));
    for (i = 0; i < readBytes; i++) {
        DLOG2((" %02x", (((char *) buf)[i] & 0xFF)));
    }
    DLOG2(("\n"));
    for (i = 0; i < readBytes; i++) {
        if ((((char *) buf)[i] & 0xFF) >= 0x80) {
            DLOG2((" %02x", (((char *) buf)[i] & 0xFF)));
        } else {
            DLOG2((" %c ", (((char *) buf)[i] & 0xFF)));
        }
    }
    DLOG2(("\n"));

    if (readBytes == 0) {
        /* SSH protocol consumed everything.  Come back again. */
        maybe_readable = Q_TRUE;
#ifdef Q_PDCURSES_WIN32
        set_errno(WSAEWOULDBLOCK);
#else
        set_errno(EAGAIN);
#endif
        return -1;
    }

    if (readBytes == count) {
        DLOG(("ssh_read() maybe_readable: TRUE\n"));
        /*
         * There might still be more data in the envelope, have
         * process_incoming_data() call us again.
         */
        maybe_readable = Q_TRUE;
    } else {
        DLOG(("ssh_read() maybe_readable: FALSE\n"));
        maybe_readable = Q_FALSE;
    }

    /* We read something, pass it on. */
    return readBytes;
}

/**
 * Write data from a buffer to the remote system, via an 8-bit clean channel
 * through the ssh protocol.
 *
 * @param fd the socket descriptor
 * @param buf the buffer to read from
 * @param count the number of bytes to write to the remote side
 * @return the number of bytes written
 */
ssize_t ssh_write(const int fd, void * buf, size_t count) {
    int cryptStatus;
    int writtenBytes;
    int i;

    DLOG(("ssh_write()\n"));

    /* Make sure we're supposed to send something */
    assert(count > 0);

    cryptStatus = cryptPushData(cryptSession, buf, count, &writtenBytes);
    if (cryptStatusError(cryptStatus)) {
        DLOG(("ERROR cryptPushData()\n"));

        /* cryptlib error */
        emit_crypto_error(cryptStatus, cryptSession);
        if ((cryptStatus == CRYPT_ERROR_PERMISSION) &&
            (q_program_state == Q_STATE_HOST)
        ) {
            /*
             * For some odd reason the VERY FIRST write is returning
             * CRYPT_ERROR_PERMISSION.  I suspect this is a timing thing.
             * For this one case return EAGAIN so that qodem_write() will try
             * again.
             */
            set_errno(EAGAIN);
        } else {
            /* This will be an error */
            set_errno(EIO);
        }
        return -1;
    }

    /* Post everything */
    cryptStatus = cryptFlushData(cryptSession);
    if (cryptStatusError(cryptStatus)) {
        DLOG(("ERROR cryptFlushData()\n"));

        /* cryptlib error */
        emit_crypto_error(cryptStatus, cryptSession);
        /* This will be an error */
        set_errno(EIO);
        return -1;
    }

    if (writtenBytes == 0) {
        DLOG(("ssh_write() writtenBytes is 0, returning EAGAIN\n"));

        /* This isn't EOF yet */
#ifdef Q_PDCURSES_WIN32
        set_errno(WSAEWOULDBLOCK);
#else
        set_errno(EAGAIN);
#endif
        return -1;
    }

    DLOG(("ssh_write() : wrote %d bytes (count = %d):\n", writtenBytes,
            (int)count));
    for (i = 0; i < writtenBytes; i++) {
        DLOG2((" %02x", (((char *) buf)[i] & 0xFF)));
    }
    DLOG2(("\n"));
    for (i = 0; i < writtenBytes; i++) {
        if ((((char *) buf)[i] & 0xFF) >= 0x80) {
            DLOG2((" %02x", (((char *) buf)[i] & 0xFF)));
        } else {
            DLOG2((" %c ", (((char *) buf)[i] & 0xFF)));
        }
    }
    DLOG2(("\n"));

    /* We wrote something, pass it on. */
    return writtenBytes;
}

/**
 * Send new screen dimensions to the remote side.
 *
 * @param lines the number of screen rows
 * @param columns the number of screen columns
 */
void ssh_resize_screen(const int lines, const int columns) {
    /*
     * Set the flag here.  On the next call to ssh_write() cryptlib will see
     * this and generate a window-change message.
     */
    ssh_send_window_change = Q_TRUE;
}

/**
 * Get the ssh server key fingerprint as a hex-encoded SHA1 hash of the
 * server key, the same as the key fingerprint exposed by most ssh clients.
 *
 * @return the key string
 */
const char * ssh_server_key_str() {
    char fingerprint[CRYPT_MAX_HASHSIZE + 1];
    int fingerprint_n;
    int cryptStatus;
    int i;
    int n;

    if (ssh_server_key == NULL) {
        cryptStatus = cryptGetAttributeString(cryptSession,
            CRYPT_SESSINFO_SERVER_FINGERPRINT_SHA1,
            fingerprint, &fingerprint_n);

        if (cryptStatusError(cryptStatus)) {
            /* cryptlib error */
            emit_crypto_error(cryptStatus, cryptSession);
            ssh_server_key = Xstrdup("UNKNOWN", __FILE__, __LINE__);
            return ssh_server_key;
        }

        DLOG(("SSH server fingerprint (%d): ", fingerprint_n));
        for (i = 0; i < fingerprint_n; i++) {
            DLOG2((" %02x", (((char *) fingerprint)[i] & 0xFF)));
        }
        DLOG2(("\n"));

        n = (fingerprint_n * 3 + 2);
        ssh_server_key = (char *) Xmalloc(n * sizeof(char), __FILE__, __LINE__);
        sha1_to_string(fingerprint, ssh_server_key, n);
    }
    return ssh_server_key;
}

/**
 * Create a RSA private key for the SSH server.  This does nothing if the key
 * file already exists.
 */
void ssh_create_server_key() {
    int cryptStatus;
    CRYPT_CONTEXT privKeyContext;
    int keyLen = 1024 / 8;
    CRYPT_KEYSET keySet;
    char * filename;
    char notify_message[DIALOG_MESSAGE_SIZE];
    Q_BOOL old_keyboard_blocks = q_keyboard_blocks;

    filename = get_datadir_filename(HOST_SSH_SERVER_KEY_FILENAME);
    if (file_exists(filename) == Q_TRUE) {
        /* Server key already exists, bail out. */
        return;
    }

    DLOG(("ssh_create_server_key()\n"));

    /*
     * Show the user a message that we are creating the server key.  Make
     * sure we don't wait on them to press a key, just compute and go.
     */
    snprintf(notify_message, sizeof(notify_message),
        _("Creating new SSH server key, this may take some time..."));
    q_keyboard_blocks = Q_FALSE;
    notify_form(notify_message, 0.1);
    q_keyboard_blocks = old_keyboard_blocks;

    /* Create the key */
    cryptStatus = cryptCreateContext(&privKeyContext, CRYPT_UNUSED,
                                     CRYPT_ALGO_RSA);
    if (cryptStatusError(cryptStatus)) {
        DLOG(("ERROR cryptCreateContext()\n"));
        emit_crypto_error(cryptStatus, cryptSession);
        return;
    }

    cryptStatus = cryptSetAttributeString(privKeyContext, CRYPT_CTXINFO_LABEL,
                                          "RSA_KEY", 7);
    if (cryptStatusError(cryptStatus)) {
        DLOG(("ERROR cryptSetAttributeString(CRYPT_CTXINFO_LABEL)\n"));
        emit_crypto_error(cryptStatus, cryptSession);
        return;
    }

    cryptStatus = cryptSetAttribute(privKeyContext, CRYPT_CTXINFO_KEYSIZE,
                                    keyLen);
    if (cryptStatusError(cryptStatus)) {
        DLOG(("ERROR cryptSetAttribute(CRYPT_CTXINFO_KEYSIZE)\n"));
        emit_crypto_error(cryptStatus, cryptSession);
        return;
    }

    cryptStatus = cryptGenerateKey(privKeyContext);
    if (cryptStatusError(cryptStatus)) {
        DLOG(("ERROR cryptGenerateKey()\n"));
        emit_crypto_error(cryptStatus, cryptSession);
        return;
    }

    /* Save the key */
    cryptStatus = cryptKeysetOpen(&keySet, CRYPT_UNUSED, CRYPT_KEYSET_FILE,
                                  filename, CRYPT_KEYOPT_CREATE);
    if (cryptStatusError(cryptStatus)) {
        DLOG(("ERROR cryptKeysetOpen()\n"));
        emit_crypto_error(cryptStatus, cryptSession);
        return;
    }

    cryptStatus = cryptAddPrivateKey(keySet, privKeyContext, "password");
    if (cryptStatusError(cryptStatus)) {
        DLOG(("ERROR cryptAddPrivateKey()\n"));
        emit_crypto_error(cryptStatus, cryptSession);
        return;
    }

    cryptStatus = cryptKeysetClose(keySet);
    if (cryptStatusError(cryptStatus)) {
        DLOG(("ERROR cryptKeysetClose()\n"));
        emit_crypto_error(cryptStatus, cryptSession);
        return;
    }

    DLOG(("create_server_key() FINISHED\n"));
}

/**
 * Retrieve a RSA private key for the SSH server.
 */
static CRYPT_CONTEXT load_server_key() {
    CRYPT_CONTEXT privKey;
    CRYPT_KEYSET keySet;
    int cryptStatus = 0;
    char * filename;

    DLOG(("load_server_key()\n"));

    filename = get_datadir_filename(HOST_SSH_SERVER_KEY_FILENAME);
    if (file_exists(filename) == Q_FALSE) {
        /* Server key doesn't exist, create it first. */
        ssh_create_server_key();
    }

    cryptStatus = cryptKeysetOpen(&keySet, CRYPT_UNUSED, CRYPT_KEYSET_FILE,
                                  filename, CRYPT_KEYOPT_READONLY);
    if (cryptStatusError(cryptStatus)) {
        DLOG(("ERROR cryptKeysetOpen()\n"));
        emit_crypto_error(cryptStatus, cryptSession);
        return CRYPT_UNUSED;
    }

    cryptStatus = cryptGetPrivateKey(keySet, &privKey, CRYPT_KEYID_NAME,
                                     "RSA_KEY", "password");
    if (cryptStatusError(cryptStatus)) {
        DLOG(("ERROR cryptGetPrivateKey()\n"));
        emit_crypto_error(cryptStatus, cryptSession);
        return CRYPT_UNUSED;
    }

    cryptStatus = cryptKeysetClose(keySet);
    if (cryptStatusError(cryptStatus)) {
        DLOG(("ERROR cryptKeysetClose()\n"));
        emit_crypto_error(cryptStatus, cryptSession);
        return CRYPT_UNUSED;
    }

    DLOG(("load_server_key(): privKey = %d\n", privKey));
    return privKey;
}

/**
 * Accept a ssh server connection.
 *
 * @return the accepted socket descriptor, or -1 if no new connection is
 * available.
 */
static int ssh_accept(int fd) {
    char notify_message[DIALOG_MESSAGE_SIZE];
    char errorMessage[DIALOG_MESSAGE_SIZE];
    int errorMessage_n;
    int cryptStatus;
    int tcp_flag = 1;
#if 0
    char buffer[256];
    int buffer_n = 0;
#endif

    DLOG(("ssh_accept()\n"));

    /* Make sure we have a socket */
    if (fd == -1) {
        /* We never bind()'d in net_accept() */
        return fd;
    }

    /* We had better be listening now. */
    assert(listening == Q_TRUE);

    /*
     * On Linux, fd will be a blocking socket (it does NOT inherit from its
     * parent), however on Windows it will be a non-blocking socket.
     * Regardless of that, cryptlib needs a blocking socket.
     */
    set_blocking(fd);

    /* Setup the crypt session */
    cryptStatus = cryptCreateSession(&cryptSession, cryptUser,
                                     CRYPT_SESSION_SSH_SERVER);
    if (cryptStatusError(cryptStatus)) {
        DLOG(("ERROR cryptCreateSession()\n"));
        goto crypt_error;
    }

    /* Disable Nagle's algorithm as per cryptlib docs */
#ifdef Q_PDCURSES_WIN32
    if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (const char *)&tcp_flag,
                   sizeof(tcp_flag)) != 0) {
#else
    if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (void *)&tcp_flag,
                   sizeof(tcp_flag)) != 0) {
#endif
        DLOG(("Unable to set TCP_NODELAY\n"));
    } else {
        DLOG(("Disabled Nagle's algorithm\n"));
    }

    /* Pass in the SSH server private key */
    DLOG(("Loading server key into session...\n"));
    cryptStatus = cryptSetAttribute(cryptSession, CRYPT_SESSINFO_PRIVATEKEY,
                                    load_server_key());
    if (cryptStatusError(cryptStatus)) {
        DLOG(("ERROR cryptSetAttribute(CRYPT_SESSINFO_PRIVATEKEY)\n"));
        goto crypt_error;
    }

    /* Pass in the network socket */
    DLOG(("Supplying network socket fd = %d\n", fd));
    cryptStatus = cryptSetAttribute(cryptSession, CRYPT_SESSINFO_NETWORKSOCKET,
                                    fd);
    if (cryptStatusError(cryptStatus)) {
        DLOG(("ERROR cryptSetAttribute(CRYPT_SESSINFO_NETWORKSOCKET)\n"));
        goto crypt_error;
    }

    /* Automatically authenticate the user (any user) */
    DLOG(("Starting SSH session...\n"));
    cryptStatus = cryptSetAttribute(cryptSession, CRYPT_SESSINFO_ACTIVE, 1);
    if ((cryptStatusError(cryptStatus)) &&
        (cryptStatus != CRYPT_ENVELOPE_RESOURCE)
    ) {
        DLOG(("ERROR cryptSetAttribute(CRYPT_SESSINFO_ACTIVE)\n"));
        goto crypt_error;
    }

#if 0
    /* Clear any messages from the client for the initial handshake */
    DLOG(("Pop data to initiate handshake...\n"));
    cryptStatus = cryptPopData(cryptSession, buffer, sizeof(buffer), &buffer_n);
    if (cryptStatusError(cryptStatus)) {
        DLOG(("ERROR cryptPopData()\n"));
        goto crypt_error;
    }
#endif

    /*
     * Login OK.  We don't care what the username or password are, host mode
     * does its own login prompt.
     */
    DLOG(("Telling client they are authenticated.\n"));
    cryptStatus = cryptSetAttribute(cryptSession, CRYPT_SESSINFO_AUTHRESPONSE,
                                    1);
    if (cryptStatusError(cryptStatus)) {
        DLOG(("ERROR cryptSetAttribute(CRYPT_SESSINFO_AUTHRESPONSE)\n"));
        goto crypt_error;
    }

    /* Set the network timeouts to mimic non-blocking behavior */
    DLOG(("Setting network timeouts to small values.\n"));
    cryptSetAttribute(cryptSession, CRYPT_OPTION_NET_READTIMEOUT, 0);
    cryptSetAttribute(cryptSession, CRYPT_OPTION_NET_WRITETIMEOUT, 0);

    DLOG(("SSH server session established: fd = %d\n", fd));

    /* Session is established, let's get to moving data! */
    return fd;

crypt_error:

    /* This is some kind of un-recoverable error */
    emit_crypto_error(cryptStatus, cryptSession);

    cryptGetAttributeString(cryptSession, CRYPT_ATTRIBUTE_ERRORMESSAGE,
                            errorMessage, &errorMessage_n);

    /* Error establishing session */
    snprintf(notify_message, sizeof(notify_message),
        _("Error establishing SSH server session: %s"), errorMessage);
    notify_form(notify_message, 3.0);
    /* Do a full close: pretend it's open and we got EOF */
    connected = Q_TRUE;
    q_child_tty_fd = fd;
    net_close();
    return -1;
}

#endif /* Q_SSH_CRYPTLIB */
