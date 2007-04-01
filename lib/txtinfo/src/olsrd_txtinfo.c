/*
 * The olsr.org Optimized Link-State Routing daemon(olsrd)
 * Copyright (c) 2004, Andreas T�nnesen(andreto@olsr.org)
 *                     includes code by Bruno Randolf
 *                     includes code by Andreas Lopatic
 *                     includes code by Sven-Ola Tuecke
 *                     includes code by Lorenz Schori
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without 
 * modification, are permitted provided that the following conditions 
 * are met:
 *
 * * Redistributions of source code must retain the above copyright 
 *   notice, this list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright 
 *   notice, this list of conditions and the following disclaimer in 
 *   the documentation and/or other materials provided with the 
 *   distribution.
 * * Neither the name of olsr.org, olsrd nor the names of its 
 *   contributors may be used to endorse or promote products derived 
 *   from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS 
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT 
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS 
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE 
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, 
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, 
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; 
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER 
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT 
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN 
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE 
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * Visit http://www.olsr.org for more information.
 *
 * If you find this software useful feel free to make a donation
 * to the project. For more information see the website or contact
 * the copyright holders.
 *
 * $Id: olsrd_txtinfo.c,v 1.2 2007/03/27 03:59:27 tlopatic Exp $
 */

/*
 * Dynamic linked library for the olsr.org olsr daemon
 */

 
#include <sys/types.h>
#include <sys/socket.h>
#if !defined WIN32
#include <sys/select.h>
#endif
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <time.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <errno.h>

#include "olsr.h"
#include "olsr_types.h"
#include "neighbor_table.h"
#include "two_hop_neighbor_table.h"
#include "mpr_selector_set.h"
#include "tc_set.h"
#include "hna_set.h"
#include "mid_set.h"
#include "link_set.h"
#include "socket_parser.h"

#include "olsrd_txtinfo.h"
#include "olsrd_plugin.h"


#ifdef WIN32
#define close(x) closesocket(x)
#endif


static int ipc_socket;
static int ipc_open;
static int ipc_connection;
static int ipc_socket_up;


/* IPC initialization function */
static int
plugin_ipc_init(void);

static void 
send_info(int neighonly);

static void
ipc_action(int);

static void inline
ipc_print_neigh_link(void);

static void inline
ipc_print_routes(void);

static void inline
ipc_print_topology(void);

static void inline
ipc_print_hna(void);

#define TXT_IPC_BUFSIZE 256
static int 
ipc_sendf(const char* format, ...);

/**
 *Do initialization here
 *
 *This function is called by the my_init
 *function in uolsrd_plugin.c
 */
int
olsrd_plugin_init()
{
  /* Initial IPC value */
  ipc_open = 0;
  ipc_socket_up = 0;

  plugin_ipc_init();
  return 1;
}


/**
 * destructor - called at unload
 */
void
olsr_plugin_exit()
{
  if(ipc_open)
    close(ipc_socket);
}



static int
plugin_ipc_init()
{
  struct sockaddr_in sin;
  olsr_u32_t yes = 1;

  /* Init ipc socket */
  if ((ipc_socket = socket(AF_INET, SOCK_STREAM, 0)) == -1) 
    {
#ifndef NODEBUG
      olsr_printf(1, "(TXTINFO) socket()=%s\n", strerror(errno));
#endif
      return 0;
    }
  else
    {
      if (setsockopt(ipc_socket, SOL_SOCKET, SO_REUSEADDR, (char *)&yes, sizeof(yes)) < 0) 
      {
#ifndef NODEBUG
	olsr_printf(1, "(TXTINFO) setsockopt()=%s\n", strerror(errno));
#endif
	return 0;
      }

#if defined __FreeBSD__ && defined SO_NOSIGPIPE
      if (setsockopt(ipc_socket, SOL_SOCKET, SO_NOSIGPIPE, (char *)&yes, sizeof(yes)) < 0) 
      {
	perror("SO_REUSEADDR failed");
	return 0;
      }
#endif

      /* Bind the socket */
      
      /* complete the socket structure */
      memset(&sin, 0, sizeof(sin));
      sin.sin_family = AF_INET;
      sin.sin_addr.s_addr = INADDR_ANY;
      sin.sin_port = htons(ipc_port);
      
      /* bind the socket to the port number */
      if (bind(ipc_socket, (struct sockaddr *) &sin, sizeof(sin)) == -1) 
	{
#ifndef NODEBUG
	  olsr_printf(1, "(TXTINFO) bind()=%s\n", strerror(errno));
#endif
	  return 0;
	}
      
      /* show that we are willing to listen */
      if (listen(ipc_socket, 1) == -1) 
	{
#ifndef NODEBUG
	  olsr_printf(1, "(TXTINFO) listen()=%s\n", strerror(errno));
#endif
	  return 0;
	}
	
      /* Register with olsrd */
      add_olsr_socket(ipc_socket, &ipc_action);

#ifndef NODEBUG
      olsr_printf(2, "(TXTINFO) listening on port %d\n",ipc_port);
#endif
      ipc_socket_up = 1;
    }

  return 1;
}


static void
ipc_action(int fd)
{
  struct sockaddr_in pin;
  socklen_t addrlen;
  char *addr;  

  addrlen = sizeof(struct sockaddr_in);

  if(ipc_open)
    return;

  if ((ipc_connection = accept(fd, (struct sockaddr *)  &pin, &addrlen)) == -1)
    {
#ifndef NODEBUG
      olsr_printf(1, "(TXTINFO) accept()=%s\n", strerror(errno));
#endif
      exit(1);
    }
  else
    {
      addr = inet_ntoa(pin.sin_addr);
      if(ntohl(pin.sin_addr.s_addr) != ntohl(ipc_accept_ip.s_addr))
	{
	  olsr_printf(1, "(TXTINFO) From host(%s) not allowed!\n", addr);
	  close(ipc_connection);
	  return;
	}
      else
	{
      ipc_open = 1;
#ifndef NODEBUG
      olsr_printf(2, "(TXTINFO) Connect from %s\n",addr);
#endif
      
      /* purge read buffer to prevent blocking on linux*/
      fd_set rfds;
      FD_ZERO(&rfds);
      FD_SET(ipc_connection, &rfds);
      struct timeval tv = {0,0};
      int neighonly = 0;
      if(select(ipc_connection+1, &rfds, NULL, NULL, &tv)) {
        char requ[128];
        ssize_t s = recv(ipc_connection, &requ, sizeof(requ), 0);
        if (0 < s) {
          requ[s] = 0;
          /* To print out neighbours only on the Freifunk Status
           * page the normal output is somewhat lengthy. The
           * header parsing is sufficient for standard wget.
           */
          neighonly = (0 != strstr(requ, "/neighbours"));
        }
      }

      send_info(neighonly);
	  
      close(ipc_connection);
      ipc_open = 0;
    }
  }
}

static void inline
ipc_print_neigh_link(void)
{
  struct neighbor_entry *neigh;
  struct neighbor_2_list_entry *list_2;
  struct link_entry *link = NULL;
  int index, thop_cnt;

	ipc_sendf("Table: Links\nLocal IP\tremote IP\tHysteresis\tLinkQuality\tlost\ttotal\tNLQ\tETX\n");

  /* Link set */
  link = link_set;
	while(link)
	{
	ipc_sendf( "%s\t%s\t%0.2f\t%0.2f\t%d\t%d\t%0.2f\t%0.2f\t\n",
			olsr_ip_to_string(&link->local_iface_addr),
			olsr_ip_to_string(&link->neighbor_iface_addr),
			link->L_link_quality, 
			link->loss_link_quality,
			link->lost_packets, 
			link->total_packets,
			link->neigh_link_quality, 
			(link->loss_link_quality * link->neigh_link_quality) ? 1.0 / (link->loss_link_quality * link->neigh_link_quality) : 0.0);
		link = link->next;
      }
  	ipc_sendf("\nTable: Neighbors\nIP address\tSYM\tMPR\tMPRS\tWillingness\t2 Hop Neighbors\n");

  /* Neighbors */
  for(index=0;index<HASHSIZE;index++)
    {
      for(neigh = neighbortable[index].next;
	  neigh != &neighbortable[index];
	  neigh = neigh->next)
	{
		ipc_sendf( 
			  "%s\t%s\t%s\t%s\t%d\t", 
			  olsr_ip_to_string(&neigh->neighbor_main_addr),
			  (neigh->status == SYM) ? "YES" : "NO",
			  neigh->is_mpr ? "YES" : "NO",
			  olsr_lookup_mprs_set(&neigh->neighbor_main_addr) ? "YES" : "NO",
			  neigh->willingness);

	  thop_cnt = 0;

	  for(list_2 = neigh->neighbor_2_list.next;
	      list_2 != &neigh->neighbor_2_list;
	      list_2 = list_2->next)
	    {
	      //size += sprintf(&buf[size], "<option>%s</option>\n", olsr_ip_to_string(&list_2->neighbor_2->neighbor_2_addr));
	      thop_cnt ++;
	    }
  		ipc_sendf("%d\n", thop_cnt);
	}
    }

	ipc_sendf("\n");
}


static void inline
ipc_print_routes(void)
{
  int size = 0, index;
  struct rt_entry *routes;

	ipc_sendf("Table: Routes\nDestination\tGateway\tMetric\tETX\tInterface\tType\n");

  /* Neighbors */
  for(index = 0;index < HASHSIZE;index++)
    {
      for(routes = routingtable[index].next;
	  routes != &routingtable[index];
	  routes = routes->next)
	{
		size = 0;
  		ipc_sendf( "%s\t%s\t%d\t%.2f\t%s\tHOST\n",
			  olsr_ip_to_string(&routes->rt_dst),
			  olsr_ip_to_string(&routes->rt_router),
			  routes->rt_metric,
			  routes->rt_etx,
			  routes->rt_if->int_name);
	}
    }

  /* HNA */
  for(index = 0;index < HASHSIZE;index++)
    {
      for(routes = hna_routes[index].next;
	  routes != &hna_routes[index];
	  routes = routes->next)
	{
  		ipc_sendf("%s\t%s\t%d\t%s\t\tHNA\n",
			  olsr_ip_to_string(&routes->rt_dst),
			  olsr_ip_to_string(&routes->rt_router),
			  routes->rt_metric,
			  routes->rt_if->int_name);
	}
    }

	ipc_sendf("\n");

}

static void inline
ipc_print_topology(void)
{
  olsr_u8_t index;
  struct tc_entry *entry;
  struct topo_dst *dst_entry;


  ipc_sendf("Table: Topology\nDestination IP\tLast hop IP\tLQ\tILQ\tETX\n");

  /* Topology */  
  for(index=0;index<HASHSIZE;index++)
    {
      /* For all TC entries */
      entry = tc_table[index].next;
      while(entry != &tc_table[index])
	{
	  /* For all destination entries of that TC entry */
	  dst_entry = entry->destinations.next;
	  while(dst_entry != &entry->destinations)
	    {
			ipc_sendf( "%s\t%s\t%0.2f\t%0.2f\t%0.2f\n", 
			      olsr_ip_to_string(&dst_entry->T_dest_addr),
			      olsr_ip_to_string(&entry->T_last_addr), 
			      dst_entry->link_quality,
			      dst_entry->inverse_link_quality,
			      (dst_entry->link_quality * dst_entry->inverse_link_quality) ? 1.0 / (dst_entry->link_quality * dst_entry->inverse_link_quality) : 0.0);
		
	      dst_entry = dst_entry->next;
	    }
	  entry = entry->next;
	}
    }

  ipc_sendf("\n");
}

static void inline
ipc_print_hna(void)
{
  int size;
  olsr_u8_t index;
  struct hna_entry *tmp_hna;
  struct hna_net *tmp_net;

  size = 0;

  ipc_sendf("Table: HNA\nNetwork\tNetmask\tGateway\n");

  /* Announced HNA entries */
	struct hna4_entry *hna4;
	for(hna4 = olsr_cnf->hna4_entries; hna4; hna4 = hna4->next)
	  {
			ipc_sendf("%s\t%s\t%s\n",
			  olsr_ip_to_string(&hna4->net),
			  olsr_ip_to_string(&hna4->netmask),
				olsr_ip_to_string(&olsr_cnf->main_addr));
	  }
	struct hna6_entry *hna6;
	for(hna6 = olsr_cnf->hna6_entries; hna6; hna6 = hna6->next)
	  {
			ipc_sendf("%s\t%d\t%s\n",
			  olsr_ip_to_string(&hna6->net),
				hna6->prefix_len,
		  	olsr_ip_to_string(&olsr_cnf->main_addr));
		}

  /* HNA entries */
  for(index=0;index<HASHSIZE;index++)
    {
      tmp_hna = hna_set[index].next;
      /* Check all entrys */
      while(tmp_hna != &hna_set[index])
	{
	  /* Check all networks */
	  tmp_net = tmp_hna->networks.next;
	      
	  while(tmp_net != &tmp_hna->networks)
	    {
		if (AF_INET == olsr_cnf->ip_version) {
			ipc_sendf("%s\t%s\t%s\n",
				olsr_ip_to_string(&tmp_net->A_network_addr),
				olsr_ip_to_string((union olsr_ip_addr *)&tmp_net->A_netmask.v4),
				olsr_ip_to_string(&tmp_hna->A_gateway_addr));
		}
		else {
			ipc_sendf("%s\t%d\t%s\n",
				olsr_ip_to_string(&tmp_net->A_network_addr),
				tmp_net->A_netmask.v6,
				olsr_ip_to_string(&tmp_hna->A_gateway_addr));
		}
	      tmp_net = tmp_net->next;
	    }
	      
	  tmp_hna = tmp_hna->next;
	}
    }

	ipc_sendf("\n");

}


static void 
send_info(int neighonly)
{
	
	/* Print minimal http header */
	ipc_sendf("HTTP/1.0 200 OK\n");
	ipc_sendf("Content-type: text/plain\n\n");

	/* Print tables to IPC socket */
	
	/* links + Neighbors */
	ipc_print_neigh_link();
	
	/* topology */
	if (!neighonly) ipc_print_topology();
	
 	/* hna */
	if (!neighonly) ipc_print_hna();

	/* routes */
	if (!neighonly) ipc_print_routes();
}

/*
 * In a bigger mesh, there are probs with the fixed
 * bufsize. Because the Content-Length header is
 * optional, the sprintf() is changed to a more
 * scalable solution here.
 */
 
static int 
ipc_sendf(const char* format, ...)
{
	char txtnetbuf[TXT_IPC_BUFSIZE];

	va_list arg;
	int rv;
#if defined __FreeBSD__ || defined __NetBSD__ || defined __OpenBSD__ || defined __MacOSX__
	int flags = 0;
#else
	int flags = MSG_NOSIGNAL;
#endif
	va_start(arg, format);
	rv = vsnprintf(txtnetbuf, sizeof(txtnetbuf), format, arg);
	va_end(arg);
	if(ipc_socket_up) {
		if (0 > send(ipc_connection, txtnetbuf, rv, flags)) {
#ifndef NODEBUG
			olsr_printf(1, "(TXTINFO) Failed sending data to client!\n");
#endif
			close(ipc_connection);
			return - 1;
		}
	}
	return rv;
}