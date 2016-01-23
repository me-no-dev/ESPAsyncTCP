/*
 cbuf.cpp - Circular buffer implementation
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

#include "ccbuf.h"

size_t ccbuf::available() const {
  if(_end >= _begin) {
    return _end - _begin;
  }
  return _size - (_begin - _end);
}

size_t ccbuf::free() const {
  if(_end >= _begin)
    return _size - (_end - _begin) - 1;
  return _begin - _end - 1;
}

int ccbuf::peek() {
  if(empty()) return -1;
  return static_cast<int>(*_begin);
}

int ccbuf::read(){
  if(empty()) return -1;
  char result = *_begin;
  _begin = wrap_if_bufend(_begin + 1);
  return static_cast<int>(result);
}

size_t ccbuf::read(char* dst, size_t len) {
  size_t bytes_available = available();
  size_t size_to_read = (len < bytes_available) ? len : bytes_available;
  size_t size_read = size_to_read;
  if(_end < _begin && size_to_read > (size_t)(_bufend - _begin)) {
    size_t top_size = _bufend - _begin;
    memcpy(dst, _begin, top_size);
    _begin = _buf;
    size_to_read -= top_size;
    dst += top_size;
  }
  memcpy(dst, _begin, size_to_read);
  _begin = wrap_if_bufend(_begin + size_to_read);
  return size_read;
}

size_t ccbuf::write(char c){
  if(full()) return 0;
  *_end = c;
  _end = wrap_if_bufend(_end + 1);
  return 1;
}

size_t ccbuf::write(const char* src, size_t len) {
  size_t bytes_available = free();
  size_t size_to_write = (len < bytes_available) ? len : bytes_available;
  size_t size_written = size_to_write;
  if(_end >= _begin && size_to_write > (size_t)(_bufend - _end)) {
    size_t top_size = _bufend - _end;
    memcpy(_end, src, top_size);
    _end = _buf;
    size_to_write -= top_size;
    src += top_size;
  }
  memcpy(_end, src, size_to_write);
  _end = wrap_if_bufend(_end + size_to_write);
  return size_written;
}

void ccbuf::flush() {
  _begin = _buf;
  _end = _buf;
}

