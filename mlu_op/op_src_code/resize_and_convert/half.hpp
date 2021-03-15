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

#ifndef HALF_H
#define HALF_H

#include <assert.h>
#include <stdint.h>
#include <iostream>

#define HALF_MAX 65504
#define HALF_MIN -65504
#define HALF_PRECISION 0.000001
// for debug, to locate half deviation, such as
// + - * /
// you can change DIFF_SCALE value to fit your app
#define DIFF_SCALE 10000

typedef union {
  int32_t i;
  float f;
} _bit32_u;

class half {
  friend std::ostream& operator<<(std::ostream& out, const half& c);
  friend std::istream& operator>>(std::istream& in, half& c);

 public:
  half();
  ~half();
  half(const float a);
  // Data Cast
  explicit operator int();
  explicit operator float();
  explicit operator double();
  //
  friend half operator+(const int& a, const half& b);

  half& operator=(const half& a);
  half operator-(void);
  half operator+(const half& a);
  half operator-(const half& a);
  half operator*(const half& a);
  half operator/(const half& a);
  half& operator+=(const half& a);
  half& operator-=(const half& a);
  half& operator*=(const half& a);
  half& operator/=(const half& a);
  bool operator<(const half& a);
  bool operator<=(const half& a);
  bool operator>(const half& a);
  bool operator>=(const half& a);
  bool operator==(const half& a);
  bool operator!=(const half& a);
  static uint16_t float2half(const float a);
  static float half2float(const uint16_t a);
  uint16_t data_;
};

#endif  // HALF_H
