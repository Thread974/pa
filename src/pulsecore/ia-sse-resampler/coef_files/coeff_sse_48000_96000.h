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
* Filter for conversion from 48000Hz to 96000Hz
* Coefficients per phase   = 20
* Upsample rate            = 2
* Downsample rate          = 1
* Resample frequency       = 96000
* Filter order             = 40
* Pass band frequency      = 19500
* Kaiser window beta value = 7.00
*/
float const coeff_48000_96000_float_sse[40] __attribute__ ((aligned (16)))= {
/** Phase 0 **/
-0.000541063219510F, 0.001648728039543F, -0.001798619920090F, -0.002793600069651F, 0.016333728150506F, -0.038873632139034F,
0.061183765416787F, -0.061077067602519F, -0.011845188985612F, 0.756884758530665F, 0.392022828079130F, -0.158837831636496F,
0.059783850621280F, -0.007579916934581F, -0.012670029063762F, 0.013812860741354F, -0.007966885501766F, 0.002741503966788F,
-0.000381134953701F, -0.000047053519331F,
/** Phase 1 **/
-0.000047053519331F, -0.000381134953701F, 0.002741503966788F, -0.007966885501766F, 0.013812860741354F, -0.012670029063762F,
-0.007579916934581F, 0.059783850621280F, -0.158837831636496F, 0.392022828079130F, 0.756884758530665F, -0.011845188985612F,
-0.061077067602519F, 0.061183765416787F, -0.038873632139034F, 0.016333728150506F, -0.002793600069651F, -0.001798619920090F,
0.001648728039543F, -0.000541063219510F
};
