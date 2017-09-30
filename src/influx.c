/*
 * eagle-owl InfluxDB consumer.
 *
 * Copyright (C) 2017 JP Meijers <eagle-owl@jpmeijers.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */
 
#include <stdio.h> /* printf, sprintf */
#include <stdlib.h> /* exit, atoi, malloc, free */
#include <stdbool.h>
#include <unistd.h> /* read, write, close */
#include <string.h> /* memcpy, memset */
#include <sys/socket.h> /* socket, connect */
#include <netinet/in.h> /* struct sockaddr_in, struct sockaddr */
#include <netdb.h> /* struct hostent, gethostbyname */
#include "influx.h"
#include "db.h"

#define INFLUXDB_HOST "localhost"
#define INFLUXDB_PORT 8086
#define INFLUXDB_DBNAME "eagleowl"
#define INFLUXDB_KEY "amps"
#define INFLUXDB_PATH "/write?db=" INFLUXDB_DBNAME

void error(const char *msg) { perror(msg); exit(0); }

int influxdb_insert_hist(struct record_data *rec)
{
  if(!rec->isLiveData)
  {
    // We only insert live data to influxdb,
    // because those are the only ones we are guaranteed to know the timestamp of.
    return 1;
  }
  
  char amps[22];
  sprintf(amps, "%10.10f", rec->amps);
  
  // for loop iterator
  int i=0;

  // Connection details
  int portno = INFLUXDB_PORT;
  char* host = INFLUXDB_HOST;
  char* method = "POST";
  char* path = INFLUXDB_PATH;
  char* headers[] = {};

  char* payload;
  int payload_size = strlen(INFLUXDB_KEY)+strlen(" value=")+strlen(amps)+1;
  payload = malloc(payload_size);
  sprintf(payload, "%s value=%s", INFLUXDB_KEY, amps);

  struct hostent *server;
  struct sockaddr_in serv_addr;
  int sockfd, bytes, sent, received, total, message_size;
  char *message, response[4096];

  /* How big is the message? */
  message_size=0;
  message_size+=strlen("%s %s HTTP/1.0\r\n");
  message_size+=strlen(method);                         /* method         */
  message_size+=strlen(path);                         /* path           */
  for(i=0; i<sizeof(headers)/sizeof(headers[0]); i++)                                    /* headers        */
      message_size+=strlen(headers[i])+strlen("\r\n");
  message_size+=strlen("Content-Length: %d\r\n")+10; /* content length */
  message_size+=strlen("\r\n");                          /* blank line     */
  message_size+=strlen(payload);                     /* body           */

  /* allocate space for the message */
  message=malloc(message_size);

  sprintf(message,"%s %s HTTP/1.0\r\n",
          method,                  /* method         */
          path);                    /* path           */
  for(i=0; i<sizeof(headers)/sizeof(headers[0]); i++)                                    /* headers        */
      {strcat(message,headers[i]);strcat(message,"\r\n");}
  sprintf(message+strlen(message),"Content-Length: %d\r\n",(int)strlen(payload));
  strcat(message,"\r\n");                                /* blank line     */
  strcat(message,payload);                           /* body           */

  /* What are we going to send? */
  //printf("Request:\n%s\n",message);

  /* create the socket */
  sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd < 0) error("ERROR opening socket");

  /* lookup the ip address */
  server = gethostbyname(host);
  if (server == NULL) error("ERROR, no such host");

  /* fill in the structure */
  memset(&serv_addr,0,sizeof(serv_addr));
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons(portno);
  memcpy(&serv_addr.sin_addr.s_addr,server->h_addr,server->h_length);

  /* connect the socket */
  if (connect(sockfd,(struct sockaddr *)&serv_addr,sizeof(serv_addr)) < 0)
      error("ERROR connecting");

  /* send the request */
  total = strlen(message);
  sent = 0;
  do {
      bytes = write(sockfd,message+sent,total-sent);
      if (bytes < 0)
          error("ERROR writing message to socket");
      if (bytes == 0)
          break;
      sent+=bytes;
  } while (sent < total);

  /* receive the response */
  memset(response,0,sizeof(response));
  total = sizeof(response)-1;
  received = 0;
  do {
      bytes = read(sockfd,response+received,total-received);
      if (bytes < 0)
          error("ERROR reading response from socket");
      if (bytes == 0)
          break;
      received+=bytes;
  } while (received < total);

  if (received == total)
      error("ERROR storing complete response from socket");

  /* close the socket */
  close(sockfd);

  /* process response */
  //printf("Response:\n%s\n",response);

  free(payload);
  free(message);
  return 0;
}

// int main()
// {
//   struct record_data rec;
//   rec.amps = 10.002;
//   rec.isLiveData = true;
//   influxdb_insert_hist(&rec);
//   return 0;
// }
