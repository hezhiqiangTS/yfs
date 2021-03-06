// RPC stubs for clients to talk to lock_server
// see lock_client.h for protocol details.

#include "lock_client.h"
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <iostream>
#include <sstream>
#include "rpc.h"

lock_client::lock_client(std::string xdst) {
  // create a rpc client and associate it with the lock_server's rpcs
  sockaddr_in dstsock;
  make_sockaddr(xdst.c_str(), &dstsock);
  cl = new rpcc(dstsock, true);
  if (cl->bind() < 0) {
    printf("lock_client: call bind\n");
  }

  pthread_cond_init(&lock_map_c, NULL);
  pthread_mutex_init(&lock_map_m, NULL);

  // get a random port number for the client's reverse rpc server
  struct timeval tv;
  gettimeofday(&tv, NULL);
  srandom(tv.tv_usec);
  int rlock_port = ((random() % 32000) | (0x1 << 10));

  std::ostringstream tmp;
  tmp << rlock_port;
  id = tmp.str();
  rpcs *rlsrpc = new rpcs(htons(rlock_port));
  // lock_client handles the grant rpc request from the lock_server
  rlsrpc->reg(lock_protocol::grant, this, &lock_client::grant);
}

int lock_client::stat(std::string name) {
  int r;
  int ret = cl->call(lock_protocol::stat, id, name, r);
  assert(ret == lock_protocol::OK);
  return r;
}

// try to acquire lock
lock_protocol::status lock_client::acquire(std::string name) {
  assert(pthread_mutex_lock(&lock_map_m) == 0);
  auto lock_pos = lock_map.find(name);
  std::shared_ptr<lock> lock_item;
  if (lock_pos == lock_map.end()) {  // new lock name
    lock_item = std::make_shared<lock>(name);
    lock_map.insert({name, lock_item});
  } else {
    lock_item = lock_pos->second;
  }
  assert(pthread_mutex_unlock(&lock_map_m) == 0);

  while (1) {
    assert(pthread_mutex_lock(&lock_map_m) == 0);
    pthread_mutex_trylock(&lock_item->lock_m);

    if (lock_item->s == lock_status::FREE) {
      lock_item->s = lock_status::LOCKED;
      pthread_mutex_unlock(&lock_item->lock_m);
      break;
    } else if (lock_item->s == lock_status::ABSENT) {
      int r;
      int ret = cl->call(lock_protocol::acquire, id, name, r);
      lock_item->s = lock_status::ACQUIRING;
      pthread_mutex_unlock(&lock_map_m);
      pthread_cond_wait(&lock_item->lock_c, &lock_item->lock_m);
    } else {
      int r;
      int ret = cl->call(lock_protocol::acquire, id, name, r);
      pthread_mutex_unlock(&lock_map_m);
      pthread_cond_wait(&lock_item->lock_c, &lock_item->lock_m);
    }
  }
  assert(pthread_mutex_unlock(&lock_map_m) == 0);

  return lock_protocol::OK;
}

// release lock
lock_protocol::status lock_client::release(std::string name) {
  int r;
  int ret = cl->call(lock_protocol::release, id, name, r);
  auto lock_item = lock_map.find(name)->second;
  pthread_mutex_lock(&lock_item->lock_m);
  lock_item->s = lock_status::ABSENT;
  pthread_mutex_unlock(&lock_item->lock_m);
  // /pthread_cond_signal(&lock_c);
  return lock_protocol::OK;
}

// modify lock stat to free
// signal thead waiting for lock
// registerd in rpcs of lock_client
lock_protocol::status lock_client::grant(std::string name, int &dummy) {
  auto lock = *(lock_map.find(name));
  lock.second->s = lock_status::FREE;
  assert(pthread_cond_signal(&lock.second->lock_c) == 0);
  return lock_protocol::OK;
}

/*
Multiple threads in the client program
can use the same lock_client object and request for the same lock name.
*/