#define FUSE_USE_VERSION 26

#include <fuse.h>

#include "samfs_common.h"

/* SERVER_IP and SERVER_URL are local to this file */
static char SERVER_IP[80];
static char SERVER_URL[80];

static int connect_to_server()
{
   struct sockaddr_in   sock;
   int                  sock_fd;
   int                  ret;

   /* create a socket for TCP connection */
   sock_fd = socket(AF_INET, SOCK_STREAM, 0);

   /* connect to server */
   sock.sin_family = AF_INET;
   sock.sin_addr.s_addr = inet_addr(SERVER_IP);
   sock.sin_port = htons(SERVER_PORT);
   ret = connect(sock_fd, (struct sockaddr *)&sock, sizeof(struct sockaddr));
   if(-1 == ret) {
      return ret;
   }

   return sock_fd;
}

static int create_req_pkt(struct req_t *req, int msg, const char *path, mode_t mode, int flags, int len, const char *npath, size_t size, off_t offset)
{
   memset(req, 0, sizeof(struct req_t));
   req->msg = msg;
   strcpy(req->url, SERVER_URL);
   strcpy(req->uri, path);
   req->mode = mode;
   req->flags = flags;
   req->truncate_len = len;
   if(npath) strcpy(req->data, npath);
   req->size = size;
   req->offset = offset;

   return 0;
}

static int send_req(int sock_fd, struct req_t *req)
{
   int rv;
   int magic;

   magic = 0;
   req->magic = rand();

   rv = write(sock_fd, req, sizeof(struct req_t));
   read(sock_fd, &magic, sizeof(magic));
   if(req->magic != magic) {
      printf("ERROR IN WRITE: INVALID MAGIC!\n");
   }
   return rv;
}

static int read_rsp(int sock_fd, struct rsp_t *rsp)
{
   int rv;
   int magic;

   rv = read(sock_fd, rsp, sizeof(struct rsp_t));
   magic = rsp->magic;
   write(sock_fd, &magic, sizeof(magic));
   return rv;
}

static int masd_getattr (const char *path, struct stat *st)
{
   int server_fd;
   struct req_t req;
   struct rsp_t rsp;
   int rv;

   server_fd = connect_to_server();
   if(server_fd < 0) {
      return -errno;
   }

   rv = 0;

   create_req_pkt(&req, GETATTR, path, 0, 0, 0, NULL, 0, 0);

   send_req(server_fd, &req);

   read_rsp(server_fd, &rsp);

   if(SUCCESS == rsp.status) {
      memcpy(st, &rsp.data, sizeof(struct stat));
   }
   else {
      errno = rsp.errcode;
      rv = -errno;
   }

   close(server_fd);

   return rv;
}

static int masd_access (const char *path, int val)
{
   /* dont know, invoked when directory is accessed  */
   return 0;
}

static int masd_mkdir (const char *path, mode_t md)
{
   int server_fd;
   struct req_t req;
   struct rsp_t rsp;
   int rv;

   server_fd = connect_to_server();
   if(server_fd < 0) {
      return -errno;
   }

   rv = 0;

   create_req_pkt(&req, MKDIR, path, md, 0, 0, NULL, 0, 0);

   send_req(server_fd, &req);

   read_rsp(server_fd, &rsp);

   if(SUCCESS != rsp.status) {
      errno = rsp.errcode;
      rv = -errno;
   }

   close(server_fd);

   return rv;
}

static int masd_opendir (const char *path, struct fuse_file_info *finfo)
{
   /* not really required, can check for persmissions and other such stuff */
   return 0;
}

static int masd_readdir (const char *path, void *buf, fuse_fill_dir_t filler, off_t of, struct fuse_file_info *finfo)
{
   int server_fd;
   struct req_t req;
   struct rsp_t rsp;
   int rv;
   struct dirent dent;

   server_fd = connect_to_server();
   if(server_fd < 0) {
      return -errno;
   }

   rv = 0;

   create_req_pkt(&req, READDIR, path, 0, 0, 0, NULL, 0, 0);

   send_req(server_fd, &req);

   do {
      read_rsp(server_fd, &rsp);
      if(SUCCESS == rsp.status) {
         memcpy(&dent, &rsp.data, sizeof(struct stat));
         filler(buf, dent.d_name, NULL, 0);
      }
      else {
         errno = rsp.errcode;
         rv = -errno;
      }
   } while(!rsp.endofdata);

   close(server_fd);

   return rv;
}

static int masd_releasedir (const char *path, struct fuse_file_info *finfo)
{
   /* not required, invoked when directory is released after access */
   return 0;
}

static int masd_rmdir (const char *path)
{
   int server_fd;
   struct req_t req;
   struct rsp_t rsp;
   int rv;

   server_fd = connect_to_server();
   if(server_fd < 0) {
      return -errno;
   }

   rv = 0;

   create_req_pkt(&req, RMDIR, path, 0, 0, 0, NULL, 0, 0);

   send_req(server_fd, &req);

   read_rsp(server_fd, &rsp);

   if(SUCCESS != rsp.status) {
      errno = rsp.errcode;
      rv = -errno;
   }

   close(server_fd);

   return rv;
}

static int masd_create (const char *path, mode_t md, struct fuse_file_info *finfo)
{
   int server_fd;
   struct req_t req;
   struct rsp_t rsp;
   int rv;

   server_fd = connect_to_server();
   if(server_fd < 0) {
      return -errno;
   }

   rv = 0;

   create_req_pkt(&req, CREATE, path, md, finfo->flags, 0, NULL, 0, 0);

   send_req(server_fd, &req);

   read_rsp(server_fd, &rsp);

   if(SUCCESS != rsp.status) {
      errno = rsp.errcode;
      rv = -errno;
   }

   close(server_fd);

   return rv;
}

static int masd_open (const char *path, struct fuse_file_info *finfo)
{
   /* check read/write and other permissions and finfo flag */
   return 0;
}

static int masd_read (const char *path, char *buf, size_t sz, off_t of, struct fuse_file_info *finfo)
{
   int server_fd;
   struct req_t req;
   struct rsp_t rsp;
   int rv;
   int last_of;

   server_fd = connect_to_server();
   if(server_fd < 0) {
      return -errno;
   }

   rv = 0;

   create_req_pkt(&req, READ, path, 0, 0, 0, NULL, sz, of);

   send_req(server_fd, &req);

   last_of = 0;
   do {
      read_rsp(server_fd, &rsp);
      if(SUCCESS == rsp.status) {
         if(rsp.size) {
            memcpy(buf + last_of, &rsp.data, rsp.size);
         }
         last_of += rsp.size;
         rv = last_of;
      }
      else {
         errno = rsp.errcode;
         rv = -errno;
      }
   } while(!rsp.endofdata);

   close(server_fd);

   return rv;
}

static int masd_write (const char *path, const char *buf, size_t sz, off_t of, struct fuse_file_info *finfo)
{
   int server_fd;
   struct req_t req;
   struct rsp_t rsp;
   int rv;
   int write_size;
   int total_write;
   int write_of;

   server_fd = connect_to_server();
   if(server_fd < 0) {
      return -errno;
   }

   rv = 0;

   create_req_pkt(&req, WRITE, path, 0, 0, 0, NULL, sz, of);

   send_req(server_fd, &req);

   /* check if server is ready to receive a file data */
   read_rsp(server_fd, &rsp);
   write_of = 0;
   if(SUCCESS == rsp.status) {
      write_size = (sizeof(req.data) < sz)? sizeof(req.data): sz;
      do {
         memcpy(&req.data, buf + write_of, write_size);
         req.size = write_size;
         write_of += write_size;
         total_write += write_size;
         if(total_write < sz) {
            req.endofdata = FALSE;
            write_size = (sizeof(req.data) < (sz - total_write))? sizeof(req.data): (sz - total_write);
         }
         else {
            req.endofdata = TRUE;
            write_size = 0; /* sent all data, break the loop */
         }
         send_req(server_fd, &req);
      } while(write_size);

      /* check the write status on server side */
      read_rsp(server_fd, &rsp);
      if(SUCCESS != rsp.status) {
         errno = rsp.errcode;
         rv = -errno;
      }
      else {
         rv = rsp.size; /* server will return total written bytes (should be equal to 'sz') */
      }
   }
   else {
      errno = rsp.errcode;
      rv = -errno;
   }

   close(server_fd);

   return rv;
}

static int masd_truncate (const char *path, off_t len)
{
   int server_fd;
   struct req_t req;
   struct rsp_t rsp;
   int rv;

   server_fd = connect_to_server();
   if(server_fd < 0) {
      return -errno;
   }

   rv = 0;

   create_req_pkt(&req, TRUNCATE, path, 0, 0, len, NULL, 0, 0);

   send_req(server_fd, &req);

   read_rsp(server_fd, &rsp);

   if(SUCCESS != rsp.status) {
      errno = rsp.errcode;
      rv = -errno;
   }

   close(server_fd);

   return rv;
}

static int masd_release (const char *path, struct fuse_file_info *finfo)
{
   /* dont know, invoked when file is closed(?)  */
   return 0;
}

static int masd_unlink (const char *path)
{
   int server_fd;
   struct req_t req;
   struct rsp_t rsp;
   int rv;

   server_fd = connect_to_server();
   if(server_fd < 0) {
      return -errno;
   }

   rv = 0;

   create_req_pkt(&req, UNLINK, path, 0, 0, 0, NULL, 0, 0);

   send_req(server_fd, &req);

   read_rsp(server_fd, &rsp);

   if(SUCCESS != rsp.status) {
      errno = rsp.errcode;
      rv = -errno;
   }

   close(server_fd);

   return rv;
}

static int masd_rename (const char *path, const char *npath)
{
   int server_fd;
   struct req_t req;
   struct rsp_t rsp;
   int rv;

   server_fd = connect_to_server();
   if(server_fd < 0) {
      return -errno;
   }

   rv = 0;

   create_req_pkt(&req, RENAME, path, 0, 0, 0, npath, 0, 0);

   send_req(server_fd, &req);

   read_rsp(server_fd, &rsp);

   if(SUCCESS != rsp.status) {
      errno = rsp.errcode;
      rv = -errno;
   }

   close(server_fd);

   return rv;
}

static int masd_chmod (const char *path, mode_t md)
{
   int server_fd;
   struct req_t req;
   struct rsp_t rsp;
   int rv;

   server_fd = connect_to_server();
   if(server_fd < 0) {
      return -errno;
   }

   rv = 0;

   create_req_pkt(&req, CHMOD, path, md, 0, 0, NULL, 0, 0);

   send_req(server_fd, &req);

   read_rsp(server_fd, &rsp);

   if(SUCCESS != rsp.status) {
      errno = rsp.errcode;
      rv = -errno;
   }

   close(server_fd);

   return rv;
}

static int masd_utime (const char *path, struct utimbuf *tm)
{
   int server_fd;
   struct req_t req;
   struct rsp_t rsp;
   int rv;

   server_fd = connect_to_server();
   if(server_fd < 0) {
      return -errno;
   }

   rv = 0;

   create_req_pkt(&req, UTIME, path, 0, 0, 0, NULL, 0, 0);

   send_req(server_fd, &req);

   read_rsp(server_fd, &rsp);

   if(SUCCESS == rsp.status) {
      memcpy(tm, &rsp.data, sizeof(struct utimbuf));
   }
   else {
      errno = rsp.errcode;
      rv = -errno;
   }

   close(server_fd);

   return rv;
}

static int masd_statfs (const char *path, struct statvfs * stat)
{
   int server_fd;
   struct req_t req;
   struct rsp_t rsp;
   int rv;

   server_fd = connect_to_server();
   if(server_fd < 0) {
      return -errno;
   }

   rv = 0;

   create_req_pkt(&req, STATFS, path, 0, 0, 0, NULL, 0, 0);

   send_req(server_fd, &req);

   read_rsp(server_fd, &rsp);

   if(SUCCESS == rsp.status) {
      memcpy(stat, &rsp.data, sizeof(struct statvfs));
   }
   else {
      errno = rsp.errcode;
      rv = -errno;
   }

   close(server_fd);

   return rv;
}


static struct fuse_operations masd_oper = {
   .getattr = masd_getattr,         /* get file/dir attributes */
   .access = masd_access,           /* access dir/file */

   .mkdir = masd_mkdir,             /* create dir */
   .opendir = masd_opendir,         /* not reuired */
   .readdir = masd_readdir,         /* read dir contents */
   .releasedir = masd_releasedir,   /* close dir */
   .rmdir = masd_rmdir,             /* remove dir */

   .create = masd_create,           /* create a new file */
   .open = masd_open,               /* open file */
   .read = masd_read,               /* read file */
   .write = masd_write,             /* write file */
   .truncate = masd_truncate,       /* truncate the file */
   .release = masd_release,         /* close file */
   .unlink = masd_unlink,           /* delete file */

   .rename = masd_rename,           /* rename file/dir */
   .chmod = masd_chmod,             /* change read/write/executable permissions */
   .utime = masd_utime,             /* get access time of file/dir */
   .statfs = masd_statfs,           /* stat fs */
};

int main(int argc, char *argv[])
{
   int i;
   int j;
   int digit_count;
   int is_last_char_dot;
   int mount_point;

   if(argc < 4) {
      printf("insufficient arguments\n");
      goto invalid_arg;
   }

   memset(SERVER_IP, 0, sizeof(SERVER_IP));
   memset(SERVER_URL, 0, sizeof(SERVER_URL));
   SERVER_URL[0] = '/'; /* default url is '/' */

   for(i = 1; i < argc; i++) {
      if(strcmp(argv[i], "-d") == 0) {
         /* do nothing */
      }
      else if(strcmp(argv[i], "-mount") == 0) {
         i++;
         /* first argument after '-mount' is source, i.e. remote location */
         if(i < argc) {
            /* parse argument character by character and separate ip addr and url */
            /* expected argument format is "x.x.x.x:url" or "x.x.x.x" */
            j = 0;
            digit_count = 0;
            is_last_char_dot = FALSE;
            while(argv[i][j]) {
               if((argv[i][j] >= '0' && argv[i][j] <= '9')) {  /* ip addr should contain digit ... */
                  is_last_char_dot = FALSE;
                  SERVER_IP[j] = argv[i][j];
               }
               else if(argv[i][j] == '.') {                    /* ... or dot */
                  if(is_last_char_dot == TRUE) {
                     /* last character was dot and no number found after that, i.e. ip addr is incomplete */
                     printf("invalid ip addr\n");
                     goto invalid_arg;
                  }
                  is_last_char_dot = TRUE;
                  SERVER_IP[j] = argv[i][j];
                  digit_count++;
                  if(digit_count > 3) {
                     /* ip addr should not contain more than three dots */
                     printf("ip addr too large\n");
                     goto invalid_arg;
                  }
               }
               else if(argv[i][j] == ':') {
                  if(digit_count < 3) {
                     /* less than three dots found in ip addr, i.e. ip addr is incomplete */
                     printf("incomplete ip addr\n");
                     goto invalid_arg;
                  }
                  if(is_last_char_dot == TRUE) {
                     /* last character was dot and no number found after that, i.e. ip addr is incomplete */
                     printf("incomplete ip addr\n");
                     goto invalid_arg;
                  }
                  is_last_char_dot = FALSE;

                  /* url will start from here */
                  j++; /* skip ':' */
                  if(argv[i][j] == '/') {
                     strcpy(SERVER_URL, &argv[i][j]);       /* copy as it is */
                  }
                  else {
                     strcpy(&SERVER_URL[1], &argv[i][j]);   /* prepend '/' */
                  }
                  break; /* this will break ip:url parse while loop */
               }
               else {
                  /* other than digit or dot found */
                  printf("invalid character in ip addr\n");
                  goto invalid_arg;
               }
               j++;
            } /* end of ip:url parse while loop */

            if(digit_count < 3) {
               /* less than three dots found in ip addr, i.e. ip addr is incomplete */
               printf("incomplete ip addr\n");
               goto invalid_arg;
            }
            if(is_last_char_dot == TRUE) {
               /* last character was dot and no number found after that, i.e. ip addr is incomplete */
               printf("incomplete ip addr\n");
               goto invalid_arg;
            }

            /* second argument after '-mount' is destination, i.e. mount point */
            i++;
            if(i > argc || argv[i][0] == '-') {
               /* mount point missing */
               printf("mount point missing\n");
               goto invalid_arg;
            }
            else {
               /* save argc index of mount point */
               if(argv[i][0] == '/') {
                  /* path starts with '/', so copy as it is */
                  mount_point = i;
               }
               else {
                  printf("mount point should be absolute, i.e. should start with '/'\n");
                  goto invalid_arg;
               }
            }
         }
         else {
            /* argument for '-mount' is missing */
            printf("insufficient argument for -mount\n");
            goto invalid_arg;
         }
      } /* end of '-mount' argument parsing */
      else {
         printf("invalid argument '%s'\n", argv[i]);
         goto invalid_arg;
      }
   }

   /* TODO: check if server is available or not */
   /* TODO: check if url is valid on server or not */

   printf("mounting %s:%s to %s\n", SERVER_IP, SERVER_URL, argv[mount_point]);
   argv[1] = argv[mount_point];
   argv[2] = strdup("-d"); /* do not run in daemon mode */
   return fuse_main(3, argv, &masd_oper, NULL);

invalid_arg:
   printf("USAGE: %s -mount <source_ip:dir> <mount_point>\n", argv[0]);
   return 0;
}

