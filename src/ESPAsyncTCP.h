/*
 * AsyncTCP.h
 *
 *  Created on: 1.12.2015 г.
 *      Author: ficeto
 */

#ifndef ASYNCTCP_H_
#define ASYNCTCP_H_


extern "C" {
  #include "lwip/tcp.h"
}
#include "IPAddress.h"
#include <ESP8266WiFi.h>

#include <functional>

#define USE_ASYNC_BUFFER 0
#define SERVER_KEEP_CLIENTS 0
#define CLIENT_SYNC_API 0

class AsyncClient;

#define ASYNC_MAX_ACK_TIME 5000

typedef std::function<void(void*, AsyncClient*)> AcConnectHandler;
typedef std::function<void(void*, AsyncClient*, size_t len, uint32_t time)> AcAckHandler;
typedef std::function<void(void*, AsyncClient*, err_t error)> AcErrorHandler;
typedef std::function<void(void*, AsyncClient*, void *data, size_t len)> AcDataHandler;
typedef std::function<void(void*, AsyncClient*, uint32_t time)> AcTimeoutHandler;

class AsyncClient {
  private:
    tcp_pcb* _pcb;
    AcConnectHandler _connect_cb;
    void* _connect_cb_arg;
    AcConnectHandler _discard_cb;
    void* _discard_cb_arg;
    AcAckHandler _sent_cb;
    void* _sent_cb_arg;
    AcErrorHandler _error_cb;
    void* _error_cb_arg;
    AcDataHandler _recv_cb;
    void* _recv_cb_arg;
    AcTimeoutHandler _timeout_cb;
    void* _timeout_cb_arg;
    AcConnectHandler _poll_cb;
    void* _poll_cb_arg;
    int _refcnt;
    bool _pcb_busy;
    uint32_t _pcb_sent_at;
    bool _close_pcb;
    uint32_t _rx_last_packet;
    uint32_t _rx_since_timeout;

    err_t _close();
    err_t _connected(void* pcb, int8_t err);
    void _error(err_t err);
    err_t _poll(tcp_pcb* pcb);
    err_t _sent(tcp_pcb* pcb, uint16_t len);
    err_t _recv(tcp_pcb* pcb, pbuf* pb, err_t err);
    static err_t _s_poll(void *arg, struct tcp_pcb *tpcb);
    static err_t _s_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *pb, err_t err);
    static void _s_error(void *arg, err_t err);
    static err_t _s_sent(void *arg, struct tcp_pcb *tpcb, uint16_t len);
    static err_t _s_connected(void* arg, void* tpcb, int8_t err);

  public:
    AsyncClient* prev;
    AsyncClient* next;

    AsyncClient(tcp_pcb* pcb = 0);
    ~AsyncClient();

    AsyncClient & operator=(const AsyncClient &other);
    AsyncClient & operator+=(const AsyncClient &other);

    bool operator==(const AsyncClient &other) {
      return (_pcb != NULL && other._pcb != NULL && (_pcb->remote_ip.addr == other._pcb->remote_ip.addr) && (_pcb->remote_port == other._pcb->remote_port));
    }

    bool operator!=(const AsyncClient &other) {
      return !(*this == other);
    }

    bool connect(IPAddress ip, uint16_t port);
    bool connect(const char* host, uint16_t port);
    void close();
    //err_t _close();
    err_t abort();
    bool free();

    bool canSend();//ack is not pending
    size_t space(){ return tcp_sndbuf(_pcb);}
    size_t write(const char* data, size_t size); //only when canSend() == true

    uint8_t state();
    bool connecting();
    bool connected();
    bool disconnecting();
    bool disconnected();
    bool freeable();//disconnected or disconnecting

    uint32_t getRxTimeout();
    void setRxTimeout(uint32_t timeout);//no RX data timeout for the connection in seconds
    void setNoDelay(bool nodelay);
    bool getNoDelay();
    uint32_t getRemoteAddress();
    uint16_t getRemotePort();
    uint32_t getLocalAddress();
    uint16_t getLocalPort();


    void onConnect(AcConnectHandler cb, void* arg = 0);     //on successful connect
    void onDisconnect(AcConnectHandler cb, void* arg = 0);  //disconnected
    void onAck(AcAckHandler cb, void* arg = 0);             //ack received
    void onError(AcErrorHandler cb, void* arg = 0);         //unsuccessful connect or error
    void onData(AcDataHandler cb, void* arg = 0);           //data received
    void onTimeout(AcTimeoutHandler cb, void* arg = 0);     //ack timeout
    void onPoll(AcConnectHandler cb, void* arg = 0);        //every 125ms when connected
};

class AsyncServer {
  private:
    uint16_t _port;
    IPAddress _addr;
    tcp_pcb* _pcb;
    AcConnectHandler _connect_cb;
    void* _connect_cb_arg;

  public:
    AsyncServer(IPAddress addr, uint16_t port);
    AsyncServer(uint16_t port);
    ~AsyncServer();
    void onClient(AcConnectHandler cb, void* arg);
    void begin();
    void end();
    void setNoDelay(bool nodelay);
    bool getNoDelay();
    uint8_t status();

  protected:
    int8_t _accept(tcp_pcb* newpcb, int8_t err);
    static int8_t _s_accept(void *arg, tcp_pcb* newpcb, int8_t err);
};


#endif /* ASYNCTCP_H_ */
