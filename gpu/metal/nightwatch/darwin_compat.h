// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <sys/socket.h>
#include <poll.h>
inline int nw_darwin_socket(int domain,int type,int protocol) {
    const int s=::socket(domain,type,protocol),one=1;
    if (s>=0) setsockopt(s,SOL_SOCKET,SO_NOSIGPIPE,&one,sizeof(one));
    return s;
}
inline int nw_darwin_accept(int listener,sockaddr* address,socklen_t* length) {
    const int s=::accept(listener,address,length),one=1;
    if (s>=0) setsockopt(s,SOL_SOCKET,SO_NOSIGPIPE,&one,sizeof(one));
    return s;
}
