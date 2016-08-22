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

#include "lwip/opt.h"
#include "lwip/tcp.h"
#include "lwip/inet.h"
#include "lwipr_compat.h"

#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>

static axl_tcp_t * axl_tcp_array = NULL;
static int axl_tcp_next_fd = 0;

axl_tcp_t * axl_new(struct tcp_pcb *tcp) {
  if(tcp == NULL) {
    return NULL;
  }

  axl_tcp_t * new_item = (axl_tcp_t*)malloc(sizeof(axl_tcp_t));
  if(!new_item){
    AXL_DEBUG("new_item == NULL\n");
    return NULL;
  }

  if(axl_tcp_next_fd < 0){
    axl_tcp_next_fd = 0;//overflow
  }

  new_item->tcp = tcp;
  new_item->handshake = SSL_NOT_OK;
  new_item->arg = NULL;
  new_item->on_data = NULL;
  new_item->on_handshake = NULL;
  new_item->on_error = NULL;
  new_item->tcp_pbuf = NULL;
  new_item->pbuf_offset = 0;
  new_item->next = NULL;
  new_item->ssl_ctx = ssl_ctx_new(SSL_CONNECT_IN_PARTS | SSL_SERVER_VERIFY_LATER, 1);
  if(new_item->ssl_ctx == NULL){
    AXL_DEBUG("new_item->ssl_ctx == NULL\n");
    free(new_item);
    return NULL;
  }

  new_item->fd = axl_tcp_next_fd++;
  if(axl_tcp_array == NULL){
    axl_tcp_array = new_item;
  } else {
    axl_tcp_t * item = axl_tcp_array;
    while(item->next != NULL)
      item = item->next;
    item->next = new_item;
  }

  new_item->ssl = ssl_client_new(new_item->ssl_ctx, new_item->fd, NULL, 0, NULL);
  if(new_item->ssl == NULL){
    AXL_DEBUG("new_item->ssl == NULL\n");
    axl_free(tcp);
    return NULL;
  }

  //AXL_DEBUG("axl_new: %d\n", new_item->fd);
  return new_item;
}

void axl_arg(struct tcp_pcb *tcp, void * arg){
  axl_tcp_t * item = axl_get(tcp);
  if(item) {
    item->arg = arg;
  }
}

void axl_data(struct tcp_pcb *tcp, axl_data_cb_t arg){
  axl_tcp_t * item = axl_get(tcp);
  if(item) {
    item->on_data = arg;
  }
}

void axl_handshake(struct tcp_pcb *tcp, axl_handshake_cb_t arg){
  axl_tcp_t * item = axl_get(tcp);
  if(item) {
    item->on_handshake = arg;
  }
}

void axl_err(struct tcp_pcb *tcp, axl_error_cb_t arg){
  axl_tcp_t * item = axl_get(tcp);
  if(item) {
    item->on_error = arg;
  }
}

int axl_free(struct tcp_pcb *tcp) {

  if(tcp == NULL) {
    return -1;
  }

  axl_tcp_t * item = axl_tcp_array;

  if(item->tcp == tcp){
    axl_tcp_array = axl_tcp_array->next;
    if(item->tcp_pbuf != NULL){
      pbuf_free(item->tcp_pbuf);
    }
    if(item->ssl)
      ssl_free(item->ssl);
    if(item->ssl_ctx)
      ssl_ctx_free(item->ssl_ctx);
    free(item);
    return 0;
  }

  while(item->next && item->next->tcp != tcp)
    item = item->next;

  if(item->next == NULL){
    return ERR_AXL_INVALID_CLIENTFD_DATA;//item not found
  }

  axl_tcp_t * i = item->next;
  item->next = i->next;
  if(i->tcp_pbuf != NULL){
    pbuf_free(i->tcp_pbuf);
  }
  free(i);

  return 0;
}

axl_tcp_t* axl_get(struct tcp_pcb *tcp) {
  if(tcp == NULL) {
    return NULL;
  }
  axl_tcp_t * item = axl_tcp_array;
  while(item && item->tcp != tcp){
    item = item->next;
  }
  return item;
}

/**
 * Reads data from the SSL over TCP stream. Returns decrypted data.
 * @param tcp_pcb *tcp - pointer to the raw tcp object
 * @param pbuf *p - pointer to the buffer with the TCP packet data
 *
 * @return int
 *      0 - when everything is fine but there are no symbols to process yet
 *      < 0 - when there is an error
 *      > 0 - the length of the clear text characters that were read
 */
int axl_read(struct tcp_pcb *tcp, struct pbuf *p) {
  if(tcp == NULL) {
    return -1;
  }
  axl_tcp_t* fd_data = NULL;

  int read_bytes = 0;
  int total_bytes = 0;
  uint8_t *read_buf;

  fd_data = axl_get(tcp);
  if(fd_data == NULL) {
    AXL_DEBUG("axl_ssl_read:fd_data == NULL\n");
    return ERR_AXL_INVALID_CLIENTFD_DATA;
  }

  if(p == NULL) {
    AXL_DEBUG("axl_ssl_read:p == NULL\n");
    return ERR_AXL_INVALID_DATA;
  }

  //AXL_DEBUG("READY TO READ SOME DATA\n");

  fd_data->tcp_pbuf = p;
  fd_data->pbuf_offset = 0;

  do {
    read_bytes = ssl_read(fd_data->ssl, &read_buf);
    //AXL_DEBUG("axl_ssl_read: %d\n", read_bytes);
    if(read_bytes < SSL_OK) {
      /* An error has occurred. Give it back for further processing */
      total_bytes = read_bytes;
      break;
    } else if(read_bytes > 0){
      if(fd_data->on_data){
        fd_data->on_data(fd_data->arg, tcp, read_buf, read_bytes);
      }
      total_bytes+= read_bytes;
    }
    if(fd_data->handshake != SSL_OK) {
      fd_data->handshake = ssl_handshake_status(fd_data->ssl);
      if(fd_data->handshake == SSL_OK){
        if(fd_data->on_handshake)
          fd_data->on_handshake(fd_data->arg, tcp, fd_data->ssl);
      } else if(fd_data->handshake != SSL_NOT_OK){
        AXL_DEBUG("axl_read: handshake error: %d\n", fd_data->handshake);
        if(fd_data->on_error)
          fd_data->on_error(fd_data->arg, tcp, fd_data->handshake);
        return fd_data->handshake;
      }
    }
  } while (p->tot_len - fd_data->pbuf_offset > 0);

  tcp_recved(tcp, p->tot_len);
  pbuf_free(p);

  return total_bytes;
}

int axl_write(struct tcp_pcb *tcp, uint8_t *data, size_t len) {
  if(tcp == NULL) {
    return -1;
  }
  axl_tcp_t * axl = axl_get(tcp);
  if(!axl){
    return 0;
  }
  axl->last_wr = 0;

  int rc = ssl_write(axl->ssl, data, len);

  if (rc < 0){
    if(rc != SSL_CLOSE_NOTIFY) {
      AXL_DEBUG("axl_write failed: %d\r\n", rc);
    }
    return rc;
  }
  return axl->last_wr;
}



axl_tcp_t* axl_get_by_fd(int fd) {
  axl_tcp_t * item = axl_tcp_array;
  while(item && item->fd != fd){
    item = item->next;
  }
  return item;
}
/*
 * The LWIP tcp raw version of the SOCKET_WRITE(A, B, C)
 */
int ax_port_write(int fd, uint8_t *data, uint16_t len) {
  axl_tcp_t *fd_data = NULL;
  int tcp_len = 0;
  err_t err = ERR_OK;

  //AXL_DEBUG("ax_port_write: %d, %d\n", fd, len);

  fd_data = axl_get_by_fd(fd);
  if(fd_data == NULL) {
    AXL_DEBUG("ax_port_write:fd_data == NULL\n");
    return ERR_AXL_INVALID_CLIENTFD;
  }

  if (fd_data->tcp == NULL || data == NULL || len == 0) {
    AXL_DEBUG("Return Zero.\n");
    return 0;
  }

  if (tcp_sndbuf(fd_data->tcp) < len) {
    tcp_len = tcp_sndbuf(fd_data->tcp);
    if(tcp_len == 0) {
      AXL_DEBUG("The send buffer is full! We have problem.\n");
      return 0;
    }

  } else {
    tcp_len = len;
  }

  if (tcp_len > 2 * fd_data->tcp->mss) {
    tcp_len = 2 * fd_data->tcp->mss;
  }

  do {
    err = tcp_write(fd_data->tcp, data, tcp_len, TCP_WRITE_FLAG_COPY);
    if(err < SSL_OK) {
      AXL_DEBUG("Got error: %d\n", err);
    }

    if (err == ERR_MEM) {
      AXL_DEBUG("Not enough memory to write data with length: %d (%d)\n", tcp_len, len);
      tcp_len /= 2;
    }
  } while (err == ERR_MEM && tcp_len > 1);
  if (err == ERR_OK) {
    AXL_DEBUG("send_raw_packet length %d(%d)\n", tcp_len, len);
    err = tcp_output(fd_data->tcp);
    if(err != ERR_OK) {
      AXL_DEBUG("tcp_output got err: %d\n", err);
    }
  }

  fd_data->last_wr += tcp_len;

  return tcp_len;
}

/*
 * The LWIP tcp raw version of the SOCKET_READ(A, B, C)
 */
int ax_port_read(int fd, uint8_t *data, int len) {
  axl_tcp_t *fd_data = NULL;
  uint8_t *read_buf = NULL;
  uint8_t *pread_buf = NULL;
  u16_t recv_len = 0;

  //AXL_DEBUG("ax_port_read: %d, %d\n", fd, len);

  fd_data = axl_get_by_fd(fd);
  if (fd_data == NULL) {
    AXL_DEBUG("ax_port_read:fd_data == NULL\n");
    return ERR_AXL_INVALID_CLIENTFD_DATA;
  }

  if(fd_data->tcp_pbuf == NULL || fd_data->tcp_pbuf->tot_len == 0) {
    AXL_DEBUG("Nothing to read?! May be the connection needs resetting?\n");
    return 0;
  }

  read_buf =(uint8_t*)calloc(fd_data->tcp_pbuf->len + 1, sizeof(uint8_t));
  pread_buf = read_buf;
  if (pread_buf != NULL){
    recv_len = pbuf_copy_partial(fd_data->tcp_pbuf, read_buf, len, fd_data->pbuf_offset);
    fd_data->pbuf_offset += recv_len;
  }

  if (recv_len != 0) {
    memcpy(data, read_buf, recv_len);
  }

  if(len < recv_len) {
    AXL_DEBUG("Bytes needed: %d, Bytes read: %d\n", len, recv_len);
  }

  free(pread_buf);
  pread_buf = NULL;

  return recv_len;
}

int ax_get_file(const char *filename, uint8_t **buf) {
    AXL_DEBUG("ax_get_file: %s\n", filename);
    *buf = 0;
    return 0;
}

void ax_wdt_feed() {
}
