// the extent server implementation

#include "extent_server.h"

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <sstream>

extent_server::extent_server() {}

std::string extent_server::filename(extent_protocol::extentid_t id) {
  char buf[32];
  sprintf(buf, "./ID/%016llx", id);
  return std::string(buf, strlen(buf));
}

int extent_server::put(extent_protocol::extentid_t id, std::string buf, int &) {
  return extent_protocol::IOERR;
}

int extent_server::get(extent_protocol::extentid_t id, std::string &buf) {
  return extent_protocol::IOERR;
}

int extent_server::getattr(extent_protocol::extentid_t id,
                           extent_protocol::attr &a) {
  return extent_protocol::IOERR;
}

int extent_server::remove(extent_protocol::extentid_t id, int &) {
  return extent_protocol::IOERR;
}
