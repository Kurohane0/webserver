#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <list>
#include <iostream>
#include <exception>
#include <pthread.h>
#include "locker.h"

//线程池模板类
template<class T>
class threadpool{
public:
    // thread_number是线程池中线程的数量，
    // max_requests是请求队列中最多允许的、等待处理的请求的数量
    threadpool(int thread_number=8,int max_requests = 10000);
    ~threadpool();
    //添加任务
    bool append(T* request);

private:
    //线程工作函数
    static void* worker(void* arg);
    void run();

    //线程数
    int m_thread_number;

    //线程池数组
    pthread_t* m_threads;

    //请求队列中的最大请求数
    int m_max_requests;

    //请求队列
    std::list<T*> m_workqueue;

    //互斥锁
    locker m_queuelocker;

    //信号量，任务数
    sem m_queuestat;

    //代表线程是否结束
    bool m_stop;
};

template <class T>
threadpool<T>::threadpool(int thread_number,int max_requests):
        m_thread_number(thread_number),m_max_requests(max_requests),
        m_stop(false),m_threads(NULL){

    if((thread_number <=0) || (max_requests <=0)){
        throw std::exception();
    }
    //创建数组
    m_threads = new pthread_t[m_thread_number];
    if(!m_threads){
        throw std::exception();
    }

    //创建线程并脱离
    for(int i=0;i<thread_number;++i){
        std::cout<<"创建第"<<i<<"个线程"<<std::endl;
        //C++中worker必须是静态函数
        if(pthread_create(m_threads + i,NULL,worker,this) != 0){
            delete [] m_threads;
            throw std::exception();
        }

        if(pthread_detach(m_threads[i])){
            delete [] m_threads;
            throw std::exception();
        }
    }
}

template<class T>
threadpool<T>::~threadpool(){
    delete [] m_threads;
    m_stop = true;
}

template<class T>
bool threadpool<T>::append(T* request){
    m_queuelocker.lock();
    if(m_workqueue.size() > m_max_requests){
        m_queuelocker.unlock();
        return false;
    }
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post();
    return true;
}

template<class T>
void* threadpool<T>::worker(void* arg){
    threadpool* pool=(threadpool*)arg;
    pool->run();
    return pool;
}

template<class T>
void threadpool<T>::run(){
    while(!m_stop){
        m_queuestat.wait();
        m_queuelocker.lock();
        if(m_workqueue.empty()){
            m_queuelocker.unlock();
            continue;
        }
        //读取数据
        T* request =m_workqueue.front();
        //删除已读取的数据
        m_workqueue.pop_front();
        m_queuelocker.unlock();
        if(!request){
            continue;
        }
        //执行T中的流程
        request->process();
    }
}

#endif