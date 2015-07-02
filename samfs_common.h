#ifndef __SAMFS_COMMON_H__
#define __SAMFS_COMMON_H__

#include <stdio.h>
#include <errno.h>         /* errno */
#include <string.h>        /* strdup() */
#include <stdlib.h>        /* rand() */

#include <sys/types.h>     /* lstat(), mkdir(), opendir(), closedir(), open(), lseek(), truncate(), utime(), mknod(), utimes(), connect() */
#include <sys/stat.h>      /* lstat(), mkdir(), open(), chmod(), mknod() */
#include <unistd.h>        /* lstat(), rmdir(), close(), lseek(), read(), write(), truncate(), unlink(), chown(), mknod(), symlink(), readlink() */
#include <dirent.h>        /* opendir(), readdir(), closedir() */
#include <fcntl.h>         /* open(), mknod() */
#include <utime.h>         /* utime(), utimes() */
#include <sys/statvfs.h>   /* statvfs() */
#include <sys/time.h>      /* utimes() */

#include <sys/socket.h>    /* connect(), inet_addr()  */
#include <netinet/in.h>    /* inet_addr() */
#include <arpa/inet.h>     /* inet_addr(), htons() */

#define SERVER_PORT  5001

#define SUCCESS      0
#define FAIL         -1

#define TRUE         1
#define FALSE        0

#define URL_LEN      80
#define URI_LEN      160
#define DATA_SIZE    1024

typedef enum msg_type_t {
   UNKNOWN,
   GETATTR,
   ACCESS,
   MKDIR,
   OPENDIR,
   READDIR,
   RELEASEDIR,
   RMDIR,
   CREATE,
   OPEN,
   READ,
   WRITE,
   TRUNCATE,
   RELEASE,
   UNLINK,
   RENAME,
   CHMOD,
   UTIME,
   STATFS,
} msg_type_t;

/* request packet format */
typedef struct req_t {
   int      magic;            /* magic number, used by send/recv for integrity check */
   int      msg;              /* request message (of type msg_type_t) */
   char     url[URL_LEN];     /* server dir name mounted on client */
   char     uri[URI_LEN];     /* full name/path of file/dir wrt client mount-point */
   mode_t   mode;             /* mode of file operations, used by mkdir */
   int      flags;            /* flags for creating new file, used by create  */
   int      truncate_len;     /* used by truncate */
   size_t   size;             /* used by read/write */
   off_t    offset;           /* used by read/write */
   char     endofdata;        /* used by write, set to 1 if this is last data packet */
   char     data[DATA_SIZE];  /* used by write */
} req_t;

/* response packet format */
typedef struct rsp_t {
   int      magic;            /* magic number, used by send/recv for integrity check */
   int      status;           /* status of the request, 0 if success, -1 on failure */
   int      errcode;          /* stores errno in case of failure */
   size_t   size;             /* used by read/write */
   char     endofdata;        /* set to 1 if this is last data packet */
   char     data[DATA_SIZE];  /* output data of requested command */
} rsp_t;

#endif

