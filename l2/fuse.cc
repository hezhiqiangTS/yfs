/*
 * receive request from fuse and call methods of yfs_client
 *
 * started life as low-level example in the fuse distribution.  we
 * have to use low-level interface in order to get i-numbers.  the
 * high-level interface only gives us complete paths.
 */

#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <fuse/fuse_lowlevel.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "yfs_client.h"

int myid;
yfs_client *yfs;

int id() { return myid; }

yfs_client::status getattr(yfs_client::inum inum, struct stat &st) {
  yfs_client::status ret;

  bzero(&st, sizeof(st));

  st.st_ino = inum;
  printf("getattr %016llx %d\n", inum, yfs->isfile(inum));
  if (yfs->isfile(inum)) {
    yfs_client::fileinfo info;
    ret = yfs->getfile(inum, info);
    if (ret != yfs_client::OK) return ret;
    st.st_mode = S_IFREG | 0666;
    st.st_nlink = 1;
    st.st_atime = info.atime;
    st.st_mtime = info.mtime;
    st.st_ctime = info.ctime;
    st.st_size = info.size;
    printf("   getattr -> %llu\n", info.size);
  } else {
    yfs_client::dirinfo info;
    ret = yfs->getdir(inum, info);
    if (ret != yfs_client::OK) return ret;
    st.st_mode = S_IFDIR | 0777;
    st.st_nlink = 2;
    st.st_atime = info.atime;
    st.st_mtime = info.mtime;
    st.st_ctime = info.ctime;
    printf("   getattr -> %lu %lu %lu\n", info.atime, info.mtime, info.ctime);
  }
  return yfs_client::OK;
}

void fuseserver_getattr(fuse_req_t req, fuse_ino_t ino,
                        struct fuse_file_info *fi) {
  struct stat st;
  yfs_client::inum inum = ino;  // req->in.h.nodeid;
  yfs_client::status ret;

  ret = getattr(inum, st);
  if (ret != yfs_client::OK) {
    fuse_reply_err(req, ENOENT);
    return;
  }
  fuse_reply_attr(req, &st, 0);
}

struct dirbuf {
  char *p;
  size_t size;
};

void dirbuf_add(struct dirbuf *b, const char *name, fuse_ino_t ino) {
  struct stat stbuf;
  size_t oldsize = b->size;
  b->size += fuse_dirent_size(strlen(name));
  b->p = (char *)realloc(b->p, b->size);
  memset(&stbuf, 0, sizeof(stbuf));
  stbuf.st_ino = ino;
  fuse_add_dirent(b->p + oldsize, name, &stbuf, b->size);
}

#define min(x, y) ((x) < (y) ? (x) : (y))

int reply_buf_limited(fuse_req_t req, const char *buf, size_t bufsize,
                      off_t off, size_t maxsize) {
  if (off < bufsize)
    return fuse_reply_buf(req, buf + off, min(bufsize - off, maxsize));
  else
    return fuse_reply_buf(req, NULL, 0);
}

void fuseserver_readdir(fuse_req_t req, fuse_ino_t ino, size_t size, off_t off,
                        struct fuse_file_info *fi) {
  yfs_client::inum inum = ino;  // req->in.h.nodeid;
  struct dirbuf b;
  yfs_client::dirent e;

  printf("fuseserver_readdir\n");

  if (!yfs->isdir(inum)) {
    fuse_reply_err(req, ENOTDIR);
    return;
  }

  memset(&b, 0, sizeof(b));

  // fill in the b data structure using dirbuf_add

  reply_buf_limited(req, b.p, b.size, off, size);
  free(b.p);
}

void fuseserver_statfs(fuse_req_t req) {
  struct statvfs buf;

  printf("statfs\n");

  memset(&buf, 0, sizeof(buf));

  buf.f_namemax = 255;
  buf.f_bsize = 512;

  fuse_reply_statfs(req, (const struct statfs *)&buf);
}

struct fuse_lowlevel_ops fuseserver_oper;

int main(int argc, char *argv[]) {
  char *mountpoint = 0;
  int err = -1;
  int fd;

  setvbuf(stdout, NULL, _IONBF, 0);

  if (argc != 4) {
    fprintf(stderr,
            "Usage: yfs_client <mountpoint> <port-extent-server> "
            "<port-lock-server>\n");
    exit(1);
  }
  mountpoint = argv[1];

  srandom(getpid());

  myid = random();

  yfs = new yfs_client(argv[2], argv[3]);

  fuseserver_oper.getattr = fuseserver_getattr;
  fuseserver_oper.statfs = fuseserver_statfs;
  fuseserver_oper.readdir = fuseserver_readdir;

  const char *fuse_argv[20];
  int fuse_argc = 0;
  fuse_argv[fuse_argc++] = argv[0];
#ifdef __APPLE__
  fuse_argv[fuse_argc++] = "-o";
  fuse_argv[fuse_argc++] = "nolocalcaches";  // no dir entry caching
  fuse_argv[fuse_argc++] = "-o";
  fuse_argv[fuse_argc++] = "daemon_timeout=86400";
#endif

  // everyone can play, why not?
  // fuse_argv[fuse_argc++] = "-o";
  // fuse_argv[fuse_argc++] = "allow_other";

  fuse_argv[fuse_argc++] = mountpoint;
  fuse_argv[fuse_argc++] = "-d";

  fuse_args args = FUSE_ARGS_INIT(fuse_argc, (char **)fuse_argv);
  int foreground;
  int res =
      fuse_parse_cmdline(&args, &mountpoint, 0 /*multithreaded*/, &foreground);
  if (res == -1) {
    fprintf(stderr, "fuse_parse_cmdline failed\n");
    return 0;
  }

  args.allocated = 0;

  fd = fuse_mount(mountpoint, (const char *)&(args.argv));
  if (fd == -1) {
    fprintf(stderr, "fuse_mount failed\n");
    exit(1);
  }

  struct fuse_session *se;

  se = fuse_lowlevel_new((const char *)&(args.argv), &fuseserver_oper,
                         sizeof(fuseserver_oper), NULL);
  if (se == 0) {
    fprintf(stderr, "fuse_lowlevel_new failed\n");
    exit(1);
  }

  struct fuse_chan *ch = fuse_kern_chan_new(fd);
  if (ch == NULL) {
    fprintf(stderr, "fuse_kern_chan_new failed\n");
    exit(1);
  }

  fuse_session_add_chan(se, ch);
  // err = fuse_session_loop_mt(se);   // FK: wheelfs does this; why?
  err = fuse_session_loop(se);

  fuse_session_destroy(se);
  close(fd);
  fuse_unmount(mountpoint);

  return err ? 1 : 0;
}
