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
* Filter for conversion from 16000Hz to 48000Hz
* Coefficients per phase   = 28
* Upsample rate            = 3
* Downsample rate          = 1
* Resample frequency       = 48000
* Filter order             = 84
* Pass band frequency      = 6500
* Kaiser window beta value = 7.00
*/
float const coeff_16000_48000_float_sse[84] __attribute__ ((aligned (16)))= {
/** Phase 0 **/
0.000294858397280F, -0.000350130565171F, -0.000486291599791F, 0.002947063019835F, -0.006878660914017F, 0.010297121013439F,
-0.009245521333045F, -0.000858899426603F, 0.022486650334443F, -0.052949012541832F, 0.082431167954399F, -0.092707433997851F,
0.043426739020836F, 0.787783297099851F, 0.320553554640773F, -0.163882911581219F, 0.079603586403438F, -0.022703486778786F,
-0.009951856548177F, 0.021819849669426F, -0.019830171430586F, 0.011966078515128F, -0.004351381205502F, -0.000159812755652F,
0.001559543553186F, -0.001224837209062F, 0.000514260558305F, -0.000093292410788F,
/** Phase 1 **/
0.000022945758305F, 0.000347048058455F, -0.001489013472135F, 0.003290060581923F, -0.004528264431566F, 0.002809612316833F,
0.004411418355408F, -0.017779368988510F, 0.033827220445809F, -0.043890088183199F, 0.034730979797193F, 0.011217154088932F,
-0.129568375111450F, 0.606588600901744F, 0.606588600901744F, -0.129568375111450F, 0.011217154088932F, 0.034730979797193F,
-0.043890088183199F, 0.033827220445809F, -0.017779368988510F, 0.004411418355408F, 0.002809612316833F, -0.004528264431566F,
0.003290060581923F, -0.001489013472135F, 0.000347048058455F, 0.000022945758305F,
/** Phase 2 **/
-0.000093292410788F, 0.000514260558305F, -0.001224837209062F, 0.001559543553186F, -0.000159812755652F, -0.004351381205502F,
0.011966078515128F, -0.019830171430586F, 0.021819849669426F, -0.009951856548177F, -0.022703486778786F, 0.079603586403438F,
-0.163882911581219F, 0.320553554640773F, 0.787783297099851F, 0.043426739020836F, -0.092707433997851F, 0.082431167954399F,
-0.052949012541832F, 0.022486650334443F, -0.000858899426603F, -0.009245521333045F, 0.010297121013439F, -0.006878660914017F,
0.002947063019835F, -0.000486291599791F, -0.000350130565171F, 0.000294858397280F
};
