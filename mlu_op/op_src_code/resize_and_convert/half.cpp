/*************************************************************************
 * Copyright (C) [2019] by Cambricon, Inc. All rights reserved
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *************************************************************************/

#include "half.hpp"

inline bool diffNotMuch(half a, half b) {
  return true;
  float c = (float)a;
  float d = (float)b;
  if (c == 0 || d == 0) {
    return true;
  }
  c = c > 0 ? c : 0;
  d = d > 0 ? d : 0;
  if ((c / d) > DIFF_SCALE && (c / d) > DIFF_SCALE) {
    return false;
  }
  return true;
}

half::half() {}

half::~half() {}

half::half(const float a) { data_ = float2half(a); }

// Data Cast
half::operator int() {
  float a = half::half2float(data_);
  return (int)a;
}

half::operator float() {
  float a = half::half2float(data_);
  return a;
}

half::operator double() {
  float a = half::half2float(data_);
  return (double)a;
}
std::ostream& operator<<(std::ostream& output, const half& c) {
  float data_f = half::half2float(c.data_);
  output << data_f;
  return output;
}

std::istream& operator>>(std::istream& input, half& c) {
  float data_f;
  input >> data_f;
  c.data_ = half::float2half(data_f);
  return input;
}

half operator+(const int& a, const half& b) {
  float c = a;
  float b_f = half::half2float(b.data_);
  float result = c + b_f;
  result = result > HALF_MAX ? HALF_MAX : result;
  result = result < HALF_MIN ? HALF_MIN : result;
  return result;
}

half& half::operator=(const half& a) {
  data_ = a.data_;
  return *this;
}

half half::operator-(void) { return (half(0) - *this); }

half half::operator+(const half& a) {
  assert(diffNotMuch(*this, a));
  float data_f = half2float(data_);
  float a_f = half2float(a.data_);
  float result = data_f + a_f;
  result = result > HALF_MAX ? HALF_MAX : result;
  result = result < HALF_MIN ? HALF_MIN : result;
  return result;
}

half half::operator-(const half& a) {
  assert(diffNotMuch(*this, a));
  float data_f = half2float(data_);
  float a_f = half2float(a.data_);
  float result = data_f - a_f;
  result = result > HALF_MAX ? HALF_MAX : result;
  result = result < HALF_MIN ? HALF_MIN : result;
  return result;
}

half half::operator*(const half& a) {
  assert(diffNotMuch(*this, a));
  float data_f = half2float(data_);
  float a_f = half2float(a.data_);
  float result = data_f * a_f;
  result = result > HALF_MAX ? HALF_MAX : result;
  result = result < HALF_MIN ? HALF_MIN : result;
  return result;
}

half half::operator/(const half& a) {
  assert(diffNotMuch(*this, a));
  float data_f = half2float(data_);
  float a_f = half2float(a.data_);
  float result = data_f / a_f;
  result = result > HALF_MAX ? HALF_MAX : result;
  result = result < HALF_MIN ? HALF_MIN : result;
  return result;
}

half& half::operator+=(const half& a) {
  assert(diffNotMuch(*this, a));
  half result = *this + a;
  data_ = result.data_;
  return *this;
}

half& half::operator-=(const half& a) {
  assert(diffNotMuch(*this, a));
  half result = *this - a;
  data_ = result.data_;
  return *this;
}

half& half::operator*=(const half& a) {
  assert(diffNotMuch(*this, a));
  half result = *this * a;
  data_ = result.data_;
  return *this;
}

half& half::operator/=(const half& a) {
  assert(diffNotMuch(*this, a));
  half result = *this / a;
  data_ = result.data_;
  return *this;
}

bool half::operator<(const half& a) {
  float data_f = half2float(this->data_);
  float a_f = half2float(a.data_);
  return data_f < a_f ? true : false;
}

bool half::operator<=(const half& a) {
  float data_f = half2float(this->data_);
  float a_f = half2float(a.data_);
  return data_f <= a_f ? true : false;
}

bool half::operator>(const half& a) {
  float data_f = half2float(this->data_);
  float a_f = half2float(a.data_);
  return data_f > a_f ? true : false;
}

bool half::operator>=(const half& a) {
  float data_f = half2float(this->data_);
  float a_f = half2float(a.data_);
  return data_f >= a_f ? true : false;
}

bool half::operator==(const half& a) { return data_ == a.data_ ? true : false; }

bool half::operator!=(const half& a) { return data_ != a.data_ ? true : false; }

uint16_t half::float2half(const float f) {
  // assert((f > HALF_MIN) && (f < HALF_MAX));
  // assert((f == 0) || (f > HALF_PRECISION) || (f < -HALF_PRECISION));
  _bit32_u u;
  u.f = f;
  unsigned int bytes = u.i;
  unsigned char sign = (bytes >> 31) & 0x00000001;
  unsigned char exp = (bytes >> 23) & 0x000000FF;
  unsigned int eff = ((bytes >> 13) & 0x000003FF);  // + ((bytes >> 12) & 0x00000001);

  if (exp == 0xFF) {
    // inf or nan
    exp = 0x1F;
    if (eff) {
      // nan        -NaN     +NaN
      return sign ? 0xFFFF : 0x7FFF;
    } else {
      // inf        -inf     +inf
      return sign ? 0xFC00 : 0x7C00;
    }
  } else if (exp == 0x00) {
    // zero or denormal
    if (eff) {
      // denormal
      return sign ? 0x8000 : 0x0000;
    } else {
      return sign ? 0x8000 : 0x0000;
    }
  } else if (exp - 0x7F >= 0x1F - 0x0F) {
    // +/- inf
    // inf        -inf     +inf
    return sign ? 0xFC00 : 0x7C00;
  } else if (exp - 0x7F <= 0x00 - 0x0F) {
    // denormal
    int shift = (0x7F - exp - 0x0E);
    shift = shift > 11 ? 11 : shift;
    return ((sign << 15) | ((0x0400 | eff) >> shift));
  } else {
    // normal number
    exp = ((exp - 0x7F) + 0x0F) & 0x1F;
    return (sign << 15) | (exp << 10) | eff;
  }
}

float half::half2float(const uint16_t f) {
  unsigned char sign = (f >> 15) & 0x01;
  unsigned char exp = (f >> 10) & 0x1F;
  unsigned int eff = f & 0x03FF;

  unsigned int result;

  if (exp == 0x1F) {
    // handle inf of nan
    if (eff) {
      // NaN
      result = sign ? 0xFFFFFFFF : 0x7FFFFFFF;
    } else {
      // +/- inf
      result = sign ? 0xFF800000 : 0x7F800000;
    }
  } else if (exp == 0x00) {
    if (eff) {
      // denormal
      unsigned int result_base;
      result = (sign << 31) | ((0x7F - 0x0E) << 23) | (eff << (23 - 10));
      // substruct the 1.xxxxxx in eff
      result_base = (sign << 31) | ((0x7F - 0x0E) << 23) | (0x00000000 << 13);
      _bit32_u u1, u2;
      u1.i = result;
      u2.i = result_base;
      return u1.f - u2.f;
    } else {
      // zero
      result = (sign << 31) | 0x00000000;
    }
  } else {
    // normal number
    exp = (exp - 0x0F) + 0x7F;
    result = (sign << 31) | (exp << 23) | (eff << (23 - 10));
  }

  _bit32_u u;
  u.i = result;
  return u.f;
}
