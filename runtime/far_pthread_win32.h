#ifndef FAR_PTHREAD_WIN32_H
#define FAR_PTHREAD_WIN32_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

typedef HANDLE pthread_t;

typedef CRITICAL_SECTION pthread_mutex_t;

typedef CONDITION_VARIABLE pthread_cond_t;

typedef void* pthread_attr_t;

static inline int pthread_create(pthread_t* thread, const pthread_attr_t* attr, void* (*start)(void*),
                                 void* arg) {
  (void)attr;
  *thread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)start, arg, 0, NULL);
  return *thread ? 0 : -1;
}

static inline int pthread_join(pthread_t thread, void** value) {
  (void)value;
  if (WaitForSingleObject(thread, INFINITE) != WAIT_OBJECT_0)
    return -1;
  CloseHandle(thread);
  return 0;
}

static inline int pthread_mutex_init(pthread_mutex_t* mu, const void* attr) {
  (void)attr;
  InitializeCriticalSection(mu);
  return 0;
}

static inline int pthread_mutex_lock(pthread_mutex_t* mu) {
  EnterCriticalSection(mu);
  return 0;
}

static inline int pthread_mutex_unlock(pthread_mutex_t* mu) {
  LeaveCriticalSection(mu);
  return 0;
}

static inline int pthread_mutex_destroy(pthread_mutex_t* mu) {
  DeleteCriticalSection(mu);
  return 0;
}

static inline int pthread_cond_init(pthread_cond_t* cv, const void* attr) {
  (void)attr;
  InitializeConditionVariable(cv);
  return 0;
}

static inline int pthread_cond_wait(pthread_cond_t* cv, pthread_mutex_t* mu) {
  SleepConditionVariableCS(cv, mu, INFINITE);
  return 0;
}

static inline int pthread_cond_signal(pthread_cond_t* cv) {
  WakeConditionVariable(cv);
  return 0;
}

static inline int pthread_cond_broadcast(pthread_cond_t* cv) {
  WakeAllConditionVariable(cv);
  return 0;
}

static inline int pthread_cond_destroy(pthread_cond_t* cv) {
  (void)cv;
  return 0;
}

#endif
