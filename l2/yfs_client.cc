// yfs client.  implements FS operations using extent and lock server
#include "yfs_client.h"
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <iostream>
#include <sstream>
#include "extent_client.h"

yfs_client::yfs_client(std::string extent_dst, std::string lock_dst) {
  ec = new extent_client(extent_dst);
}

yfs_client::inum yfs_client::n2i(std::string n) {
  std::istringstream ist(n);
  unsigned long long finum;
  ist >> finum;
  return finum;
}

std::string yfs_client::filename(inum inum) {
  std::ostringstream ost;
  ost << inum;
  return ost.str();
}

bool yfs_client::isfile(inum inum) {
  if (inum & 0x80000000) return true;
  return false;
}

bool yfs_client::isdir(inum inum) { return !isfile(inum); }

int yfs_client::getfile(inum inum, fileinfo &fin) {
  int r = OK;
  std::string f = filename(inum);

  printf("getfile %016llx\n", inum);
  extent_protocol::attr a;
  if (ec->getattr(inum, a) != extent_protocol::OK) {
    r = IOERR;
    goto release;
  }

  fin.atime = a.atime;
  fin.mtime = a.mtime;
  fin.ctime = a.ctime;
  fin.size = a.size;
  printf("getfile %016llx -> sz %llu\n", inum, fin.size);

release:

  return r;
}

int yfs_client::getdir(inum inum, dirinfo &din) {
  int r = OK;
  std::string d = filename(inum);

  printf("getdir %016llx\n", inum);
  extent_protocol::attr a;
  if (ec->getattr(inum, a) != extent_protocol::OK) {
    r = IOERR;
    goto release;
  }
  din.atime = a.atime;
  din.mtime = a.mtime;
  din.ctime = a.ctime;

release:
  return r;
}
