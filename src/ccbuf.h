/* 
 cbuf.h - Circular buffer implementation
 Copyright (c) 2014 Ivan Grokhotkov. All rights reserved.
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

#ifndef __ccbuf_h
#define __ccbuf_h

#include <stddef.h>
#include <stdint.h>
#include <string.h>

class ccbuf {
  public:
    ccbuf *next;

    ccbuf(size_t len):next(NULL), _size(len), _buf(new char[len]), _bufend(_buf + len), _begin(_buf), _end(_begin){}
    ~ccbuf() {
      delete[] _buf;
    }

    inline bool empty() const {
      return _begin == _end;
    }

    inline bool full() const {
      return wrap_if_bufend(_end + 1) == _begin;
    }

    size_t available() const ;
    size_t free() const ;
    int peek();
    int read();
    size_t read(char* dst, size_t len);
    size_t write(char c);
    size_t write(const char* src, size_t len);
    void flush();

  private:
    inline char* wrap_if_bufend(char* ptr) const {
      return (ptr == _bufend) ? _buf : ptr;
    }

    const size_t _size;
    char* _buf;
    const char* const _bufend;
    char* _begin;
    char* _end;
};

#endif//__cbuf_h
