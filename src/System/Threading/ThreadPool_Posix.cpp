#ifndef _WIN32
#include <pthread.h>
#include <sched.h>

#include "ThreadPool.h"
#include "System/Common.h"
#include "System/Log.h"
#include "System/Events/Callable.h"

// pthread_yield is not defined on either Linux or OSX
#define pthread_yield   sched_yield

/*
===============================================================================

 ThreadPool_Pthread

===============================================================================
*/


class ThreadPool_Pthread : public ThreadPool
{
public:
    ThreadPool_Pthread(int numThread);
    ~ThreadPool_Pthread();

    ThreadPool*         Create(int numThread);

    virtual void        Start();
    virtual void        Stop();
    virtual void        WaitStop();

    virtual void        Execute(const Callable& callable);

protected:
    pthread_t*          threads;
    pthread_mutex_t     mutex;
    pthread_cond_t      cond;
    unsigned            numStarted;
    bool                finished;
    
    static void*        thread_function(void *param);
    void                ThreadFunction();
    bool                IsFinished();
};

/*
===============================================================================
*/

ThreadPool* ThreadPool::Create(int numThread)
{
    return new ThreadPool_Pthread(numThread);
}

uint64_t ThreadPool::GetThreadID()
{
    return (uint64_t) pthread_self();
}

void ThreadPool::YieldThread()
{
    pthread_yield();
}

void* ThreadPool_Pthread::thread_function(void* param)
{
    ThreadPool_Pthread* tp = (ThreadPool_Pthread*) param;
    
    BlockSignals();
    tp->ThreadFunction();
    
    // pthread_exit should be called for cleanup, instead it creates
    // more unreachable bytes with valgrind:
    //pthread_exit(NULL);
    return NULL;
}

void ThreadPool_Pthread::ThreadFunction()
{   
    Callable    callable;
    Callable*   it;
    bool        firstRun;

    firstRun = true;
    
    while (running)
    {
        pthread_mutex_lock(&mutex);
        numActive--;

        if (firstRun)
        {
            numStarted++;
            pthread_cond_broadcast(&cond);
            firstRun = false;
        }
        
    wait:
        while (numPending == 0 && running && !IsFinished())
            pthread_cond_wait(&cond, &mutex);
        
        if (!running || IsFinished())
        {
            pthread_mutex_unlock(&mutex);
            break;
        }
        it = callables.First();
        if (!it)
            goto wait;
            
        callable = *it;
        callables.Remove(it);
        numPending--;
        numActive++;
        
        pthread_mutex_unlock(&mutex);
        
        Call(callable);
    }
    
    if (finished)
    {
        pthread_mutex_lock(&mutex);
        pthread_cond_signal(&cond);
        pthread_mutex_unlock(&mutex);
    }
}

ThreadPool_Pthread::ThreadPool_Pthread(int numThreads_)
{
    numThreads = numThreads_;
    numPending = 0;
    numActive = 0;
    numStarted = 0;
    running = false;
    finished = false;
    stackSize = 0;
    
    pthread_mutex_init(&mutex, NULL);
    pthread_cond_init(&cond, NULL);
    
    threads = new pthread_t[numThreads];
}

ThreadPool_Pthread::~ThreadPool_Pthread()
{
    Stop();
    delete[] threads;
}

void ThreadPool_Pthread::Start()
{
    unsigned        i;
    int             ret;
    pthread_attr_t  attr;
    
    if (running)
        return;
    
    running = true;
    numActive = numThreads;
    
    pthread_attr_init(&attr);
    if (stackSize != 0)
        pthread_attr_setstacksize(&attr, stackSize);
    
    for (i = 0; i < numThreads; i++)
    {
        ret = pthread_create(&threads[i], &attr, thread_function, this);
        if (ret < 0)
        {
            Log_Errno("Could not start thread!");
            numThreads = i;
        }
    }
        
    pthread_attr_destroy(&attr);
}

void ThreadPool_Pthread::Stop()
{
    unsigned i;
    
    if (!running)
        return;

    running = false;
    
    pthread_mutex_lock(&mutex);
    pthread_cond_broadcast(&cond);
    pthread_mutex_unlock(&mutex);

    for (i = 0; i < numThreads; i++)
    {
        pthread_join(threads[i], NULL);
        pthread_detach(threads[i]);
    }
    
    numActive = 0;
}

void ThreadPool_Pthread::WaitStop()
{
    unsigned i;
    
    if (!running)
        return;
    
    pthread_mutex_lock(&mutex);
    while (numStarted < numThreads)
        pthread_cond_wait(&cond, &mutex);

    finished = true;

    pthread_cond_broadcast(&cond);
    pthread_mutex_unlock(&mutex);

    for (i = 0; i < numThreads; i++)
    {
        pthread_join(threads[i], NULL);
        pthread_detach(threads[i]);
    }
    
    running = false;
    numActive = 0;
}

void ThreadPool_Pthread::Execute(const Callable& callable)
{
    pthread_mutex_lock(&mutex);
    
    callables.Append((Callable&)callable);
    numPending++;
    
    pthread_cond_signal(&cond);
    
    pthread_mutex_unlock(&mutex);
}

bool ThreadPool_Pthread::IsFinished()
{
    return finished && numPending == 0;
}

#endif
