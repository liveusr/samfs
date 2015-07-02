#include <sys/ipc.h>
#include <sys/shm.h>

#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include <pthread.h>
#include <semaphore.h>

#include "samfs_common.h"

static char    SRCPATH[URL_LEN];
static fd_set  select_fds; /* this fd set stores fds of client connected using select */
static fd_set  thread_fds; /* this fd set stores fds of client connected using select,
                              used by child process to close non-required, while using fork.
                            */

/* different types of concurrency method used by this server */
enum {
   SAM_PTHREAD,
   SAM_FORK,
   SAM_SELECT,
   SAM_UNDEFINED
};

/* structure to maintain statistics and status */
struct sam_status_t {
   sem_t          mutex;                  /* mutex to protect updation of variables below variables */
   char           server_name[URL_LEN];   /* name of server binary */
   char           server_ip[32];          /* ip on which server is running */
   char           server_dir[URL_LEN];    /* source directory which is exported by server */
   int            server_pid;             /* pid of server process */
   unsigned int   conc_method;            /* type of concurrency method being applied */
   unsigned int   select_count;           /* number of clients connected using select */
   unsigned int   thread_count;           /* number of clients connected using pthread */
   unsigned int   forked_count;           /* number of clients connected using fork */
   unsigned int   bytes_rcvd;             /* total number of bytes received */
   unsigned int   bytes_sent;             /* total number of bytes sent */
   unsigned int   uplink_rate;            /* uplink data rate */
   unsigned int   dnlink_rate;            /* downlink data rate */
   unsigned int   uplink_avg;             /* average uplink data rate */
   unsigned int   dnlink_avg;             /* average downlink data rate */
} *sam_stat;

static int read_req(int sock_fd, struct req_t *req)
{
   int rv;
   int magic;

   rv = read(sock_fd, req, sizeof(struct req_t));
   magic = req->magic;
   write(sock_fd, &magic, sizeof(magic));
   
   sem_wait(&sam_stat->mutex);
   sam_stat->bytes_rcvd += rv;
   sam_stat->dnlink_rate += rv;
   sem_post(&sam_stat->mutex);

   return rv;
}

static int send_rsp(int sock_fd, struct rsp_t *rsp)
{
   int rv;
   int magic;

   magic = 0;
   rsp->magic = rand();
   rv = write(sock_fd, rsp, sizeof(struct rsp_t));
   read(sock_fd, &magic, sizeof(magic));
   if(rsp->magic != magic) {
      printf("ERROR IN WRITE: INVALID MAGIC\n");
   }
   
   sem_wait(&sam_stat->mutex);
   sam_stat->bytes_sent += rv;
   sam_stat->uplink_rate += rv;
   sem_post(&sam_stat->mutex);
   
   return rv;
}

static int handle_getattr(int client_fd, struct req_t *req)
{
   int            rv;
   char           local_path[URI_LEN];
   struct stat    st;
   struct rsp_t   rsp;

   sprintf(local_path, "%s%s%s", SRCPATH, req->url, req->uri);

   rv = lstat(local_path, &st);
   if(0 == rv) {
      rsp.status = SUCCESS;
      memcpy(&rsp.data, &st, sizeof(struct stat));
   }
   else {
      rsp.status = FAIL;
      rsp.errcode = errno;
   }
   rsp.endofdata = TRUE;

   send_rsp(client_fd, &rsp);

   return 0;
}

static int handle_mkdir(int client_fd, struct req_t *req)
{
   int            rv;
   char           local_path[URI_LEN];
   struct rsp_t   rsp;

   sprintf(local_path, "%s%s%s", SRCPATH, req->url, req->uri);

   rv = mkdir(local_path, req->mode);
   if(0 == rv) {
      rsp.status = SUCCESS;
   }
   else {
      rsp.status = FAIL;
      rsp.errcode = errno;
   }
   rsp.endofdata = TRUE;

   send_rsp(client_fd, &rsp);

   return 0;
}

static int handle_readdir(int client_fd, struct req_t *req)
{
   int            rv;
   char           local_path[URI_LEN];
   struct rsp_t   rsp;
   DIR            *dirp;
   struct dirent  dent;
   struct dirent  *next_dent;

   sprintf(local_path, "%s%s%s", SRCPATH, req->url, req->uri);

   dirp = opendir(local_path);
   if(NULL == dirp) {
      rsp.status = FAIL;
      rsp.errcode = ENOENT;
      rsp.endofdata = TRUE;
      send_rsp(client_fd, &rsp);
      return 0;
   }

   do {
      rv = readdir_r(dirp, &dent, &next_dent);
      if(rv != 0) {
         rsp.status = FAIL;
         rsp.endofdata = TRUE;
         rsp.errcode = rv;
      }
      else if(next_dent) {
         memcpy(&rsp.data, &dent, sizeof(struct dirent));
         rsp.status = SUCCESS;
         rsp.endofdata = (next_dent)? FALSE: TRUE;
      }
      else {
         rsp.status = FAIL;
         rsp.endofdata = TRUE;
         rsp.errcode = 0;
      }
      send_rsp(client_fd, &rsp);
   } while(next_dent);

   closedir(dirp);

   return 0;
}

static int handle_rmdir(int client_fd, struct req_t *req)
{
   int            rv;
   char           local_path[URI_LEN];
   struct rsp_t   rsp;

   sprintf(local_path, "%s%s%s", SRCPATH, req->url, req->uri);

   rv = rmdir(local_path);
   if(0 == rv) {
      rsp.status = SUCCESS;
   }
   else {
      rsp.status = FAIL;
      rsp.errcode = errno;
   }
   rsp.endofdata = TRUE;

   send_rsp(client_fd, &rsp);

   return 0;
}

static int handle_create(int client_fd, struct req_t *req)
{
   int            fd;
   char           local_path[URI_LEN];
   struct rsp_t   rsp;

   sprintf(local_path, "%s%s%s", SRCPATH, req->url, req->uri);

   fd = open(local_path, req->flags | O_WRONLY | O_CREAT);
   if(-1 == fd) {
      rsp.status = FAIL;
      rsp.errcode = errno;
   }
   else {
      close(fd);
      chmod(local_path, req->mode);
      rsp.status = SUCCESS;
   }
   rsp.endofdata = TRUE;

   send_rsp(client_fd, &rsp);

   return 0;
}

static int handle_read(int client_fd, struct req_t *req)
{
   int            rv;
   char           local_path[URI_LEN];
   struct rsp_t   rsp;
   int            fd;
   int            read_size;
   int            total_read;

   sprintf(local_path, "%s%s%s", SRCPATH, req->url, req->uri);

   fd = open(local_path, O_RDONLY);
   if(-1 == fd) {
      rsp.status = FAIL;
      rsp.errcode = errno;
      rsp.endofdata = TRUE;
      send_rsp(client_fd, &rsp);
      return 0;
   }

   if(req->offset) {
      rv = lseek(fd, req->offset, SEEK_SET);
      if(-1 == rv) {
         rsp.status = FAIL;
         rsp.errcode = errno;
         rsp.endofdata = TRUE;
         send_rsp(client_fd, &rsp);
         return 0;
      }
   }

   /* read minimum of 'buffer size' and 'requested size' */
   read_size = (sizeof(rsp.data) < req->size)? sizeof(rsp.data): req->size;
   total_read = 0;
   do {
      rv = read(fd, &rsp.data, read_size);
      total_read += rv;
      if(rv < 0) {
         /* error has occured while reading from file */
         rsp.status = FAIL;
         rsp.endofdata = TRUE;
         rsp.errcode = errno;
         read_size = 0; /* nothing to read, break the loop */
      }
      else if(rv < read_size) {
         /* we have reached end of file */
         rsp.status = SUCCESS;
         rsp.endofdata = TRUE;
         rsp.size = rv;
         read_size = 0; /* nothing to read, break the loop */
      }
      else if(total_read == req->size) {
         /* read requested size of data */
         rsp.status = SUCCESS;
         rsp.endofdata = TRUE;
         rsp.size = rv;
         read_size = 0; /* nothing to read, break the loop */
      }
      else {
         rsp.status = SUCCESS;
         rsp.endofdata = FALSE;
         rsp.size = rv;
         /* read minimum of 'buffer size' and 'remaining requested size' */
         read_size = (sizeof(rsp.data) < (req->size - total_read))? sizeof(rsp.data): (req->size - total_read);
      }
      send_rsp(client_fd, &rsp);
   } while(read_size);

   close(fd);

   return 0;
}

static int handle_write(int client_fd, struct req_t *req)
{
   int            rv;
   char           local_path[URI_LEN];
   struct rsp_t   rsp;
   int            fd;
   struct req_t   dreq;
   int            total_write;

   sprintf(local_path, "%s%s%s", SRCPATH, req->url, req->uri);

   fd = open(local_path, O_WRONLY);
   if(-1 == fd) {
      rsp.status = FAIL;
      rsp.errcode = errno;
      rsp.endofdata = TRUE;
      send_rsp(client_fd, &rsp);
      return 0;
   }

   if(req->offset) {
      rv = lseek(fd, req->offset, SEEK_SET);
      if(-1 == rv) {
         rsp.status = FAIL;
         rsp.errcode = errno;
         rsp.endofdata = TRUE;
         send_rsp(client_fd, &rsp);
         return 0;
      }
   }
 
   /* send rsp just to tell server is ready to recv file data */
   rsp.status = SUCCESS;
   send_rsp(client_fd, &rsp);

   /* recv data from client */
   total_write = 0;
   rv = 0;
   do {
      read_req(client_fd, &dreq);
      if(rv >= 0) {
         rv = write(fd, &dreq.data, dreq.size);
         if(rv < 0) {
            rsp.status = FAIL;
            rsp.errcode = errno;
            rsp.endofdata = TRUE;
         }
         else {
            total_write += rv;
            rsp.status = SUCCESS;
            rsp.endofdata = TRUE;
            rsp.size = total_write;
         }
      }
   } while(!dreq.endofdata);

   /* send client write status */
   send_rsp(client_fd, &rsp);

   close(fd);
 
   return 0;
}

static int handle_truncate(int client_fd, struct req_t *req)
{
   int            rv;
   char           local_path[URI_LEN];
   struct rsp_t   rsp;

   sprintf(local_path, "%s%s%s", SRCPATH, req->url, req->uri);

   rv = truncate(local_path, req->truncate_len);
   if(0 == rv) {
      rsp.status = SUCCESS;
   }
   else {
      rsp.status = FAIL;
      rsp.errcode = errno;
   }
   rsp.endofdata = TRUE;

   send_rsp(client_fd, &rsp);

   return 0;
}

static int handle_unlink(int client_fd, struct req_t *req)
{
   int            rv;
   char           local_path[URI_LEN];
   struct rsp_t   rsp;

   sprintf(local_path, "%s%s%s", SRCPATH, req->url, req->uri);

   rv = unlink(local_path);
   if(0 == rv) {
      rsp.status = SUCCESS;
   }
   else {
      rsp.status = FAIL;
      rsp.errcode = errno;
   }
   rsp.endofdata = TRUE;

   send_rsp(client_fd, &rsp);

   return 0;
}

static int handle_rename(int client_fd, struct req_t *req)
{
   int            rv;
   char           local_path[URI_LEN];
   char           new_path[URI_LEN];
   struct rsp_t   rsp;

   sprintf(local_path, "%s%s%s", SRCPATH, req->url, req->uri);
   sprintf(new_path, "%s%s%s", SRCPATH, req->url, req->data);

   rv = rename(local_path, new_path);
   if(0 == rv) {
      rsp.status = SUCCESS;
   }
   else {
      rsp.status = FAIL;
      rsp.errcode = errno;
   }
   rsp.endofdata = TRUE;

   send_rsp(client_fd, &rsp);

   return 0;
}

static int handle_chmod(int client_fd, struct req_t *req)
{
   int            rv;
   char           local_path[URI_LEN];
   struct rsp_t   rsp;

   sprintf(local_path, "%s%s%s", SRCPATH, req->url, req->uri);

   rv = chmod(local_path, req->mode);
   if(0 == rv) {
      rsp.status = SUCCESS;
   }
   else {
      rsp.status = FAIL;
      rsp.errcode = errno;
   }
   rsp.endofdata = TRUE;

   send_rsp(client_fd, &rsp);

   return 0;
}

static int handle_utime(int client_fd, struct req_t *req)
{
   int            rv;
   char           local_path[URI_LEN];
   struct utimbuf tm;
   struct rsp_t   rsp;

   sprintf(local_path, "%s%s%s", SRCPATH, req->url, req->uri);

   rv = utime(local_path, &tm);
   if(0 == rv) {
      rsp.status = SUCCESS;
      memcpy(&rsp.data, &tm, sizeof(struct utimbuf));
   }
   else {
      rsp.status = FAIL;
      rsp.errcode = errno;
   }
   rsp.endofdata = TRUE;

   send_rsp(client_fd, &rsp);

   return 0;
}

static int handle_statfs(int client_fd, struct req_t *req)
{
   int            rv;
   char           local_path[URI_LEN];
   struct statvfs st;
   struct rsp_t   rsp;

   sprintf(local_path, "%s%s%s", SRCPATH, req->url, req->uri);

   rv = statvfs(local_path, &st);
   if(0 == rv) {
      rsp.status = SUCCESS;
      memcpy(&rsp.data, &st, sizeof(struct statvfs));
   }
   else {
      rsp.status = FAIL;
      rsp.errcode = errno;
   }
   rsp.endofdata = TRUE;

   send_rsp(client_fd, &rsp);

   return 0;
}

static int process_req(int client_fd, struct req_t *req)
{
   switch(req->msg) {
      case GETATTR:
         handle_getattr(client_fd, req);
         break;
      case MKDIR:
         handle_mkdir(client_fd, req);
         break;
      case READDIR:
         handle_readdir(client_fd, req);
         break;
      case RMDIR:
         handle_rmdir(client_fd, req);
         break;
      case CREATE:
         handle_create(client_fd, req);
         break;
      case READ:
         handle_read(client_fd, req);
         break;
      case WRITE:
         handle_write(client_fd, req);
         break;
      case TRUNCATE:
         handle_truncate(client_fd, req);
         break;
      case UNLINK:
         handle_unlink(client_fd, req);
         break;
      case RENAME:
         handle_rename(client_fd, req);
         break;
      case CHMOD:
         handle_chmod(client_fd, req);
         break;
      case UTIME:
         handle_utime(client_fd, req);
         break;
      case STATFS:
         handle_statfs(client_fd, req);
         break;
      default:
         break;
   }

   return 0;
}

static char *string_rate(int rate, char *srate)
{
   float frate;
   int level;
   char suffix[][8] = {"b/s", "kb/s", "mb/s", "gb/s", "", "", "", ""};

   frate = rate * 8; /* convert it to bits */
   level = 0;
   while(frate >= 512.0f) {
      frate = frate / 1024;
      level++;
   }  

   sprintf(srate, "%.2f %s", frate, suffix[level]);

   return srate;
}

static void print_stats(void)
{
   char uprate[16], dnrate[16];

   while(1) {
      printf("\x1b[H\x1b[2J"); /* clears screen */

      if(sam_stat->uplink_avg)
         sam_stat->uplink_avg = ((sam_stat->uplink_avg * 2) + sam_stat->uplink_rate) / 3;
      else
         sam_stat->uplink_avg = sam_stat->uplink_rate;

      if(sam_stat->dnlink_avg)
         sam_stat->dnlink_avg = ((sam_stat->dnlink_avg * 2) + sam_stat->dnlink_rate) / 3;
      else
         sam_stat->dnlink_avg = sam_stat->dnlink_rate;

      printf("\n");
      printf("   +--------------------------------------------------------------------------+\n");
      printf("   |                             Server Dashboard                             |\n");
      printf("   +--------------------------------------------------------------------------+\n");
      printf("   | Server Binary : %-27s Server PID : %15d |\n", sam_stat->server_name, sam_stat->server_pid);
      printf("   | Source Dir    : %-27s Server IP  : %15s |\n", sam_stat->server_dir, sam_stat->server_ip);
      printf("   +--------------------------------------------------------------------------+\n");
      printf("   | Concurrency Method Being Used : ");
      switch(sam_stat->conc_method) {
         case SAM_SELECT:
            printf("%-40s |\n", "select");
            break;
         case SAM_PTHREAD:
            printf("%-40s |\n", "pthread");
            break;
         case SAM_FORK:
            printf("%-40s |\n", "fork");
            break;
         default:
            printf("%-40s |\n", "");
            break;
      }
      printf("   +--------------------------------------------------------------------------+\n");
      printf("   | Clients Connected Using _                                                |\n");
      printf("   | select() : %-10u     pthread() : %-10u     fork() : %-10u |\n",
            sam_stat->select_count, sam_stat->thread_count, sam_stat->forked_count);
      printf("   |                                                                          |\n");
      printf("   | Total Connected Clients : %-46u |\n",
            sam_stat->select_count + sam_stat->thread_count + sam_stat->forked_count);
      printf("   +--------------------------------------------------------------------------+\n");
#if 0
      printf("   | Total Bytes Received : %11u       Total Bytes Sent  : %11u |\n", sam_stat->bytes_rcvd, sam_stat->bytes_sent);
      //printf("   | Inst. Downlink Rate  : %11u       Inst. Uplink Rate : %11u |\n", sam_stat->dnlink_rate, sam_stat->uplink_rate);
      //printf("   | Avg. Downlink Rate   : %11u       Avg. Uplink Rate  : %11u |\n", sam_stat->dnlink_avg, sam_stat->uplink_avg);
      printf("   | Inst. Downlink Rate  : %11s       Inst. Uplink Rate : %11s |\n", 
            string_rate(sam_stat->dnlink_rate, dnrate), string_rate(sam_stat->uplink_rate, uprate));
      printf("   | Avg. Downlink Rate   : %11s       Avg. Uplink Rate  : %11s |\n", 
            string_rate(sam_stat->dnlink_avg, dnrate), string_rate(sam_stat->uplink_avg, uprate));
#else
      printf("   | Total Bytes Received : %11u        Total Bytes Sent : %11u |\n", sam_stat->bytes_rcvd, sam_stat->bytes_sent);
      printf("   | Downlink Data Rate   : %11s        Uplink Data Rate : %11s |\n", 
            string_rate(sam_stat->dnlink_rate, dnrate), string_rate(sam_stat->uplink_rate, uprate));
#endif
      printf("   +--------------------------------------------------------------------------+\n");
      printf("\n");

      sem_wait(&sam_stat->mutex);
      sam_stat->uplink_rate = 0;
      sam_stat->dnlink_rate = 0;
      sem_post(&sam_stat->mutex);

      sleep(1); /* update after every one second */
   }
}

static void *handle_client_thread(void *data)
{
   int client_fd;
   struct req_t req;

   client_fd = (int) data;

   sem_wait(&sam_stat->mutex);
   sam_stat->thread_count++;
   sem_post(&sam_stat->mutex);

   read_req(client_fd, &req);
   process_req(client_fd, &req);
   
   close(client_fd);
   FD_CLR(client_fd, &thread_fds);
   
   sem_wait(&sam_stat->mutex);
   sam_stat->thread_count--;
   sem_post(&sam_stat->mutex);

   return NULL;
}

static int handle_client_fork(int client_fd)
{
   struct req_t   req;
   int            curr_fd;

   if(fork() == 0) {
      /* inside child */
      
      sem_wait(&sam_stat->mutex);
      sam_stat->forked_count++;
      sem_post(&sam_stat->mutex);
   
      /* close all other opened fds */
      for(curr_fd = 0; curr_fd < FD_SETSIZE; curr_fd++) {
         if(FD_ISSET(curr_fd, &select_fds)) {
            close(curr_fd);
         }
         if(FD_ISSET(curr_fd, &thread_fds)) {
            close(curr_fd);
         }
      }
      read_req(client_fd, &req);
      process_req(client_fd, &req);
      close(client_fd);
      
      sem_wait(&sam_stat->mutex);
      sam_stat->forked_count--;
      sem_post(&sam_stat->mutex);
      
      //return 0;
      usleep(10);
      exit(0);
   }
   else {
      /* inside parent */
      close(client_fd);
   }

   return 0;
}

static int connect_to_client(int server_fd)
{
   int                  client_fd;
   struct sockaddr_in   sock_client;
   int                  client_len;

   client_len = sizeof(struct sockaddr_in);
   client_fd = accept(server_fd, (struct sockaddr *)&sock_client, (socklen_t *)&client_len);

   return client_fd;
}

static int accept_new_connection(int server_fd)
{
   int            client_fd;
   pthread_t      thread;
   pthread_attr_t attr;
   int            rv;

   pthread_attr_init(&attr);
   pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

   client_fd = connect_to_client(server_fd);
   switch(sam_stat->conc_method) {
      case SAM_SELECT:
         FD_SET(client_fd, &select_fds);
         sem_wait(&sam_stat->mutex);
         sam_stat->select_count++;
         sem_post(&sam_stat->mutex);
         break;
      case SAM_PTHREAD:
         FD_SET(client_fd, &thread_fds);
         rv = pthread_create(&thread, &attr, handle_client_thread, (void *) client_fd);
         if(rv != 0) {
            perror("pthread_create :");
         }
         break;
      case SAM_FORK:
         handle_client_fork(client_fd);
         break;
      default: break;
   }

   return 0;
}

static int create_server(char *SERVER_IP)
{
   int                  server_fd;
   struct sockaddr_in   sock_server;
   int                  optval;
   int                  rv;

   /* create a socket for TCP connection */
   server_fd = socket(AF_INET, SOCK_STREAM, 0);

   optval = 1;
   setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

   /* bind address to the socket created above */
   sock_server.sin_family = AF_INET;
   sock_server.sin_addr.s_addr = inet_addr(SERVER_IP);
   sock_server.sin_port = htons(SERVER_PORT);
   rv = bind(server_fd, (struct sockaddr *)&sock_server, sizeof(struct sockaddr));
   if(-1 == rv) {
      perror("bind() returned error :");
      return rv;
   }

   /* ready to listen for incoming connection */
   listen(server_fd, 100);

   return server_fd;
}

static void *connect_to_shm(char *argv)
{
   int            rv;
   FILE           *fp;
   int            pid;
   char           pid_path[80];
   struct stat    st;
   int            shmid;
   key_t          shmkey;
   void           *shm;

   /* if server is running, its pid is stored in '/tmp/.samd' file.
      try to open that file to find if server is already running or not.
    */
   fp = fopen("/tmp/.samd", "r");

   if(fp) {
      /* read pid from that file */
      fscanf(fp, "%d", &pid);
      fclose(fp);

      /* check if process with that pid exists or not */
      sprintf(pid_path, "/proc/%d", pid);
      rv = lstat(pid_path, &st);
      if(0 == rv) {
         //printf("%s :: Server with pid %d already running.\n", argv, pid);
         /* server pid is used as shm key */
         shmkey = pid; 
      }
      else {
         //printf("%s :: No such server exists with pid %d.\n", argv, pid);
         /* no such server exists, that file must 
            have been created by old server.
            new shm has to be created.
            use server pid as shm key.
          */
         shmkey = getpid();
      }
   }
   else {
      /* looks like first time server is being run.
         new shm has to be created.
         use server pid as shm key.
       */
      shmkey = getpid();
   }

   /* attach to shared memory */
   shmid = shmget(shmkey, sizeof(struct sam_status_t), IPC_CREAT | 0666);
   if(shmid < 0) {
      perror("shmget :");
      return NULL;
   }
   shm = shmat(shmid, NULL, 0);
   if(shm == ((void *) -1)) {
      perror("shmat :");
      return NULL;
   }

   return shm;
}

int main(int argc, char *argv[])
{
   int            server_fd;
   int            i;
   char           start_server;
   fd_set         read_fds;
   struct req_t   req;
   int            rv;
   int            curr_fd;

   if(argc == 1) {
      printf("USAGE: %s <server_ip> <source_path>\n", argv[0]);
      return 0;
   }

   /* connect to shared memory.
      server stats are maintained in shared memory.
    */
   sam_stat = connect_to_shm(argv[0]);
   if(sam_stat == NULL) {
      printf("%s :: Error connecting to shared memory.\n", argv[0]);
      return 0;
   }

   /* parse command line arguments */
   start_server = FALSE;
   for(i = 1; i < argc; i++) {
      if(strcmp(argv[i], "-status") == 0) {
         print_stats();
      }
      else if(strcmp(argv[i], "-export") == 0) {
         if((i + 2) < argc) {
            start_server = TRUE;

            strcpy(sam_stat->server_name, argv[0]);
            strcpy(sam_stat->server_ip, argv[i + 1]);
            strcpy(sam_stat->server_dir, argv[i + 2]);
            sam_stat->server_pid = getpid();
         }
         else {
            printf("%s :: Insufficient arguments: '%s'\n", argv[0], argv[i]);
            return 0;
         }
         i += 2; /* -export consumed two arguments, so increment by two */
      }
      else if(strcmp(argv[i], "-cmethod") == 0) {
         if((i + 1) < argc) {
            if(strcmp(argv[i + 1], "fork") == 0) {
               printf("Concurrency method updated to '%s'.\n", argv[i + 1]);
               sam_stat->conc_method = SAM_FORK;
            }
            else if(strcmp(argv[i + 1], "pthread") == 0) {
               printf("Concurrency method updated to '%s'.\n", argv[i + 1]);
               sam_stat->conc_method = SAM_PTHREAD;
            }
            else if(strcmp(argv[i + 1], "select") == 0) {
               printf("Concurrency method updated to '%s'.\n", argv[i + 1]);
               sam_stat->conc_method = SAM_SELECT;
            }
            else {
               if(argv[i + 1][0] == '-') {
                  printf("%s :: Insufficient arguments: '%s'.\n", argv[0], argv[i]);
               }
               else {
                  printf("%s :: Invalid concurrency method: '%s'.\n", argv[0], argv[i + 1]);
               }
               return 0;
            }
         }
         else {
            printf("%s :: Insufficient arguments: '%s'.\n", argv[0], argv[i]);
            return 0;
         }
         i += 1; /* -cmethod consumed two arguments, so increment by two */
      }
      else {
         printf("invalid argument: '%s'\n", argv[i]);
         return 0;
      }
   } /* end of command line args parsing */

   if(!start_server) {
      /* don't want to run server? then exit */
      return 0;
   }

   /* starting server, so write its pid in file '/tmp/.samd'.
      this pid is used as shm key while creating/accessing shm.
    */
   FILE *fp;
   fp = fopen("/tmp/.samd", "w");
   fprintf(fp, "%d", getpid());
   fclose(fp);

   /* start server */
   server_fd = create_server(sam_stat->server_ip);
   strcpy(SRCPATH, sam_stat->server_dir);

   /* reset concurrency method if it is garbage */
   if(sam_stat->conc_method >= SAM_UNDEFINED) {
      sam_stat->conc_method = SAM_PTHREAD;
   }

   /* setup fd set */
   FD_ZERO(&select_fds);
   FD_ZERO(&thread_fds);
   FD_SET(server_fd, &select_fds);
   FD_SET(server_fd, &thread_fds);

   /* reset variables */
   sam_stat->select_count = 0;
   sam_stat->thread_count = 0;
   sam_stat->forked_count = 0;
   sam_stat->bytes_rcvd = 0;
   sam_stat->bytes_sent = 0;
   sam_stat->uplink_rate = 0;
   sam_stat->dnlink_rate = 0;
   sam_stat->uplink_avg = 0;
   sam_stat->dnlink_avg = 0;

   /* initialize semaphore */
   sem_init(&sam_stat->mutex, 1, 1);

   printf("Server started with pid %d, listening on IP %s and exporting %s ..\n",
         sam_stat->server_pid, sam_stat->server_ip, sam_stat->server_dir);

   /* server main loop */
   while(1) {
      /* if select fd list not empty, check if any fd has data */
      if(sam_stat->select_count > 0 || sam_stat->conc_method == SAM_SELECT) {
         read_fds = select_fds;
         rv = select(FD_SETSIZE, &read_fds, NULL, NULL, NULL);
         for(curr_fd = 0; curr_fd < FD_SETSIZE; curr_fd++) {
            if(FD_ISSET(curr_fd, &read_fds)) {
               if(curr_fd == server_fd) {
                  accept_new_connection(server_fd);
               }
               else {
                  rv = read_req(curr_fd, &req);
                  if(rv > 0) {
                     process_req(curr_fd, &req);
                  }
                  close(curr_fd);
                  FD_CLR(curr_fd, &select_fds);

                  sem_wait(&sam_stat->mutex);
                  sam_stat->select_count--;
                  sem_post(&sam_stat->mutex);
               }
            }
         }
      } /* select fd set not empty */
      else {
         accept_new_connection(server_fd);
      }
   } /* main while loop */

   /* close server */
   sleep(1); /* wait for client to close the connection first! */
   close(server_fd);

   return 0;
}

