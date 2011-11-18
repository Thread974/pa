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
* Filter for conversion from 44100Hz to 22050Hz
* Coefficients per phase   = 56
* Upsample rate            = 1
* Downsample rate          = 2
* Resample frequency       = 44100
* Filter order             = 56
* Pass band frequency      = 8600
* Kaiser window beta value = 7.00
*/
float const coeff_44100_22050_float_sse[56] __attribute__ ((aligned (16)))= {
/** Phase 0 **/
0.000052117162067F, 0.000129208296220F, -0.000045485299705F, -0.000428774282124F, -0.000330298966888F, 0.000628012664071F,
0.001275642795618F, -0.000026554355115F, -0.002355099390811F, -0.002044043696971F, 0.002193880063236F, 0.005237993661323F,
0.000953622554561F, -0.007373498609777F, -0.007615532612263F, 0.004807332259980F, 0.015307582459504F, 0.005478480610915F,
-0.017991231091044F, -0.022814684876599F, 0.007735836668084F, 0.040517498406672F, 0.022357151759315F, -0.045095376591472F,
-0.078642218658970F, 0.009703054703801F, 0.202709244414428F, 0.365676139951943F, 0.365676139951943F, 0.202709244414428F,
0.009703054703801F, -0.078642218658970F, -0.045095376591472F, 0.022357151759315F, 0.040517498406672F, 0.007735836668084F,
-0.022814684876599F, -0.017991231091044F, 0.005478480610915F, 0.015307582459504F, 0.004807332259980F, -0.007615532612263F,
-0.007373498609777F, 0.000953622554561F, 0.005237993661323F, 0.002193880063236F, -0.002044043696971F, -0.002355099390811F,
-0.000026554355115F, 0.001275642795618F, 0.000628012664071F, -0.000330298966888F, -0.000428774282124F, -0.000045485299705F,
0.000129208296220F, 0.000052117162067F
};
