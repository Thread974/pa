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
* Filter for conversion from 24000Hz to 48000Hz
* Coefficients per phase   = 28
* Upsample rate            = 2
* Downsample rate          = 1
* Resample frequency       = 48000
* Filter order             = 56
* Pass band frequency      = 9750
* Kaiser window beta value = 7.00
*/
float const coeff_24000_48000_float_sse[56] __attribute__ ((aligned (16)))= {
/** Phase 0 **/
0.000199566192984F, -0.000127765093843F, -0.000828413298108F, 0.003197228654467F, -0.006519575304393F, 0.008590539581531F,
-0.005624408726567F, -0.006117652110815F, 0.027542469679735F, -0.053978905681083F, 0.073701013666351F, -0.066647744523962F,
-0.012163517155788F, 0.757607949246480F, 0.395743185006616F, -0.167377335424847F, 0.068198598048152F, -0.009745960096959F,
-0.019247293146483F, 0.026286749550649F, -0.020549280761915F, 0.010788174117900F, -0.002834752136149F, -0.001210239761634F,
0.002019596901802F, -0.001314970554662F, 0.000483330644852F, -0.000070587514310F,
/** Phase 1 **/
-0.000070587514310F, 0.000483330644852F, -0.001314970554662F, 0.002019596901802F, -0.001210239761634F, -0.002834752136149F,
0.010788174117900F, -0.020549280761915F, 0.026286749550649F, -0.019247293146483F, -0.009745960096959F, 0.068198598048152F,
-0.167377335424847F, 0.395743185006616F, 0.757607949246480F, -0.012163517155788F, -0.066647744523962F, 0.073701013666351F,
-0.053978905681083F, 0.027542469679735F, -0.006117652110815F, -0.005624408726567F, 0.008590539581531F, -0.006519575304393F,
0.003197228654467F, -0.000828413298108F, -0.000127765093843F, 0.000199566192984F
};
