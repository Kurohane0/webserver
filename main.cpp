#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/errno.h>
#include  "locker.h"
#include "threadpool.h"
#include "http_conn.h"

#define MAX_FD 65536   //最大文件描述符个数
#define MAX_EVENT_NUMBER 10000   //最大监听事件数量

//添加文件描述符
extern void addfd( int epollfd, int fd , bool one_shot );
extern void removefd( int epollfd, int fd);

//添加信号捕捉
void addsig(int sig,void ( hander )(int)){
    struct sigaction sa;
    memset( &sa, '\0',sizeof(sa));
    sa.sa_handler=hander;
    sigfillset( &sa.sa_mask );
    assert(sigaction( sig, &sa,NULL )!= -1);
}

int main(int argc, char * argv[]){
    if(argc<=1){
        printf("按照如下格式运行：%s port_number\n",basename(argv[0]));
        return 1;
    }
    //获取端口号
    int port = atoi(argv[1]);

    //忽略SIGPIPE信号
    addsig( SIGPIPE, SIG_IGN );

    //创建线程池
    threadpool<http_conn>*pool = NULL;
    try{
        pool = new threadpool<http_conn>;
    }catch( ... ){
        return 1;
    }

    //创建用于保存客户端信息的数组
    http_conn* users = new http_conn[ MAX_FD ];
    //创建监听的套接字
    int listenfd = socket( PF_INET, SOCK_STREAM, 0 );

    int ret = 0;
    struct sockaddr_in address;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_family = AF_INET;
    address.sin_port = htons( port );

    //设置端口复用
    int reuse = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof( reuse ));
    ret = bind(listenfd ,(struct sockaddr*)&address ,sizeof( address ));
    ret = listen(listenfd, 5);

    //创建epoll对象和事件数组
    epoll_event events[ MAX_EVENT_NUMBER ];
    int epollfd = epoll_create(5);
    //将监听的文件描述符添加到epoll对象中
    addfd( epollfd, listenfd, false );
    http_conn::m_epollfd = epollfd;

    while(1){
        int number = epoll_wait( epollfd, events, MAX_EVENT_NUMBER, -1 ); //检测事件发生
        
        if((number < 0)&&(errno != EINTR )){
            printf("epoll调用失败\n");
            break;
        }
        //循环遍历事件数组
        for( int i = 0; i < number; ++i){

            int sockfd = events[i].data.fd;

            if( sockfd == listenfd ){   //有客户端连接

                struct sockaddr_in client_address;
                socklen_t client_addrlength = sizeof(client_address);
                int connfd = accept( listenfd, (struct sockaddr*)&client_address, &client_addrlength);   //连接客户端

                if( connfd < 0 ){
                    printf("客户端连接失败\n");
                    continue;
                }

                if( http_conn::m_user_count >= MAX_FD ){   //连接数满了
                    //给客户端返回服务器忙的信息
                    close(connfd);
                    continue;
                } 

                users[connfd].init( connfd, client_address );  //初始化新的客户端数据，并放到数组中

            }else if(events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)){   //客户端异常断开或发生错误
                
                users[sockfd].close_conn();

            }else if(events[i].events & EPOLLIN){   //发生读事件

                if(users[sockfd].read()){
                    pool->append(users + sockfd);
                } else {
                    users[sockfd].close_conn();
                }
            }else if( events[i].events &EPOLLOUT){    //发生写事件

                if(!users[sockfd].write()){
                    users[sockfd].close_conn();
                }
            }
        }
    }

    close( epollfd );
    close( listenfd );
    delete [] users;
    delete pool;
    return 0;
}

