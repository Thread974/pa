/*
    This file is provided under a dual LGPL/BSD license.  When using
    or redistributing this file, you may do so under either license.

    LGPL LICENSE SUMMARY

    Copyright(c) 2011. Intel Corporation. All rights reserved.

    This library is free software; you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as
    published by the Free Software Foundation; either version 2.1 of the
    License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful, but
    WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
    USA.

    BSD LICENSE

    Copyright (c) 2011. Intel Corporation. All rights reserved.

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions
    are met:

      - Redistributions of source code must retain the above copyright
        notice, this list of conditions and the following disclaimer.
      - Redistributions in binary form must reproduce the above copyright
        notice, this list of conditions and the following disclaimer in
        the documentation and/or other materials provided with the
        distribution.
      - Neither the name of Intel Corporation nor the names of its
        contributors may be used to endorse or promote products derived
        from this software without specific prior written permission.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
    "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
    LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
    A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
    OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
    SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
    LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
    DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
    THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
    (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
    OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

    Contact Information for Intel:
        Intel Corporation
        2200 Mission College Blvd.
        Santa Clara, CA  97052
*/

/*
* Filter for conversion from 48000Hz to 16000Hz
* Coefficients per phase   = 76
* Upsample rate            = 1
* Downsample rate          = 3
* Resample frequency       = 48000
* Filter order             = 76
* Pass band frequency      = 6500
* Kaiser window beta value = 7.00
*/
float const coeff_48000_16000_float_sse[76] __attribute__ ((aligned (16)))= {
/** Phase 0 **/
0.000023735615284F, -0.000032003020039F, -0.000136302362799F, -0.000191875324188F, -0.000070350467367F, 0.000248055100380F,
0.000566799290178F, 0.000543756423795F, -0.000031305107259F, -0.000935151544573F, -0.001489045378442F, -0.000982658061713F,
0.000659210044745F, 0.002501328448947F, 0.003000223862556F, 0.001138551654660F, -0.002450371692208F, -0.005385323841186F,
-0.004937794588483F, -0.000243506548682F, 0.006304557115790F, 0.009945800561308F, 0.006718163097068F, -0.003017193994013F,
-0.013486331874172F, -0.016470158243069F, -0.007140928330879F, 0.011034002964175F, 0.026424835920426F, 0.025723235817989F,
0.003650306113323F, -0.030353475184860F, -0.053935988596620F, -0.042825994050395F, 0.014402848427505F, 0.106585757835823F,
0.202036067229430F, 0.262608522687565F, 0.262608522687565F, 0.202036067229430F, 0.106585757835823F, 0.014402848427505F,
-0.042825994050395F, -0.053935988596620F, -0.030353475184860F, 0.003650306113323F, 0.025723235817989F, 0.026424835920426F,
0.011034002964175F, -0.007140928330879F, -0.016470158243069F, -0.013486331874172F, -0.003017193994013F, 0.006718163097068F,
0.009945800561308F, 0.006304557115790F, -0.000243506548682F, -0.004937794588483F, -0.005385323841186F, -0.002450371692208F,
0.001138551654660F, 0.003000223862556F, 0.002501328448947F, 0.000659210044745F, -0.000982658061713F, -0.001489045378442F,
-0.000935151544573F, -0.000031305107259F, 0.000543756423795F, 0.000566799290178F, 0.000248055100380F, -0.000070350467367F,
-0.000191875324188F, -0.000136302362799F, -0.000032003020039F, 0.000023735615284F
};
