// #define USE_CILK 0

#include <stdio.h>
#include <unistd.h> // sleep
#include <stdlib.h> // exit

#define DIE(...) do {                                              \
  printf("Error detected at line %d in %s\n", __LINE__, __FILE__); \
  printf(__VA_ARGS__);                                             \
  exit(1);                                                         \
} while(0)

#include <pthread.h>

//! Mutual exclusion lock wrapper
typedef pthread_mutex_t Mutex_t;

//! Thread syncronization condition wrapper
typedef pthread_cond_t Cond_t;

void MutexInit(Mutex_t* m)
{
  int e = pthread_mutex_init(m, NULL);
  if( e != 0 ) {
    DIE("pthread_mutex_init returned %d", e);
  }
}

void MutexDestroy(Mutex_t* m)
{
  int e = pthread_mutex_destroy(m);
  if( e != 0 ) {
    DIE("pthread_mutex_destroy returned %d", e);
  }
}

void MutexLock(Mutex_t* m)
{
  int e = pthread_mutex_lock(m);
  if( e != 0 ) {
    DIE("pthread_mutex_lock returned %d", e);
  }
}

void MutexUnlock(Mutex_t* m)
{
  int e = pthread_mutex_unlock(m);
  if( e != 0 ) {
    DIE("pthread_mutex_unlock returned %d", e);
  }
}

void CondInit(Cond_t* c)
{
  int e = pthread_cond_init(c, NULL);
  if( e != 0 ) {
    DIE("pthread_cond_init returned %d", e);
  }
}

void CondDestroy(Cond_t* c)
{
  int e = pthread_cond_destroy(c);
  if( e != 0 ) {
    DIE("pthread_cond_destroy returned %d", e);
  }
}

void CondSignal(Cond_t* c)
{
  int e = pthread_cond_signal(c);
  if( e != 0 ) {
    DIE("pthread_cond_signal returned %d", e);
  }
}

void CondWait(Cond_t* c, Mutex_t* m)
{
  int e = pthread_cond_wait(c, m);
  if( e != 0 ) {
    DIE("pthread_cond_wait returned %d", e);
  }
}

#include <list>

class ThreadIDPool {
private:
  std::list<int> indexList; // First in first out queue
  Mutex_t lock;
  Cond_t pool_not_empty_condition;

public:
  void initializePool(const int n_thread)
  {
    indexList.clear();
    for (int i = 0; i < n_thread; i++)
      indexList.push_back(i);
    MutexInit(&lock);
    CondInit(&pool_not_empty_condition);
  }

  void destroyPool()
  {
    indexList.clear();
    MutexDestroy(&lock);
    CondDestroy(&pool_not_empty_condition);
  }

  int getIndexBlocking()
  {
    int value = -1;
    MutexLock(&lock);
    while (indexList.empty())
      CondWait(&pool_not_empty_condition, &lock);

    value = indexList.front();
    indexList.pop_front();

    MutexUnlock(&lock);
    return value;
  }

  void returnIndex(const int i)
  {
    MutexLock(&lock);
    indexList.push_back(i);
    CondSignal(&pool_not_empty_condition);
    MutexUnlock(&lock);
  }

} threadIDpool; // This is a global variable!

void thread_id_pool_initialize(const int n_thread)
{ threadIDpool.initializePool(n_thread); }

void thread_id_pool_destroy()
{ threadIDpool.destroyPool(); }

int thread_id_pool_get_index()
{ return threadIDpool.getIndexBlocking(); }

void thread_id_pool_return_index(const int i)
{ threadIDpool.returnIndex(i); }

// This is necessary when using openmp to lunch tasks.
// We need to lunch task with "omp single" to make sure we lunch one task for one time;
// inside a task we may want to lunch other tasks / omp for,
// and these tasks will be sequential if we didn't turn omp_nested on.
#if defined __INTEL_COMPILER && __INTEL_COMPILER_BUILD_DATE > 20200101
  #define OMP_SET_NESTED_LEVEL(n) omp_set_max_active_levels(n)
#else
  #define OMP_SET_NESTED_LEVEL(n) omp_set_nested(n)
#endif

#if USE_CILK
  #include <cilk/cilk.h>
  #include <cilk/cilk_api.h>
  #define PARALLEL_REGION // Nothing
  #define PARALLEL_TASK cilk_spawn
  #define PARALLEL_SYNC cilk_sync
  #define PARALLEL_FOR cilk_for
  // By the rule of common denominator, you have to obey the rules of PARALLEL_GET_THREAD_ID() and PARALLEL_RETURN_THREAD_ID() for openmp as well.
  #define PARALLEL_GET_THREAD_ID() __cilkrts_get_worker_number()
  #define PARALLEL_RETURN_THREAD_ID(i) // Nothing
#else
  #include <omp.h>
  #define PARALLEL_REGION OMP_SET_NESTED_LEVEL(2); _Pragma("omp parallel") _Pragma("omp single nowait")
  #define PARALLEL_TASK _Pragma("omp task")
  #define PARALLEL_SYNC _Pragma("omp taskwait")
  #define PARALLEL_FOR _Pragma("omp parallel for") for
  // In each task, you can call PARALLEL_GET_THREAD_ID() only once, and if so, you must call PARALLEL_RETURN_THREAD_ID() at the end of your task.
  #define PARALLEL_GET_THREAD_ID() thread_id_pool_get_index()
  #define PARALLEL_RETURN_THREAD_ID(i) thread_id_pool_return_index(i)
#endif

void parallel_for_inside_task(const int task_id) {
  PARALLEL_FOR (int loop_id = 0; loop_id < 10; loop_id++) {
    const int i_thread = PARALLEL_GET_THREAD_ID();
    sleep(1);
    printf("Thread %2d finish sleeping in task %2d loop %2d\n", i_thread, task_id, loop_id); fflush(stdout);
    PARALLEL_RETURN_THREAD_ID(i_thread);
  }
}

void parallel_for_inside_task(const int task_id, const int sleep_time) {
  PARALLEL_FOR (int loop_id = 0; loop_id < 2 * 2; loop_id++) {
    const int i_thread = PARALLEL_GET_THREAD_ID();
    sleep(sleep_time);
    printf("Thread %2d finish sleeping in task %2d loop %2d\n", i_thread, task_id, loop_id); fflush(stdout);
    PARALLEL_RETURN_THREAD_ID(i_thread);
  }
}

void parallel_task_only(const int task_id) {
  const int i_thread = PARALLEL_GET_THREAD_ID();
  sleep(1);
  printf("Thread %d finish sleeping in task %d\n", i_thread, task_id); fflush(stdout);
  PARALLEL_RETURN_THREAD_ID(i_thread);
}

int main() {
  const int n_thread = 2;

#if USE_CILK
  char Ncilk[16];
  sprintf(Ncilk, "%d", n_thread);
  if (__cilkrts_set_param("nworkers", Ncilk) != 0) {
    DIE("GPUBox: Failed to set Cilk worker count.");
  }
#else
  if (omp_get_dynamic() != 0)
    omp_set_dynamic(0);
  omp_set_num_threads(n_thread);
#endif

  thread_id_pool_initialize(n_thread);

  printf("begin, n_thread = %d\n", n_thread);
  // PARALLEL_REGION {
  //   PARALLEL_TASK parallel_for_inside_task(0);
  //   PARALLEL_TASK parallel_for_inside_task(1);
  //   PARALLEL_TASK parallel_for_inside_task(2);
  //   PARALLEL_TASK parallel_for_inside_task(3);
  //   PARALLEL_TASK parallel_for_inside_task(4);
  //   PARALLEL_TASK parallel_for_inside_task(5);
  //   PARALLEL_TASK parallel_for_inside_task(6);
  //   PARALLEL_TASK parallel_for_inside_task(7);
  //   PARALLEL_TASK parallel_for_inside_task(8);
  //   PARALLEL_TASK parallel_for_inside_task(9);
  //   PARALLEL_SYNC;
  // }

  // PARALLEL_FOR (int loop_id = 0; loop_id < 400; loop_id++) {
  //   const int i_thread = PARALLEL_GET_THREAD_ID();
  //   sleep(1);
  //   printf("Thread %d finish sleeping in loop %2d\n", i_thread, loop_id); fflush(stdout);
  //   PARALLEL_RETURN_THREAD_ID(i_thread);
  // }

  // PARALLEL_REGION {
  //   for (int i = 0; i < 3200; i++)
  //     PARALLEL_TASK parallel_task_only(i);
  //   PARALLEL_SYNC;
  // }

  // PARALLEL_REGION {
  //   PARALLEL_FOR (int outer_id = 0; outer_id < 1; outer_id++) {
  //     PARALLEL_FOR (int inner_id = 0; inner_id < 400; inner_id++) {
  //       const int i_thread = PARALLEL_GET_THREAD_ID();
  //       sleep(1);
  //       printf("Thread %d finish sleeping in outer %2d inner %2d\n", i_thread, outer_id, inner_id); fflush(stdout);
  //       PARALLEL_RETURN_THREAD_ID(i_thread);
  //     }
  //   }
  // }

  PARALLEL_REGION {
    PARALLEL_TASK parallel_for_inside_task(10, 10);
    PARALLEL_TASK parallel_for_inside_task( 9,  9);
    PARALLEL_TASK parallel_for_inside_task( 8,  8);
    PARALLEL_TASK parallel_for_inside_task( 7,  7);
    PARALLEL_TASK parallel_for_inside_task( 6,  6);
    PARALLEL_TASK parallel_for_inside_task( 5,  5);
    PARALLEL_TASK parallel_for_inside_task( 4,  4);
    PARALLEL_TASK parallel_for_inside_task( 3,  3);
    PARALLEL_TASK parallel_for_inside_task( 2,  2);
    PARALLEL_TASK parallel_for_inside_task( 1,  1);
    PARALLEL_SYNC;
  }
  printf("end, n_thread = %d\n", n_thread);

  thread_id_pool_destroy();

  return 0;
}
