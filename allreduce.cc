/*
Copyright (c) by respective owners including Yahoo!, Microsoft, and
individual contributors. All rights reserved.  Released under a BSD (revised)
license as described in the file LICENSE.
 */
/*
This implements the allreduce function of MPI.
 */
#include <iostream>
#include <cstdio>
#include <cmath>
#include <ctime>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif
#include <sys/timeb.h>
#include "allreduce.h"

using namespace std;

const int buf_size = 1<<16;

// port is already in network order
socket_t sock_connect(const uint32_t ip, const int port) {

  socket_t sock = socket(PF_INET, SOCK_STREAM, 0);
  if (sock == -1)
    {
      cerr << "can't get socket " << endl;
	  throw exception();
    }

  sockaddr_in far_end;
  far_end.sin_family = AF_INET;
  far_end.sin_port = port;

  far_end.sin_addr = *(in_addr*)&ip;
  memset(&far_end.sin_zero, '\0',8);

  {
    char hostname[NI_MAXHOST];
    char servInfo[NI_MAXSERV];
    getnameinfo((sockaddr *) &far_end, sizeof(sockaddr), hostname, NI_MAXHOST, servInfo, NI_MAXSERV, NI_NUMERICSERV);

    cerr << "connecting to " << hostname << ':' << ntohs(port) << endl;
  }

  if (connect(sock,(sockaddr*)&far_end, sizeof(far_end)) == -1)
  {
#ifdef _WIN32
    int err_code = WSAGetLastError();
    cerr << "Windows Sockets error code: " << err_code << endl;
#endif
    cerr << "can't connect to: " ;
    uint32_t pip = ntohl(ip);
    unsigned char * pp = (unsigned char*)&pip;

    for (size_t i = 0; i < 4; i++)
    {
      cerr << static_cast<unsigned int>(static_cast<unsigned short>(pp[3-i])) << ".";
    }
    cerr << ':' << ntohs(port) << endl;
    perror(NULL);
    throw exception();
  } 
  return sock;
}

socket_t getsock()
{
  socket_t sock = socket(PF_INET, SOCK_STREAM, 0);
  if (sock < 0) {
      cerr << "can't open socket!" << endl;
      throw exception();
  }

  // SO_REUSEADDR will allow port rebinding on Windows, causing multiple instances
  // of VW on the same machine to potentially contact the wrong tree node.
#ifndef _WIN32
    int on = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char*)&on, sizeof(on)) < 0) 
      perror("setsockopt SO_REUSEADDR");
#endif
  return sock;
}

void all_reduce_init(const string master_location, const size_t unique_id, const size_t total, const size_t node, node_socks& socks)
{
#ifdef _WIN32
  WSAData wsaData;
  WSAStartup(MAKEWORD(2,2), &wsaData);
  int lastError = WSAGetLastError();
#endif

  struct hostent* master = gethostbyname(master_location.c_str());

  if (master == NULL) {
    cerr << "can't resolve hostname: " << master_location << endl;
    throw exception();
  }
  socks.current_master = master_location;

  uint32_t master_ip = * ((uint32_t*)master->h_addr);
  int port = 26543;

  socket_t master_sock = sock_connect(master_ip, htons(port));
  if(send(master_sock, (const char*)&unique_id, sizeof(unique_id), 0) < (int)sizeof(unique_id))
    cerr << "write failed!" << endl; 
  if(send(master_sock, (const char*)&total, sizeof(total), 0) < (int)sizeof(total))
    cerr << "write failed!" << endl; 
  if(send(master_sock, (char*)&node, sizeof(node), 0) < (int)sizeof(node))
    cerr << "write failed!" << endl; 
  int ok;
  if (recv(master_sock, (char*)&ok, sizeof(ok), 0) < (int)sizeof(ok))
    cerr << "read 1 failed!" << endl;
  if (!ok) {
    cerr << "mapper already connected" << endl;
    throw exception();
  }

  uint16_t connection_count;		
  if(recv(master_sock, (char*)&connection_count, sizeof(connection_count), 0) < (int)sizeof(connection_count)) {
    cerr << "reading connection count failed!" << endl;
  }
  socks.connection_count = connection_count;  

  uint16_t id;
  if(recv(master_sock, (char*)&id, sizeof(id), 0) < (int)sizeof(id)) {
    cerr << "reading id failed!" << endl;
  }
  socks.id = id;  
  //cout << "id received: " << id << endl;

  socket_t sock = -1;
  short unsigned int netport = htons(26544);

    sock = getsock();
    sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = netport;

    bool listening = false;
    while(!listening)
    {
      if (bind(sock,(sockaddr*)&address, sizeof(address)) < 0)
      {
#ifdef _WIN32
        if (WSAGetLastError() == WSAEADDRINUSE)
#else
        if (errno == EADDRINUSE)
#endif
        {
          netport = htons(ntohs(netport)+1);
          address.sin_port = netport;
        }
        else
        {
          perror("Bind failed ");
          throw exception();
        }
      }
      else
      {
        if (listen(sock, connection_count) < 0)
        {
          perror("listen failed! ");
          shutdown(sock, SHUT_RDWR);
          sock = getsock();
        }
        else
        {
	  //cout << "Bound to port " << ntohs(netport) << endl;
          listening = true; 
        }
      }
    }

  if(send(master_sock, (const char*)&netport, sizeof(netport), 0) < (int)sizeof(netport))
    cerr << "write failed!" << endl;

  socks.clients = new socket_t[connection_count];
  socks.servers = new socket_t[connection_count];
  socks.connection_ids = new uint16_t[connection_count];
  socks.accepts = new int*[connection_count];
  socks.sends = new int*[connection_count];
  for (int i = 0; i < connection_count; i++) {
	socks.accepts[i] = new int[2];
	socks.sends[i] = new int[2];
  }

  uint16_t* connection_ports = (uint16_t*)calloc(connection_count,sizeof(uint16_t));
  uint32_t* connection_ips = (uint32_t*)calloc(connection_count,sizeof(uint32_t));

  for (int i = 0; i < socks.connection_count; i++) {
	if(recv(master_sock, (char*)&connection_ips[i], sizeof(connection_ips[i]), 0) < (int)sizeof(connection_ips[i]))
	  cerr << "read IP address failed for connection: " << i << endl;
	if(recv(master_sock, (char*)&connection_ports[i], sizeof(connection_ports[i]), 0) < (int)sizeof(connection_ports[i]))
	    cerr << "read port failed for connection: " << i << endl;
	if(recv(master_sock, (char*)&socks.connection_ids[i], sizeof(socks.connection_ids[i]), 0) < (int)sizeof(socks.connection_ids[i]))
	    cerr << "read id failed for connection: " << i << endl;
  }

  shutdown(master_sock, SHUT_RDWR);
  
  int valid_connection_count = 0;
  for (int i = 0; i < socks.connection_count; i++) {
	if (connection_ips[i] != (uint32_t)-1) {
		socks.servers[i] = sock_connect(connection_ips[i],connection_ports[i]);

		//send the node id so server knows how to assign the socket to the right node
		uint16_t write_size = send(socks.servers[i], (char*)&socks.id, sizeof(socks.id), 0);
	    	if (write_size < sizeof(socks.id))
	      		cerr<<"Write to connection failed "<< endl ;
		valid_connection_count++;
	} else {
		socks.servers[i] = -1;
	}
  }
 
  for (int i = 0; i < socks.connection_count; i++) {
   	socks.clients[i] = -1;
  }

  uint16_t i = 0;
  while (i < valid_connection_count)
  {
    sockaddr_in child_address;
    socklen_t size = sizeof(child_address);
    socket_t f = accept(sock,(sockaddr*)&child_address,&size);
    if (f < 0)
    {
      cerr << "bad client socket!" << endl;
      throw exception();
    } 
	
    //read the id of the connecting node 
    uint16_t incoming_conn_id;
    uint16_t read_size = recv(f, (char*)&incoming_conn_id, sizeof(incoming_conn_id), 0);
    if(read_size < sizeof(incoming_conn_id)) {      
      cerr <<" Read Child ID failed.\n";
      perror(NULL);
      throw exception();
    } 

    //assign socket to the right node, based on the connecting node id
    for (int j = 0; j < socks.connection_count; j++) {
	   if (incoming_conn_id == socks.connection_ids[j]) {
	   	socks.clients[j] = f;
		break;
	   }
    }
    i++;
  }
  //cout << "connections set up" << endl;

  shutdown(sock, SHUT_RDWR);
}


void addbufs(float* buf1, const float* buf2, const int n) {
  for(int i = 0;i < n;i++)  {
     buf1[i] += buf2[i];    
  }
}

void reduce(char* buffer, const int n, node_socks& socks) {
  int length = n/(sizeof(float));
  int accept_start = 0;
  int accept_end = length-1;
  int send_start, send_end, middle;
  int float_buffer_size = buf_size * sizeof(float);
  
  for (int i = 0; i < socks.connection_count; i++) {
    if (socks.clients[i] == -1 || socks.servers[i] == -1) {
	continue;
    }

    //by convention nodes with lower ids will always send the upper half of the buffer section they are responsible for syncing
    //and will accumulate data transfered from other nodes in the bottom half 
    middle = ceil((accept_end - accept_start+1)/2)-1;
    if (socks.id < socks.connection_ids[i]) {	
	send_start = accept_start;
	send_end = send_start + middle;
	accept_start = send_end+1;
    } else {
	send_end = accept_end;
	accept_end = accept_start + middle;
	send_start = accept_end+1;
    }
	
    //cout << "Send start: " << send_start << " Send end: " << send_end << endl;
    //cout << "Accept start: " << accept_start << " Accept end: " << accept_end << endl;

    int send_size = (send_end - send_start + 1)*sizeof(float);
    float* read_pos = ((float*)buffer) + send_start;
    while (send_size > float_buffer_size) { 
      int write_size = send(socks.servers[i], (char*)read_pos, float_buffer_size, 0);
      if (write_size < float_buffer_size)
	cerr<<"Write to connection failed "<< endl ;
      send_size -= float_buffer_size;
      read_pos += (float_buffer_size/(sizeof(float)));
    }
    if (send_size > 0) {
      int write_size = send(socks.servers[i], (char*)read_pos, send_size, 0);
      if (write_size < send_size)
	cerr<<"Write to connection failed "<< endl ;
    }

    int accept_size = (accept_end - accept_start + 1)*sizeof(float);
    float* write_pos = ((float*)buffer)+accept_start;
    while (accept_size > float_buffer_size) {
	    float read_buf[float_buffer_size];
	    int read_size = recv(socks.clients[i], (char*)read_buf, float_buffer_size, 0);
	    if(read_size < float_buffer_size) {
	      cerr <<" Read from child failed\n";
	      perror(NULL);
	      throw exception();
	    }
		
	    addbufs(write_pos,read_buf,float_buffer_size/(sizeof(float)));
	    accept_size -= float_buffer_size;
	    write_pos += (float_buffer_size/(sizeof(float)));
    }
    if (accept_size > 0) {
	    float read_buf[accept_size];
	    int read_size = recv(socks.clients[i], (char*)read_buf, accept_size, 0);
	    if(read_size < accept_size) {
	      cerr <<" Read from child failed\n";
	      perror(NULL);
	      throw exception();
	    }
		
	    addbufs(write_pos,read_buf,accept_size/(sizeof(float)));
    }

    socks.accepts[i][0] = accept_start;
    socks.accepts[i][1] = accept_end;
    socks.sends[i][0] = send_start;
    socks.sends[i][1] = send_end;
  }
}


void broadcast(char* buffer, const int n, node_socks& socks) {
   int float_buffer_size = buf_size * sizeof(float);
   for (int i = socks.connection_count - 1; i >= 0; i--) {
        if (socks.clients[i] == -1 || socks.servers[i] == -1) {
          continue;
	}

	int send_start = socks.accepts[i][0];	
	int send_end = socks.accepts[i][1];

        int send_size = (send_end - send_start + 1)*sizeof(float);
	float* read_pos = (float*)buffer+send_start;
	while (send_size > float_buffer_size) {
		int write_size = send(socks.servers[i], (char*)read_pos, float_buffer_size, 0);
		if (write_size < float_buffer_size)
		  cerr<<"Write to connection failed "<< endl ;

		read_pos += (float_buffer_size/sizeof(float));
		send_size -= float_buffer_size;
	}
	if (send_size > 0) {
		int write_size = send(socks.servers[i], (char*)read_pos, send_size, 0);
		if (write_size < send_size)
		  cerr<<"Write to connection failed "<< endl ;
	} 
 
	int accept_start = socks.sends[i][0];
	int accept_end = socks.sends[i][1];

        int accept_size = (accept_end - accept_start + 1)*sizeof(float);
	float* write_pos = (float*)buffer + accept_start;
	while(accept_size > float_buffer_size) { 
		int read_size = recv(socks.clients[i], (char*)write_pos, float_buffer_size, 0);
		if(read_size < float_buffer_size) {
		  cerr <<" Read from child failed\n";
		  perror(NULL);
		  throw exception();
		}	
	
		write_pos += (float_buffer_size/sizeof(float));
		accept_size -= float_buffer_size;
	}
	if (accept_size > 0) {
		int read_size = recv(socks.clients[i], (char*)write_pos, accept_size, 0);
		if(read_size < accept_size) {
		  cerr <<" Read from child failed\n";
		  perror(NULL);
		  throw exception();
		}	
	}
   }
}

void all_reduce(float* buffer, const int n, const string master_location, const size_t unique_id, const size_t total, const size_t node, node_socks& socks) 
{
  if(master_location != socks.current_master) 
    all_reduce_init(master_location, unique_id, total, node, socks);
  reduce((char*)buffer, n*sizeof(float), socks);
  broadcast((char*)buffer, n*sizeof(float), socks);
}


