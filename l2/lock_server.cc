// the caching lock server implementation

#include "lock_server.h"
#include <stdio.h>
#include <unistd.h>
#include <iostream>
#include <memory>
#include "rpc.h"

static void *grantthread(void *x) {
  lock_server *sc = (lock_server *)x;
  sc->granter();
  return 0;
}

lock_server::lock_server() : nacquire(0), lock_map() {
  pthread_t th;
  assert(pthread_mutex_init(&lock_map_m, 0) == 0);
  assert(pthread_mutex_init(&workq_m, 0) == 0);
  assert(pthread_cond_init(&lock_map_c, 0) == 0);
  assert(pthread_cond_init(&workq_c, 0) == 0);
  assert(pthread_create(&th, NULL, &grantthread, (void *)this) == 0);
}

static void *communicator(void *x) {
  auto lock_item = *(std::shared_ptr<lock> *)x;
  // std::cout << "[communicator thread] lock_name = " << lock_item->name <<
  // std::endl;
  pthread_mutex_lock(&lock_item->lock_m);
  if (lock_item->s == lock_status::LOCKED) {
    pthread_mutex_unlock(&lock_item->lock_m);
    return NULL;
  }
  if (lock_item->wq_empty()) {
    pthread_mutex_unlock(&lock_item->lock_m);
    return NULL;
  }

  // xclt 就是对应客户端中 rpcs *rlsrpc 的端口号
  std::string xclt = lock_item->wq_popfront();
  lock_item->s = lock_status::LOCKED;
  lock_item->owner = xclt;
  std::cout << "[communicator thread] lock " << lock_item->name
            << " is locked now" << std::endl;
  // 创建一个 rpcc
  sockaddr_in dstsock;
  make_sockaddr(xclt.c_str(), &dstsock);
  // std::cout << "[communicator thread] reverse client : " << xclt <<
  // std::endl;
  rpcc *cl = new rpcc(dstsock);
  if (cl->bind() < 0) {
    printf("lock_client: call bind\n");
  }

  // grant lock_name to clt
  int r;
  cl->call(lock_protocol::grant, lock_item->name, r);
  // std::cout << "[communicator thread] after sending grant RPC" << std::endl;

  pthread_cond_broadcast(&lock_item->waitq_c);
  pthread_mutex_unlock(&lock_item->lock_m);
  return NULL;
}

void lock_server::granter() {
  // This method should be a continuous loop, waiting for locks to become free
  // and then sending grant rpcs to those who are waiting for it.
  while (1) {
    pthread_mutex_lock(&workq_m);

    while (workq.size() == 0) {
      std::cout << "[granter thread] wait on condition" << std::endl;
      pthread_cond_wait(&workq_c, &workq_m);
    }
    // std::cout << "[granter thread] workq.size() = " << workq.size()
    //          << std::endl;
    for (auto lock_item : workq) {
      pthread_t th;
      pthread_create(&th, NULL, communicator, (void *)&lock_item);
      pthread_join(th, NULL);
    }
    workq.clear();
    pthread_mutex_unlock(&workq_m);
    // std::cout << "[lock_server::granter] lock_map.size():" << lock_map.size()
    // << std::endl;
  }
}

// 将 clt 放入 lock_item 的等待队列
// 将 lock_item 放入 workqueue
// 通知 granter 线程
void *helper_enq(void *arg) {
  lock_server::temp *pt = (lock_server::temp *)arg;
  auto lock_item = pt->lock_item;
  auto clt = pt->clt;
  auto ls = pt->ls;
  delete pt;

  pthread_mutex_lock(&lock_item->lock_m);

  // Uncomment codes below, multi threads can't arquire/release same lock_name
  // through same lock_client simultaneously
  // if (lock_item->s == lock_status::LOCKED && lock_item->owner == clt)
  //  return NULL;
  lock_item->_enq(clt);
  if (lock_item->s == lock_status::LOCKED) {
    pthread_mutex_unlock(&lock_item->lock_m);
    return NULL;
  }

  // lock_item is FREE

  pthread_mutex_unlock(&lock_item->lock_m);
  std::cout << "[helper_enq] "
            << "signal granter thread." << std::endl;
  pthread_mutex_lock(&ls->workq_m);
  ls->workq.push_back(lock_item);
  // signal granter thread to process workq
  pthread_cond_signal(&ls->workq_c);
  pthread_mutex_unlock(&ls->workq_m);
  return NULL;
}

// 将 clt 移出 lock_item 的等待队列
// 将 lock_item 放入 workqueue
// 通知 granter 线程
void *helper_deq(void *arg) {
  lock_server::temp *t = (lock_server::temp *)arg;
  std::shared_ptr<lock> lock_item = t->lock_item;
  std::string clt = t->clt;
  auto ls = t->ls;
  delete t;

  // 从 waitqueue 中删除 clt
  pthread_mutex_lock(&lock_item->waitq_m);
  for (auto item = lock_item->waitq.begin(); item != lock_item->waitq.end();
       item++) {
    if (*item == clt) {
      lock_item->waitq.erase(item);
      break;
    }
  }

  // 如果 lock is owned to clt
  // lock 状态改为 FREE
  // 加入 workq
  if (lock_item->owner != clt) {
    pthread_mutex_unlock(&lock_item->waitq_m);
    return NULL;
  }

  lock_item->s = lock_status::FREE;
  std::cout << "[helper deq] " << lock_item->name << " is released "
            << std::endl;
  assert(pthread_mutex_unlock(&lock_item->waitq_m) == 0);
  pthread_mutex_lock(&ls->workq_m);
  ls->workq.push_back(lock_item);
  pthread_mutex_unlock(&ls->workq_m);
  pthread_cond_signal(&ls->workq_c);
  return NULL;
}

lock_protocol::status lock_server::stat(std::string clt, std::string name,
                                        int &r) {
  lock_protocol::status ret = lock_protocol::OK;
  printf("stat request from clt %s for lock %s\n", clt.c_str(), name.c_str());
  r = nacquire;
  return ret;
}

lock_protocol::status lock_server::acquire(std::string clt,
                                           std::string lock_name, int &r) {
  std::cout << "[lock_server::acquire] acquire request from clt " << clt << ' '
            << "for lock " << lock_name << std::endl;
  r = 1;

  pthread_mutex_lock(&lock_map_m);
  // std::cout << "[lock_server::acquire] lock_map.size(): " << lock_map.size()
  // << std::endl;
  auto pos_lock = lock_map.find(lock_name);

  // new lock_name
  if (pos_lock == lock_map.cend()) {
    std::cout << "[lock_server::acquire] new lock_name :" << lock_name
              << std::endl;
    // 创建新 FREE 锁 lock_item
    auto lock_item = std::make_shared<lock>(lock_name);
    lock_map.insert({lock_name, lock_item});
    pthread_mutex_unlock(&lock_map_m);

    // 将 clt 放入 lock_item 的等待队列
    // 将 lock_item 放入 workqueue
    temp *pt = new temp(lock_item, clt, this);
    pthread_t th;
    pthread_create(&th, NULL, helper_enq, (void *)pt);
    pthread_detach(th);
    return lock_protocol::OK;
  }

  // old lock_name
  auto lock_item = (*pos_lock).second;
  pthread_t th;
  temp *pt = new temp(lock_item, clt, this);
  pthread_mutex_unlock(&lock_map_m);
  pthread_create(&th, NULL, helper_enq, (void *)pt);
  pthread_detach(th);
  return lock_protocol::OK;
}

lock_protocol::status lock_server::release(std::string clt,
                                           std::string lock_name, int &r) {
  std::cout << "[lock_server::release] release request from clt " << clt << ' '
            << "for lock " << lock_name << std::endl;

  pthread_mutex_lock(&lock_map_m);
  auto pos_lock = lock_map.find(lock_name);

  //  lock_name is not contained in lock_map
  if (pos_lock == lock_map.end()) {
    r = 1;
    pthread_mutex_unlock(&lock_map_m);
    return lock_protocol::OK;
  }

  auto lock_item = (*pos_lock).second;
  if (lock_item->s == lock_status::LOCKED) {
    if (lock_item->owner == clt) {
      std::cout << "[lock_server::release] clt " << clt << " release lock "
                << lock_name << " safely" << std::endl;
      lock_item->s = lock_status::FREE;
      lock_item->owner == "";
      if (lock_item->waitq.size() == 0) {
        lock_map.erase(lock_name);
        pthread_mutex_unlock(&lock_map_m);
        return lock_protocol::OK;
      }

      pthread_mutex_lock(&workq_m);
      workq.push_back(lock_item);
      pthread_cond_signal(&workq_c);
      pthread_mutex_unlock(&workq_m);
      pthread_mutex_unlock(&lock_map_m);
      return lock_protocol::OK;
    } else {
      // clt is releasing a locked lock not owened by itself.
      std::cout << "[lock_server::release] clt " << clt
                << " can't release lock " << lock_name << " , not owned."
                << std::endl;
      for (auto _clt = lock_item->waitq.begin(); _clt != lock_item->waitq.end();
           _clt++) {
        if (*_clt == clt) {
          lock_item->waitq.erase(_clt);
          break;
        }
      }
      pthread_mutex_unlock(&lock_map_m);
    }
  }

  // lock is FREE
  if (lock_item->s == lock_status::FREE && lock_item->waitq.size() == 0) {
    std::cout << "[lock_server::release] lock " << lock_item->name
              << " is FREE and its waitq is empty. " << std::endl;
    lock_map.erase(lock_name);
    r = 1;
    pthread_mutex_unlock(&lock_map_m);
    return lock_protocol::OK;
  }

  else if (lock_item->s == lock_status::FREE) {
    std::cout << "[lock_server::release] lock " << lock_item->name
              << " is FREE and its waitq is not empty. " << std::endl;
    for (auto item = lock_item->waitq.begin(); item != lock_item->waitq.end();
         item++) {
      if (*item == clt) {
        lock_item->waitq.erase(item);
        break;
      }
    }
    if (lock_item->waitq.size() == 0) {
      lock_map.erase(lock_name);
      pthread_mutex_unlock(&lock_map_m);
      return lock_protocol::OK;
    }

    pthread_t th;
    temp *t = new temp(lock_item, clt, this);
    pthread_mutex_unlock(&lock_map_m);
    pthread_create(&th, NULL, helper_enq, t);
    pthread_detach(th);
    return lock_protocol::OK;
  }
}
