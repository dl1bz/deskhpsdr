/* Copyright (C)
* 2024 - Christoph van W"ullen, DL1YCF
*
*   This program is free software: you can redistribute it and/or modify
*   it under the terms of the GNU General Public License as published by
*   the Free Software Foundation, either version 3 of the License, or
*   (at your option) any later version.
*
*   This program is distributed in the hope that it will be useful,
*   but WITHOUT ANY WARRANTY; without even the implied warranty of
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*   GNU General Public License for more details.
*
*   You should have received a copy of the GNU General Public License
*   along with this program.  If not, see <https://www.gnu.org/licenses/>.
*
*/

//
// Minimal stripped-down TCI server for use with logbook programs
// and possibly PAs. This is built upon  a "light-weight" websocket server.
//

#include <gtk/gtk.h>
#include <gdk/gdk.h>

#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>

#include <openssl/sha.h>
#include <openssl/evp.h>

#include "radio.h"
#include "vfo.h"
#include "rigctl.h"
#include "message.h"

#define MAX_TCI_CLIENTS 3
#define MAXDATASIZE     1024
#define MAXMSGSIZE      128

int tci_enable = 0;
int tci_port   = 40001;
int tci_txonly = 0;

//
// OpCodes for WebSocket frames
//
enum OpCode {
  opCONT  = 0,
  opTEXT  = 1,
  opBIN   = 2,
  opCLOSE = 8,
  opPING  = 9,
  opPONG  = 10
};

static GThread *tci_server_thread_id = NULL;
static int tci_running = 0;

static int server_socket = -1;
static struct sockaddr_in server_address;

typedef struct _client {
  int seq;                      // Seq. number of the client
  int fd;                       // socket
  int running;                  // set this to zero to close client connection
  guint tci_timer;              // GTK id  of the periodic task
  socklen_t address_length;     // unused
  struct sockaddr_in address;   // unused
  GThread *thread_id;           // thread id of receiving thread
  long long last_fa;            // last VFO-A freq reported
  long long last_fb;            // last VFO-B freq reported
  long long last_fx;            // last TX    freq reported
  int last_ma;                  // last VFO-A mode reported
  int last_mb;                  // last VFO-B mode reported
  int count;                    // ping counter
} CLIENT;

typedef struct _response {
  CLIENT *client;
  int     type;
  char    msg[MAXMSGSIZE];
} RESPONSE;

static CLIENT tci_client[MAX_TCI_CLIENTS];

static gpointer tci_server(gpointer data);
static gpointer tci_listener(gpointer data);

//
// Launch TCI system. Called upon program start if TCI is
// enabled in the props file, and from the CAT/TCI menu
// if TCI is enabled there.
//
void launch_tci () {
  t_print( "---- LAUNCHING TCI SERVER ----\n");
  tci_running = 1;
  //
  // Start TCI server
  //
  tci_server_thread_id = g_thread_new( "tci server", tci_server, GINT_TO_POINTER(tci_port));
}

//
// This enforces closing a "listener" connection even if it "hangs"
// and removes the autoreporting task. Do not  join with the listener
// so this may be called from  within the listener.
//
void force_close(CLIENT *client) {
  struct linger linger = { 0 };
  linger.l_onoff = 1;
  linger.l_linger = 0;

  client->running = 0;
  if (client->fd  != -1) {

    // No error checking since the socket may have been close in a race condition
    // in the listener
    setsockopt(client->fd, SOL_SOCKET, SO_LINGER, (const char *)&linger, sizeof(linger));
    close(client->fd);
    client->fd = -1;
  }

  if (client->tci_timer != 0) {
    g_source_remove(client->tci_timer);
    client->tci_timer = 0;
  }
}

//
// Shut down TCI system. Called from CAT/TCI menu
// if TCI is disabled there.
//
void shutdown_tci() {
  t_print("%s: server_socket=%d\n", __FUNCTION__, server_socket);
  tci_running = 0;

  //
  // Terminate all active TCI connections and join with listener threads
  // Joining is temporarily disabled until we know why it sometimes hangs
  //
  for (int id = 0; id < MAX_TCI_CLIENTS; id++) {
    force_close(&tci_client[id]);
    usleep(100000); // let the client terminate if it can

    if (tci_client[id].thread_id) {
      //g_thread_join(tci_client[id].thread_id);
      tci_client[id].thread_id = NULL;
    }

  }
  usleep(100000);  // Let the TCI thread terminate, if it can

  //
  // Forced close of server socket, and join with TCI thread
  //
  if (server_socket >= 0) {
    struct linger linger = { 0 };
    linger.l_onoff = 1;
    linger.l_linger = 0;

    // No error checking since the socket may be closed
    // in a race condition by the server thread
    setsockopt(server_socket, SOL_SOCKET, SO_LINGER, (const char *)&linger, sizeof(linger));
    close(server_socket);
    server_socket = -1;
  }

  if (tci_server_thread_id) {
    //g_thread_join(tci_server_thread_id);
    tci_server_thread_id = NULL;
  }
}

//
// send_frame is intended to  be called  through the GTK idle queue
//
static int send_frame(void *data) {
  RESPONSE *response = (RESPONSE *) data;
  CLIENT *client = response->client;
  int type = response->type;
  char *msg = response->msg;

  unsigned char frame[1024];
  unsigned char *p;
  int start;

  //
  // This means client has already been closed
  //
  if (client->fd < 0) {
    g_free(data);
    return G_SOURCE_REMOVE;
  }

  int length = strlen(msg);
  frame[0] = 128 | type;

  if (length <= 125) {
    frame[1] = length;
    start = 2;
  } else {
    frame[1] = 126;
    frame[2] = (length >> 8) & 255;
    frame[3] = length & 255;
    start = 4;
  }

  for (int i = 0; i < length; i++) {
    frame[start + i] = msg[i];
  }

  length = length + start;
  int count = 0;
  p = frame;

  //
  // Write data, possibly in several chunks. If it fails,
  // mark the client as "not running"
  //
  while (length > 0) {
    int rc = write(client->fd, p, length);

    if (rc < 0) {
      g_free(data);
      client->running = 0;
      return G_SOURCE_REMOVE;
    }

    if (rc == 0) {
      count++;

      if (count > 10) {
        g_free(data);
        client->running = 0;
        return G_SOURCE_REMOVE;
      }
    }

    length -= rc;
    p += rc;
  }
  g_free(data);
  return G_SOURCE_REMOVE;
}

void send_text(CLIENT *client, char *msg) {
  if (rigctl_debug) { t_print("TCI%d response: %s\n", client->seq, msg); }
  RESPONSE *resp=g_new(RESPONSE, 1);
  resp->client = client;
  strcpy(resp->msg, msg);
  resp->type = opTEXT;
  g_idle_add(send_frame, resp);
}

//
// To keep things  simple, send_dds does not report
// the center frequency but the "real" RX frequency
//
void send_dds(CLIENT *client, int v) {
  long long f;
  char msg[MAXMSGSIZE];
  f = vfo[v].ctun ? vfo[v].ctun_frequency : vfo[v].frequency;
  snprintf(msg, MAXMSGSIZE, "dds:%d,%lld;", v, f);
  send_text(client, msg);
}

void send_mox(CLIENT *client) {
  if (radio_is_transmitting()) {
    send_text(client,"trx:0,true;");
  } else {
    send_text(client,"trx:0,false;");
  }
}

//
// There are four (!) frequencies to report, namely for RX0/1 channel0/1.
// This is mapped onto our  piHPSDR model as
// RX=0 channel=0: VFO-A
// RX=0 channel=1: VFO-B
// RX=1 channel=0: VFO-B
// RX=1 channel=1: VFO-B
//
// Thus logbook programs correctly display both frequencies no matter whether
// they  use RX0/channel0:RX0/channel1 or RX0/channel0:RX1/channel0
//
void send_vfo(CLIENT *client, int v) {
  long long f1,f2;
  char msg[MAXMSGSIZE];
  if (v  == VFO_A) {
    f1 = vfo[VFO_A].ctun ? vfo[VFO_A].ctun_frequency : vfo[VFO_A].frequency;
    f2 = vfo[VFO_B].ctun ? vfo[VFO_B].ctun_frequency : vfo[VFO_B].frequency;
  } else {
    f1 = f2 = vfo[VFO_B].ctun ? vfo[VFO_B].ctun_frequency : vfo[VFO_B].frequency;
  }
  snprintf(msg, MAXMSGSIZE, "vfo:%d,0,%lld;", v, f1);
  send_text(client, msg);
  snprintf(msg, MAXMSGSIZE, "vfo:%d,1,%lld;", v, f2);
  send_text(client, msg);
}

void send_txfreq(CLIENT *client) {
  char msg[MAXMSGSIZE];
  snprintf(msg, MAXMSGSIZE, "tx_frequency:%lld;", vfo_get_tx_freq());
  send_text(client, msg);
}

void send_mode(CLIENT *client, int v) {
  int m = vfo[v].mode;
  const char *mode;
  char msg[MAXMSGSIZE];

  switch (m) {
  case modeLSB:
    mode = "LSB";
    break;
  case modeUSB:
    mode = "USB";
    break;
  case modeDSB:
    mode = "DSB";
    break;
  case modeCWL:
  case modeCWU:
    mode = "CW";
    break;
  case modeFMN:
    mode = "FM";
    break;
  case modeAM:
    mode = "AM";
    break;
  case modeDIGU:
    mode = "DIGU";
    break;
  case modeSPEC:
    mode = "SPEC";
    break;
  case modeDIGL:
    mode = "DIGL";
    break;
  case modeSAM:
    mode = "SAM";
    break;
  case modeDRM:
    mode = "DRM";
    break;
  default:   // should not happen
    mode = "USB";
    break;
  }

  snprintf(msg, MAXMSGSIZE, "modulation:%d,%s;", v, mode);
  send_text(client, msg);
}

void send_close(CLIENT *client) {
  RESPONSE *resp = g_new(RESPONSE, 1);
  if (rigctl_debug) { t_print("TCI%d CLOSE\n"); }
  resp->client = client;
  resp->type   = opCLOSE;
  resp->msg[0] = 0;
  g_idle_add(send_frame, resp);
}

void send_ping(CLIENT *client) {
  RESPONSE *resp = g_new(RESPONSE, 1);
  if (rigctl_debug) { t_print("TCI%d PING\n",client->seq); }
  resp->client = client;
  resp->type   = opPING;
  resp->msg[0] = 0;
  g_idle_add(send_frame, resp);
}

void send_pong(CLIENT *client) {
  RESPONSE *resp = g_new(RESPONSE, 1);
  if (rigctl_debug) { t_print("TCI%d PONG\n"); }
  resp->client = client;
  resp->type   = opPONG;
  resp->msg[0] = 0;
  g_idle_add(send_frame, resp);
}

static gboolean tci_reporter(gpointer data) {
  //
  // This function is called repeatedly as long as the client  runs
  //
  CLIENT *client = (CLIENT *) data;

  if (!client->running) {
    return FALSE;
  }

  if (++(client->count) >= 30) {
    client->count = 0;
    send_ping(client);
  }

  //
  // Determine TX frequency  and  report  if changed
  //
  long long fx = vfo_get_tx_freq();

  if (fx != client->last_fx) {
    send_txfreq(client);
    client->last_fx = fx;
  }

  if (!tci_txonly) {
    //
    // Determine VFO-A/B frequency/mode, report if changed
    //
    long long fa = vfo[VFO_A].ctun ? vfo[VFO_A].ctun_frequency : vfo[VFO_A].frequency;
    long long fb = vfo[VFO_B].ctun ? vfo[VFO_B].ctun_frequency : vfo[VFO_B].frequency;
    int       ma = vfo[VFO_A].mode;
    int       mb = vfo[VFO_B].mode;
 
    if (fa != client->last_fa || fb != client->last_fb) {
      send_vfo(client, 0);
      send_vfo(client, 1);
      client->last_fa = fa;
      client->last_fb = fb;
    }
    if (ma  != client->last_ma) {
      send_mode(client, 0);
      client->last_ma = ma;
    }
    if (mb  != client->last_mb) {
      send_mode(client, 1);
      client->last_mb = mb;
    }
  }

  return TRUE;
}

//
// This is the TCI server, which listens for (and accepts) connections
//
static gpointer tci_server(gpointer data) {
  int port = GPOINTER_TO_INT(data);
  int on = 1;
  struct timeval tv;

  tv.tv_sec = 0;
  tv.tv_usec = 100000;
  t_print("%s: starting TCI server on port %d\n", __FUNCTION__, port);
  server_socket = socket(AF_INET, SOCK_STREAM, 0);

  if (server_socket < 0) {
    t_perror("TCI: listen socket failed");
    return NULL;
  }

  if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0) {
    t_perror("TCISrvReuseAddr");
  }

  if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEPORT, &on, sizeof(on)) < 0) {
    t_perror("TCISrvReUsePort");
  }

  if (setsockopt(server_socket, SOL_SOCKET, SO_RCVTIMEO,  &tv, sizeof(tv)) < 0) {
    t_perror("TCISrvTimeOut");
  }

  // bind to listening port
  memset(&server_address, 0, sizeof(server_address));
  server_address.sin_family = AF_INET;
  server_address.sin_addr.s_addr = INADDR_ANY;
  server_address.sin_port = htons(port);

  if (bind(server_socket, (struct sockaddr * )&server_address, sizeof(server_address)) < 0) {
    t_perror("TCI: listen socket bind failed");
    close(server_socket);
    return NULL;
  }

  for (int id = 0; id < MAX_TCI_CLIENTS; id++) {
    tci_client[id].fd = -1;
  }

  // listen with a max queue of 3
  if (listen(server_socket, 3) < 0) {
    t_perror("TCI: listen failed");
    close(server_socket);
    return NULL;
  }

  while (tci_running) {
    int spare;
    char buf[MAXDATASIZE+1];
    char key[MAXDATASIZE+1];
    unsigned char sha[SHA_DIGEST_LENGTH];
    ssize_t nbytes;
    int  fd;
    char *p;
    char *q;
    //
    // find a spare slot
    //
    spare = -1;

    for (int id = 0; id < MAX_TCI_CLIENTS; id++) {
      if (tci_client[id].fd == -1) {
        spare = id;
        break;
      }
    }

    // if all slots are in use, wait and continue
    if (spare < 0) {
      usleep(100000L);
      continue;
    }

    //
    // A slot is available, try to get connection via accept()
    // (this initializes fd, address, address_length)
    //
    fd = accept(server_socket, (struct sockaddr*)&tci_client[spare].address,
                                  &tci_client[spare].address_length);

    if (fd < 0) {
      // Since we have a 0.1 sec time-out, this is normal
      continue;
    }

    t_print("%s: slot= %d connected with fd=%d\n", __FUNCTION__, spare, fd);

    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO,  &tv, sizeof(tv)) < 0) {
      t_perror("TCIClntSetTimeOut");
    }
    //
    // Setting TCP_NODELAY may (or may not) improve responsiveness
    // by *disabling* Nagle's algorithm for clustering small packets
    //
#ifdef __APPLE__

    if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (void *)&on, sizeof(on)) < 0) {
#else

    if (setsockopt(fd, SOL_TCP, TCP_NODELAY, (void *)&on, sizeof(on)) < 0) {
#endif
      t_perror("TCP_NODELAY");
    }

    //
    // Read from the socket
    //
    nbytes = read(fd, buf, sizeof(buf));
    buf[nbytes] = 0;
    //
    // Try to establish websocket connection. If this fails, close
    // and mark the client as "free":
    //
    // 1. The string obtained must start with GET.
    //
    if (strncmp(buf, "GET",  3)) { close(fd); continue; }

    //
    // 2. Obtain the key embeddded in the message (buf -> key)
    //    and append the TCI magic string (key -> buf)
    //
    p = strstr(buf, "Sec-WebSocket-Key: ");
    if (p == NULL) { close(fd); continue; }

    p +=  19;
    q = key;
    while (*p != '\n' && *p != '\r' && p != 0) {
      *q++ = *p++;
    }
    *q = 0;
    snprintf(buf, MAXDATASIZE, "%s%s", key, "258EAFA5-E914-47DA-95CA-C5AB0DC85B11");
    nbytes=strlen(buf);

    //
    // 3. Get SHA1 hash from the result (buf -> sha)
    //    and convert to base64 (sha -> key)
    //
    SHA1((unsigned char *) buf, nbytes, sha);
    EVP_EncodeBlock((unsigned char *) key, sha, SHA_DIGEST_LENGTH);

    //
    // 4. Send answer back, containing the magic string in key
    //
    snprintf(buf, 1024, 
         "HTTP/1.1 101 Switching Protocols\r\n"
         "Connection: Upgrade\r\n"
         "Upgrade: websocket\r\n"
         "Sec-WebSocket-Accept: %s\r\n\r\n", key);
    nbytes=strlen(buf);
    if (write (fd, buf, nbytes) < nbytes) {
      close(fd);
      continue;
    }

    //
    // If everything worked as expected:
    // Initialize client data structure,
    // spawn off thread that "listens" to the connection,
    // start periodic job that reports frequency/mode changes
    //
    tci_client[spare].fd              = fd;
    tci_client[spare].running         = 1;
    tci_client[spare].seq             = spare;
    tci_client[spare].last_fa         = -1;
    tci_client[spare].last_fb         = -1;
    tci_client[spare].last_fx         = -1;
    tci_client[spare].last_ma         = -1;
    tci_client[spare].last_mb         = -1;
    tci_client[spare].count           =  0;
    tci_client[spare].thread_id       = g_thread_new("TCI listener", tci_listener, (gpointer)&tci_client[spare]);
    tci_client[spare].tci_timer       = g_timeout_add(500, tci_reporter, &tci_client[spare]);
    
  }

  close(server_socket);
  return NULL;
}

int digest_frame(unsigned char *buff, char *msg,  int offset, int *type) {
  //
  // If the buffer contains enough data for a complete frame,
  // produce the payload in "msg" and return the number of
  // frame bytes consumed.
  // If there is not enough data, leave input data untouched
  // and return zero.
  // For a valid frame, return frame type in "type".
  //
  int head = 2;   // number  of bytes preceeding the payload
  int mask = (buff[1] & 0x80);
  int len = (buff[1] & 0x7F);
  int mstrt;

  if (len == 127) {
    // Do not even try
    return 0;
  }

  if (len == 126) {
    len = buff[2] + (buff[3] << 8);
    head = 4;
  }

  if (mask) {
    mstrt = head;  // position where mask starts
    head += 4;
  }

  if (head + len < offset) {
    return 0;
  }

  //
  // There is enough data. Copy/DeMask  it.
  //
  *type = buff[0] & 0x0F;

  for (int i=0; i < len;  i++) {
    if (mask) {
      msg[i]=buff[head+i] ^ buff[mstrt+(i & 3)];
    } else {
      msg[i]=buff[head+i];
    }
  }
  msg[len] = 0;   // form null-terminated  string

  //
  // Return the number of bytes *digested*, not the number of  bytes produced.
  //
  return (head + len);
}

//
// TCI "Listener". It starts with sending  initialisation data, and then
// listens for incoming commands (and ignores all of them!).
// It only responds to incoming PING and CLOSE packets.
//
static gpointer tci_listener(gpointer data) {
  CLIENT *client = (CLIENT *)data;
  t_print("%s: starting client: socket=%d\n", __FUNCTION__, client->fd);
  int numbytes;
  int   offset = 0;
  unsigned char  buff [MAXDATASIZE];
  char  msg  [MAXDATASIZE];
  //
  // Send initial state info to client
  //
  send_text(client, "protocol:ExpertSDR3,1.8;");
  send_text(client, "device:SunSDR2PRO;");
  send_text(client, "receive_only:false;");
  send_text(client, "trx_count:2;");
  send_text(client, "channels_count:2;");
  //
  // With transverters etc. the upper frequency can be
  // very large. For the time being we go up to the 70cm band
  // No need to send vfo and modulation  commands, since this is
  // automatically  done in the tci_reporter task.
  //
  send_text(client, "vfo_limits:0,450000000;");
  send_text(client, "if_limits:-96000,96000;");
  send_text(client, "modulations_list:LSB,USB,DSB,CW,FMN,AM,DIGU,SPEC,DIGL,SAM,DRM;");
  send_dds(client, VFO_A);
  send_dds(client, VFO_B);
  send_text(client, "if:0,0,0;");
  send_text(client, "if:0,1,0;");
  send_text(client, "if:1,0,0;");
  send_text(client, "if:1,1,0;");
  send_text(client, "rx_enable:0,true;");
  send_text(client, "rx_enable:1,true;");
  send_text(client, "tx_enable:0,true;");
  send_text(client, "tx_enable:1,false;");
  send_text(client, "split_enable:0,false;");
  send_text(client, "split_enable:1,false;");
  send_mox(client);
  send_text(client, "trx:1,false;");
  send_text(client, "tune:0,false;");
  send_text(client, "tune:1,false;");
  send_text(client, "mute:false;");
  send_text(client, "start;");
  send_text(client, "ready;");

  while (client->running) {
    int type;
    //
    // This can happen when a very long command has arrived...
    // ...just give up
    //
    if (offset >= MAXDATASIZE) {
      client->running = 0;
      break;
    }
    numbytes = recv(client->fd, buff+offset, MAXDATASIZE-offset, 0);
    if (numbytes <= 0) {
      usleep(100000);
      continue;
    }
    offset += numbytes;
    //
    // If there is enough data in the frame, process it
    //
    numbytes =  digest_frame(buff, msg, offset, &type);
    if (numbytes > 0) {
      switch(type) {
      case opTEXT:
        if (rigctl_debug) { t_print("TCI%d command rcvd=%s\n", client->seq, msg); }
        break;
      case opPING:
        if (rigctl_debug) { t_print("TCI%d PING rcvd\n", client->seq); }
        send_pong(client);
        break;
      case opCLOSE:
        if (rigctl_debug) { t_print("TCI%d CLOSE rcvd\n", client->seq); }
        client->running = 0;
        break;
      }
      //
      // Remove the just-processed frame from the input buffer
      // In normal operation, offset will be set to zero here.
      //
      offset  -= numbytes;
      if (offset > 0) {
        for (int i=0; i<offset; i++) {
          buff[i]=buff[i+numbytes];
        }
      }
    }
  }

  send_text(client, "stop;");
  send_close(client);
  force_close(client);
  t_print("%s: leaving thread\n", __FUNCTION__);
  return NULL;
}
