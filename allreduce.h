/*
Copyright (c) by respective owners including Yahoo!, Microsoft, and
individual contributors. All rights reserved.  Released under a BSD
license as described in the file LICENSE.
 */
// This implements the allreduce function of MPI.

#ifndef ALLREDUCE_H
#define ALLREDUCE_H
#include <string>
#ifdef _WIN32
#include <WinSock2.h>
#include <WS2tcpip.h>
typedef unsigned int uint32_t;
typedef unsigned short uint16_t;
typedef int socklen_t;
typedef SOCKET socket_t;
#define SHUT_RDWR SD_BOTH
#else
#include <sys/socket.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
typedef int socket_t;
#endif

struct node_socks {
  std::string current_master;
  uint16_t id;			
  uint16_t connection_count;	//number of connecting nodes
  socket_t* clients;		//client sockets to connecting nodes
  socket_t* servers;		//server sockets to connecting nodes
  uint16_t* connection_ids;	//ids of connecting nodes
  int** accepts;		//keeps track of the buffer positions where we accepted and aggregated data from connecting nodes
  int** sends;			//keeps track of the buffer positions from where we sent data to connecting nodes

  ~node_socks()
  {
    if(current_master != "") {
	for (int i = 0; i < connection_count; i++) {
		if (clients[i] != -1)
			shutdown(clients[i],SHUT_RDWR);
		if (servers[i] != -1)
			shutdown(servers[i],SHUT_RDWR);
		}
    }
    free(clients);
    free(servers);
    free(connection_ids);
    free(accepts);
    free(sends);
  }
  node_socks ()
  {
    current_master = "";
  }
};

void all_reduce(float* buffer, int n, std::string master_location, size_t unique_id, size_t total, size_t node, node_socks& socks);

#endif

