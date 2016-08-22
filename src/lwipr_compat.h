/*
  Asynchronous TCP library for Espressif MCUs

  Copyright (c) 2016 Hristo Gochkov. All rights reserved.
  This file is part of the esp8266 core for Arduino environment.

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/
/*
 * Compatibility for AxTLS with LWIP raw tcp mode (http://lwip.wikia.com/wiki/Raw/TCP)
 * Original Code and Inspiration: Slavey Karadzhov
 */

#ifndef LWIPR_COMPAT_H
#define LWIPR_COMPAT_H

#include "lwipopts.h"
/*
 * All those functions will run only if LWIP tcp raw mode is used
 */
#if LWIP_RAW==1

#ifdef __cplusplus
extern "C" {
#endif

#include "include/ssl.h"

#define ERR_AXL_INVALID_SSL           -101
#define ERR_AXL_INVALID_TCP           -102
#define ERR_AXL_INVALID_CLIENTFD      -103
#define ERR_AXL_INVALID_CLIENTFD_DATA -104
#define ERR_AXL_INVALID_DATA          -105

#define axl_ssl_write(A, B, C) axl_write(A, B, C)
#define axl_ssl_read(A, B) axl_read(A, B)

#ifndef AXL_DEBUG
  #define AXL_DEBUG(...) //ets_printf(__VA_ARGS__)
#endif

typedef void (* axl_data_cb_t)(void *arg, struct tcp_pcb *tcp, uint8_t * data, size_t len);
typedef void (* axl_handshake_cb_t)(void *arg, struct tcp_pcb *tcp, SSL *ssl);
typedef void (* axl_error_cb_t)(void *arg, struct tcp_pcb *tcp, int8_t error);

struct axl_tcp {
  struct tcp_pcb *tcp;
  int fd;
  SSL_CTX* ssl_ctx;
  SSL *ssl;
  int handshake;
  void * arg;
  axl_data_cb_t on_data;
  axl_handshake_cb_t on_handshake;
  axl_error_cb_t on_error;
  int last_wr;
  struct pbuf *tcp_pbuf;
  int pbuf_offset;
  struct axl_tcp * next;
};

typedef struct axl_tcp axl_tcp_t;

axl_tcp_t* axl_new(struct tcp_pcb *tcp);
axl_tcp_t* axl_get(struct tcp_pcb *tcp);

int axl_free(struct tcp_pcb *tcp);
int axl_read(struct tcp_pcb *tcp, struct pbuf *p);
int axl_write(struct tcp_pcb *tcp, uint8_t *data, size_t len);

void axl_arg(struct tcp_pcb *tcp, void * arg);
void axl_data(struct tcp_pcb *tcp, axl_data_cb_t arg);
void axl_handshake(struct tcp_pcb *tcp, axl_handshake_cb_t arg);
void axl_err(struct tcp_pcb *tcp, axl_error_cb_t arg);

#ifdef __cplusplus
}
#endif

#endif /* LWIP_RAW==1 */

#endif /* LWIPR_COMPAT_H */
