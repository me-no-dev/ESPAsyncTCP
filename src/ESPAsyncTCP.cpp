
#include "Arduino.h"
#include "debug.h"

extern "C"{
  #include "lwip/opt.h"
  #include "lwip/tcp.h"
  #include "lwip/inet.h"
}

#include "ESPAsyncTCP.h"
static uint16_t _localPort = 10000;

/*
  Async TCP Client
*/

AsyncClient::AsyncClient(tcp_pcb* pcb):
#if CLIENT_SYNC_API
  _rx_buf(0)
  , _rx_buf_offset(0),
#endif
#if USE_ASYNC_BUFFER
  _tx_buf(0),
#endif
  _connect_cb(0)
  , _connect_cb_arg(0)
  , _discard_cb(0)
  , _discard_cb_arg(0)
  , _sent_cb(0)
  , _sent_cb_arg(0)
  , _error_cb(0)
  , _error_cb_arg(0)
  , _recv_cb(0)
  , _recv_cb_arg(0)
  , _timeout_cb(0)
  , _timeout_cb_arg(0)
  , _refcnt(0)
  , _pcb_busy(false)
  , _pcb_sent_at(0)
  , _close_pcb(false)
  , _rx_last_packet(0)
  , _rx_since_timeout(0)
  , prev(NULL)
  , next(NULL)
{
  _pcb = pcb;
  if(pcb){
    tcp_setprio(_pcb, TCP_PRIO_MIN);
    tcp_arg(_pcb, this);
    tcp_recv(_pcb, &_s_recv);
    tcp_sent(_pcb, &_s_sent);
    tcp_err(_pcb, &_s_error);
    tcp_poll(_pcb, &_s_poll, 1);
  }
}

AsyncClient::~AsyncClient(){
  if (_pcb){
    abort();
    free();
  }
}

bool AsyncClient::connect(IPAddress ip, uint16_t port){
  ip_addr_t addr;
  addr.addr = ip;

  if (_pcb){
    return false;
  }

  netif* interface = ip_route(&addr);
  if (!interface){
    return false;
  }

  tcp_pcb* pcb = tcp_new();
  if (!pcb)
    return false;

  pcb->local_port = _localPort++;

  tcp_arg(pcb, this);
  tcp_err(pcb, &_s_error);
  tcp_connect(pcb, &addr, port,(tcp_connected_fn)&_s_connected);
  return true;
}

AsyncClient& AsyncClient::operator=(const AsyncClient& other){
  if (_pcb){
    abort();
    free();
  }
  _pcb = other._pcb;
  tcp_setprio(_pcb, TCP_PRIO_MIN);
  tcp_arg(_pcb, this);
  tcp_recv(_pcb, &_s_recv);
  tcp_sent(_pcb, &_s_sent);
  tcp_err(_pcb, &_s_error);
  tcp_poll(_pcb, &_s_poll, 1);
  return *this;
}


err_t AsyncClient::abort(){
  if(_pcb)
    tcp_abort(_pcb);
  return ERR_ABRT;
}

void AsyncClient::close(){
  _close_pcb = true;
}

bool AsyncClient::free(){
  if(!_pcb)
    return true;
  if(_pcb->state == CLOSED || _pcb->state > ESTABLISHED){
    tcp_arg(_pcb, NULL);
    tcp_sent(_pcb, NULL);
    tcp_recv(_pcb, NULL);
    tcp_err(_pcb, NULL);
    tcp_poll(_pcb, NULL, 0);
    _pcb = NULL;
    prev = NULL;
    next = NULL;
    return true;
  }
  return false;
}

size_t AsyncClient::write(const char* data, size_t size) {
  if(!_pcb || size == 0)
    return 0;
#if USE_ASYNC_BUFFER
  AsyncBuffer *b = new AsyncBuffer(size);
  if(!b->payload)
    return 0;
  memcpy(b->payload, data, size);
  if(!_tx_buf){
    _tx_buf = b;
  } else {
    AsyncBuffer *l = _tx_buf;
    while(l->next != NULL) l = l->next;
    l->next = b;
  }
  return size;
#else
  if(!canSend())
    return 0;
  size_t room = tcp_sndbuf(_pcb);
  size_t will_send = (room < size) ? room : size;
  err_t err = tcp_write(_pcb, data, will_send, 0);
  if(err != ERR_OK)
    return 0;
  tcp_output(_pcb);
  _pcb_sent_at = millis();
  _pcb_busy = true;
  if(will_send < size){
    size_t left = size - will_send;
    return will_send + write(data+will_send, left);
  }
  return size;
#endif
}

// Private Callbacks

err_t AsyncClient::_close(){
  err_t err = ERR_OK;
  if(_pcb) {
    err = tcp_close(_pcb);
    if(err != ERR_OK) {
      err = abort();
    }
    if(_discard_cb)
      _discard_cb(_discard_cb_arg, this);
  }
  return err;
}

int8_t AsyncClient::_connected(void* pcb, int8_t err){
  _pcb = reinterpret_cast<tcp_pcb*>(pcb);
  if(_pcb){
    tcp_setprio(_pcb, TCP_PRIO_MIN);
    tcp_recv(_pcb, &_s_recv);
    tcp_sent(_pcb, &_s_sent);
    tcp_poll(_pcb, &_s_poll, 1);
    _pcb_busy = false;
  }
  if(_connect_cb)
    _connect_cb(_connect_cb_arg, this);
  return ERR_OK;
}

void AsyncClient::_error(err_t err) {
  if(_error_cb)
    _error_cb(_error_cb_arg, this, err);
  if(err){
    _pcb->state = CLOSED;
    if(_discard_cb)
      _discard_cb(_discard_cb_arg, this);
  }
}

err_t AsyncClient::_sent(tcp_pcb* pcb, uint16_t len) {
  _rx_last_packet = millis();
#if USE_ASYNC_BUFFER
  if(_tx_buf){
    AsyncBuffer *b = _tx_buf;
    size_t size = b->size - b->index;
    size_t room = tcp_sndbuf(_pcb);
    err_t err;
    size_t will_send = (room < size) ? room : size;
    err = tcp_write(_pcb, (uint8_t*)(b->payload)+b->index, will_send, 0);
    if(err == ERR_OK){
      b->index += will_send;
      if(b->index == b->size)
        _tx_buf = _tx_buf->next;
      tcp_output(_pcb);
      _pcb_sent_at = millis();
    } else
      _pcb_busy = false;
  } else
#endif
    _pcb_busy = false;
  if(_sent_cb)
    _sent_cb(_sent_cb_arg, this, len, (millis() - _pcb_sent_at));
  return ERR_OK;
}

err_t AsyncClient::_recv(tcp_pcb* pcb, pbuf* pb, err_t err) {
  if(pb == 0){
    //os_printf("pb-null\n");
    return _close();
  }

  _rx_last_packet = millis();

#if CLIENT_SYNC_API
  if(_recv_cb){
#endif
    //use callback (onData defined)
    while(pb != NULL){
      if(pb->next == NULL){
        if(_recv_cb)
          _recv_cb(_recv_cb_arg, this, pb->payload, pb->len);
        tcp_recved(pcb, pb->len);
        pbuf_free(pb);
        pb = NULL;
      } else {
        pbuf *b = pb;
        pb = pbuf_dechain(b);
        if(_recv_cb)
          _recv_cb(_recv_cb_arg, this, b->payload, b->len);
        tcp_recved(pcb, b->len);
        pbuf_free(b);
      }
    }
#if CLIENT_SYNC_API
  } else {
    //use buffer (onData not defined)
    if(_rx_buf) {
      pbuf_cat(_rx_buf, pb);
    } else {
      _rx_buf = pb;
      _rx_buf_offset = 0;
    }
  }
#endif
  return ERR_OK;
}

err_t AsyncClient::_poll(tcp_pcb* pcb){
  // Close requested
  if(_close_pcb){
    _close_pcb = false;
    _close();
    return ERR_OK;
  }
  uint32_t now = millis();
  // ACK Timeout
  if(_pcb_busy && (now - _pcb_sent_at) >= ASYNC_MAX_ACK_TIME){
    _pcb_busy = false;
    if(_timeout_cb)
      _timeout_cb(_timeout_cb_arg, this, (now - _pcb_sent_at));
    return ERR_OK;
  }
  // RX Timeout
  if(_rx_since_timeout && now - _rx_last_packet >= _rx_since_timeout * 1000){
    _close();
    return ERR_OK;
  }
  // Everything is fine
#if USE_ASYNC_BUFFER
  if(!_pcb_busy && _tx_buf){
    AsyncBuffer *b = _tx_buf;
    size_t size = b->size - b->index;
    size_t room = tcp_sndbuf(_pcb);
    err_t err;
    size_t will_send = (room < size) ? room : size;
    err = tcp_write(_pcb, (uint8_t*)(b->payload)+b->index, will_send, 0);
    if(err == ERR_OK){
      b->index += will_send;
      if(b->index == b->size)
        _tx_buf = _tx_buf->next;
      _pcb_busy = true;
      tcp_output(_pcb);
      _pcb_sent_at = millis();
    }
  }
#endif
  if(!_pcb_busy && _poll_cb)
    _poll_cb(_poll_cb_arg, this);
  return ERR_OK;
}

// lWIP Callbacks

err_t AsyncClient::_s_poll(void *arg, struct tcp_pcb *tpcb) {
  return reinterpret_cast<AsyncClient*>(arg)->_poll(tpcb);
}

err_t AsyncClient::_s_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *pb, err_t err) {
  return reinterpret_cast<AsyncClient*>(arg)->_recv(tpcb, pb, err);
}

void AsyncClient::_s_error(void *arg, err_t err) {
  reinterpret_cast<AsyncClient*>(arg)->_error(err);
}

err_t AsyncClient::_s_sent(void *arg, struct tcp_pcb *tpcb, uint16_t len) {
  return reinterpret_cast<AsyncClient*>(arg)->_sent(tpcb, len);
}

int8_t AsyncClient::_s_connected(void* arg, void* tpcb, int8_t err){
    return reinterpret_cast<AsyncClient*>(arg)->_connected(tpcb, err);
}

// Operators

AsyncClient & AsyncClient::operator+=(const AsyncClient &other) {
  if(next == NULL){
    next = (AsyncClient*)(&other);
    next->prev = this;
  } else {
    AsyncClient *c = next;
    while(c->next != NULL) c = c->next;
    c->next =(AsyncClient*)(&other);
    c->next->prev = c;
  }
  return *this;
}


// Client Info

bool AsyncClient::connect(const char* host, uint16_t port){
  IPAddress remote_addr;
  if (WiFi.hostByName(host, remote_addr))
    return connect(remote_addr, port);
  return false;
}

void AsyncClient::setRxTimeout(uint32_t timeout){
  _rx_since_timeout = timeout;
}

uint32_t AsyncClient::getRxTimeout(){
  return _rx_since_timeout;
}

void AsyncClient::setNoDelay(bool nodelay){
  if(!_pcb) return;
  if(nodelay) tcp_nagle_disable(_pcb);
  else tcp_nagle_enable(_pcb);
}

bool AsyncClient::getNoDelay(){
  if(!_pcb) return false;
  return tcp_nagle_disabled(_pcb);
}

uint32_t AsyncClient::getRemoteAddress() {
  if(!_pcb) return 0;
  return _pcb->remote_ip.addr;
}

uint16_t AsyncClient::getRemotePort() {
  if(!_pcb) return 0;
  return _pcb->remote_port;
}

uint32_t AsyncClient::getLocalAddress() {
  if(!_pcb) return 0;
  return _pcb->local_ip.addr;
}

uint16_t AsyncClient::getLocalPort() {
  if(!_pcb) return 0;
  return _pcb->local_port;
}

uint8_t AsyncClient::state() {
  if(!_pcb) return 0;
  return _pcb->state;
}

bool AsyncClient::connected(){
  if (!_pcb)
    return false;
  return _pcb->state == ESTABLISHED;
}

bool AsyncClient::connecting(){
  if (!_pcb)
    return false;
  return _pcb->state > CLOSED && _pcb->state < ESTABLISHED;
}

bool AsyncClient::disconnecting(){
  if (!_pcb)
    return false;
  return _pcb->state > ESTABLISHED && _pcb->state < TIME_WAIT;
}

bool AsyncClient::disconnected(){
  if (!_pcb)
    return true;
  return _pcb->state == CLOSED || _pcb->state == TIME_WAIT;
}

bool AsyncClient::freeable(){
  if (!_pcb)
    return true;
  return _pcb->state == CLOSED || _pcb->state > ESTABLISHED;
}

bool AsyncClient::canSend(){
  return _pcb && _pcb->state == ESTABLISHED && !_pcb_busy;
}


// Callback Setters

void AsyncClient::onConnect(AcConnectHandler cb, void* arg){
  _connect_cb = cb;
  _connect_cb_arg = arg;
}

void AsyncClient::onDisconnect(AcConnectHandler cb, void* arg){
  _discard_cb = cb;
  _discard_cb_arg = arg;
}

void AsyncClient::onAck(AcAckHandler cb, void* arg){
  _sent_cb = cb;
  _sent_cb_arg = arg;
}

void AsyncClient::onError(AcErrorHandler cb, void* arg){
  _error_cb = cb;
  _error_cb_arg = arg;
}

void AsyncClient::onData(AcDataHandler cb, void* arg){
  _recv_cb = cb;
  _recv_cb_arg = arg;
}

void AsyncClient::onTimeout(AcTimeoutHandler cb, void* arg){
  _timeout_cb = cb;
  _timeout_cb_arg = arg;
}

void AsyncClient::onPoll(AcConnectHandler cb, void* arg){
  _poll_cb = cb;
  _poll_cb_arg = arg;
}

#if CLIENT_SYNC_API
void AsyncClient::_consume(size_t size) {
  ptrdiff_t left = _rx_buf->len - _rx_buf_offset - size;
  if(left > 0) {
    _rx_buf_offset += size;
  } else if(!_rx_buf->next) {
    if(_pcb) tcp_recved(_pcb, _rx_buf->len);
    pbuf_free(_rx_buf);
    _rx_buf = 0;
    _rx_buf_offset = 0;
  } else {
    auto head = _rx_buf;
    _rx_buf = _rx_buf->next;
    _rx_buf_offset = 0;
    pbuf_ref(_rx_buf);
    if(_pcb) tcp_recved(_pcb, head->len);
    pbuf_free(head);
  }
}

size_t AsyncClient::available() {
  if(!_rx_buf) return 0;
  return _rx_buf->tot_len - _rx_buf_offset;
}

char AsyncClient::read() {
  if(!_rx_buf) return 0;
  char c = reinterpret_cast<char*>(_rx_buf->payload)[_rx_buf_offset];
  _consume(1);
  return c;
}

size_t AsyncClient::read(char* dst, size_t size) {
  if(!_rx_buf) return 0;
  size_t max_size = _rx_buf->tot_len - _rx_buf_offset;
  size = (size < max_size) ? size : max_size;
  size_t size_read = 0;
  while(size) {
    size_t buf_size = _rx_buf->len - _rx_buf_offset;
    size_t copy_size = (size < buf_size) ? size : buf_size;
    os_memcpy(dst, reinterpret_cast<char*>(_rx_buf->payload) + _rx_buf_offset, copy_size);
    dst += copy_size;
    _consume(copy_size);
    size -= copy_size;
    size_read += copy_size;
  }
  return size_read;
}

char AsyncClient::peek() {
  if(!_rx_buf) return 0;
  return reinterpret_cast<char*>(_rx_buf->payload)[_rx_buf_offset];
}

void AsyncClient::flush() {
  if(_pcb && _rx_buf->tot_len) {
    tcp_recved(_pcb, (size_t) _rx_buf->tot_len);
  }
  pbuf_free(_rx_buf);
  _rx_buf = 0;
  _rx_buf_offset = 0;
}
#endif
/*
  Async TCP Server
*/

AsyncServer::AsyncServer(IPAddress addr, uint16_t port)
  : _port(port)
  , _addr(addr)
  , _pcb(0)
  , _connect_cb(0)
  , _connect_cb_arg(0)
#if SERVER_KEEP_CLIENTS
  , _clients(NULL)
#endif
{}

AsyncServer::AsyncServer(uint16_t port)
  : _port(port)
  , _addr((uint32_t) IPADDR_ANY)
  , _pcb(0)
  , _connect_cb(0)
  , _connect_cb_arg(0)
#if SERVER_KEEP_CLIENTS
  , _clients(NULL)
#endif
{}

AsyncServer::~AsyncServer(){}

void AsyncServer::onClient(AcConnectHandler cb, void* arg){
  _connect_cb = cb;
  _connect_cb_arg = arg;
}

void AsyncServer::begin(){
  if(_pcb)
    return;

  err_t err;
  tcp_pcb* pcb = tcp_new();
  if (!pcb)
    return;

  ip_addr_t local_addr;
  local_addr.addr = (uint32_t) _addr;
  err = tcp_bind(pcb, &local_addr, _port);

  if (err != ERR_OK) {
    tcp_close(pcb);
    return;
  }

  tcp_pcb* listen_pcb = tcp_listen(pcb);
  if (!listen_pcb) {
    tcp_close(pcb);
    return;
  }
  _pcb = listen_pcb;
  tcp_arg(_pcb, (void*) this);
  tcp_accept(_pcb, _s_accept);
}

void AsyncServer::end(){
  if(_pcb){
    //cleanup all connections?
    tcp_abort(_pcb);
    tcp_arg(_pcb, NULL);
    tcp_accept(_pcb, NULL);
    _pcb = NULL;
  }
}

void AsyncServer::setNoDelay(bool nodelay){
  if (!_pcb)
    return;
  if (nodelay)
    tcp_nagle_disable(_pcb);
  else
    tcp_nagle_enable(_pcb);
}

bool AsyncServer::getNoDelay(){
  if (!_pcb)
    return false;
  return tcp_nagle_disabled(_pcb);
}

uint8_t AsyncServer::status(){
  if (!_pcb)
    return 0;
  return _pcb->state;
}

#if SERVER_KEEP_CLIENTS
void AsyncServer::freeDisconected(){
  AsyncClient* d = _clients;
  while(d != NULL){
    if(d->freeable()){
      AsyncClient* c = d;
      if(d->prev != NULL){
        d->prev->next = d->next;
        if(d->next)
          d->next->prev = d->prev;
      } else {
        _clients = d->next;
        if(_clients != NULL)
          _clients->prev = NULL;
      }
      d = d->next;
      c->free();
      delete c;
      os_printf("f\n");
    } else {
      if(d->state() != 4)
        os_printf("nf:%u\n", d->state());
      d = d->next;
    }
  }
}
#endif

int8_t AsyncServer::_accept(tcp_pcb* pcb, int8_t err){
#if SERVER_KEEP_CLIENTS
  AsyncClient *c = new AsyncClient(pcb);
  if(_clients == NULL)
    _clients = c;
  else {
    AsyncClient *cl = _clients;
    while(cl->next != NULL) cl = cl->next;
    c->prev = cl;
    cl->next = c;
  }
  if(_connect_cb)
    _connect_cb(_connect_cb_arg, c);
#else
  if(_connect_cb)
    _connect_cb(_connect_cb_arg, new AsyncClient(pcb));
#endif
  return ERR_OK;
}

int8_t AsyncServer::_s_accept(void *arg, tcp_pcb* pcb, int8_t err){
  return reinterpret_cast<AsyncServer*>(arg)->_accept(pcb, err);
}
