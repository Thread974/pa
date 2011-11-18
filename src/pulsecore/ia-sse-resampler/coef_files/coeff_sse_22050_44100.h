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
* Filter for conversion from 22050Hz to 44100Hz
* Coefficients per phase   = 28
* Upsample rate            = 2
* Downsample rate          = 1
* Resample frequency       = 44100
* Filter order             = 56
* Pass band frequency      = 8600
* Kaiser window beta value = 7.00
*/
float const coeff_22050_44100_float_sse[56] __attribute__ ((aligned (16)))= {
/** Phase 0 **/
0.000258416592440F, -0.000857548564247F, 0.001256025328141F, -0.000053108710230F, -0.004088087393943F, 0.010475987322646F,
-0.014746997219553F, 0.009614664519961F, 0.010956961221829F, -0.045629369753198F, 0.081034996813344F, -0.090190753182944F,
0.019406109407602F, 0.731352279903885F, 0.405418488828857F, -0.157284437317940F, 0.044714303518631F, 0.015471673336168F,
-0.035982462182089F, 0.030615164919007F, -0.015231065224526F, 0.001907245109123F, 0.004387760126471F, -0.004710198781622F,
0.002551285591236F, -0.000660597933775F, -0.000090970599409F, 0.000104234324135F,
/** Phase 1 **/
0.000104234324135F, -0.000090970599409F, -0.000660597933775F, 0.002551285591236F, -0.004710198781622F, 0.004387760126471F,
0.001907245109123F, -0.015231065224526F, 0.030615164919007F, -0.035982462182089F, 0.015471673336168F, 0.044714303518631F,
-0.157284437317940F, 0.405418488828857F, 0.731352279903885F, 0.019406109407602F, -0.090190753182944F, 0.081034996813344F,
-0.045629369753198F, 0.010956961221829F, 0.009614664519961F, -0.014746997219553F, 0.010475987322646F, -0.004088087393943F,
-0.000053108710230F, 0.001256025328141F, -0.000857548564247F, 0.000258416592440F
};
