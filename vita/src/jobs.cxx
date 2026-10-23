// Worker threads for scene setup. See jobs.hxx for why this exists.
//
// pthreads rather than std::thread: the Vita port links VitaSDK's newlib, whose
// pthread implementation maps onto SceKernel threads, so one implementation
// covers the Vita, Android and the host build with no per-platform branch. A
// small pool is created on demand and joined by WaitAll(); setup runs this a
// few times per scene, so thread creation is nothing next to the milliseconds of
// noise() it hides, and there is no idle pool to leak or to wake at exit.
#include "jobs.hxx"

#include <pthread.h>
#ifndef ELECTROBENCH_VITA
#include <unistd.h>
#endif

#include <atomic>
#include <mutex>
#include <utility>
#include <vector>

namespace jobs {
namespace {

// One queue, worked on by at most CoreCount()-1 threads plus (in WaitAll) the
// calling thread. Tasks are taken under the mutex, run outside it.
struct Pool {
  std::mutex mutex;
  std::vector<std::function<void()>> tasks;
  std::vector<pthread_t> threads;
  size_t next = 0;
  int alive = 0;   // worker threads that have not exited yet
  int limit = 0;   // how many workers may be started

  bool Take(std::function<void()> *out) {
    std::lock_guard<std::mutex> lock(mutex);
    if (next >= tasks.size()) return false;
    *out = std::move(tasks[next++]);
    return true;
  }

  void Work() {
    std::function<void()> task;
    while (Take(&task)) {
      task();
      task = nullptr;
    }
  }
};

Pool gPool;

void *Trampoline(void *self) {
  static_cast<Pool *>(self)->Work();
  std::lock_guard<std::mutex> lock(gPool.mutex);
  gPool.alive--;
  return nullptr;
}

}  // namespace

int CoreCount() {
#ifdef ELECTROBENCH_VITA
  return 4;  // the console's quad-core Cortex-A9
#else
  const long n = sysconf(_SC_NPROCESSORS_ONLN);
  return (int)(n < 2 ? 2 : (n > 8 ? 8 : n));
#endif
}

namespace {

void Enqueue(const std::function<void()> &task) {
  std::lock_guard<std::mutex> lock(gPool.mutex);
  gPool.tasks.push_back(task);
  if (gPool.limit == 0) gPool.limit = CoreCount() - 1;
  if (gPool.limit < 1) gPool.limit = 1;
  if (gPool.alive >= gPool.limit) return;  // the assigned workers will take it
  pthread_t thread;
  if (pthread_create(&thread, nullptr, Trampoline, &gPool) == 0) {
    gPool.threads.push_back(thread);
    gPool.alive++;
  }
  // If the thread could not be created the task simply stays queued and runs
  // on the calling thread in WaitAll(): slower, never wrong.
}

}  // namespace

void Start(int count, const std::function<void(int)> &task) {
  // The caller's function object may not outlive this call (a temporary lambda
  // is the normal case), so each task gets its own copy.
  std::function<void(int)> copy = task;
  for (int i = 0; i < count; i++) Enqueue([copy, i] { copy(i); });
}

void WaitAll() {
  gPool.Work();  // the calling thread is one of the cores
  std::vector<pthread_t> threads;
  {
    std::lock_guard<std::mutex> lock(gPool.mutex);
    threads.swap(gPool.threads);
  }
  for (pthread_t thread : threads) pthread_join(thread, nullptr);
  std::lock_guard<std::mutex> lock(gPool.mutex);
  gPool.tasks.clear();
  gPool.next = 0;
  gPool.limit = 0;
}

}  // namespace jobs
