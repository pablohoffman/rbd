#ifndef PROTO_H
#define PROTO_H

#define PROTO_VERSION 1
#define SDPORT 8207

enum rbdmsg_type { CMD=1, REP };
enum rbdmsg_code { CMD_READ=1, CMD_WRITE, CMD_GETSZ, CMD_CLOSE, REP_OK=128, REP_ERR };

struct rbdmsg_hdr {
    unsigned int version;              /* protocol version */
    enum rbdmsg_type type;             /* command or response */
    enum rbdmsg_code code;             /* operation or response code */
    unsigned int id;                   /* unique id of the message */
    unsigned int payload_size;         /* data size, in bytes */

    unsigned int fsop_offset_sectors;  /* only used for file operation */
    unsigned int fsop_size;            /* commands */
};

#endif
