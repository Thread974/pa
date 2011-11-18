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
* Filter for conversion from 48000Hz to 24000Hz
* Coefficients per phase   = 52
* Upsample rate            = 1
* Downsample rate          = 2
* Resample frequency       = 48000
* Filter order             = 52
* Pass band frequency      = 9750
* Kaiser window beta value = 7.00
*/
float const coeff_48000_24000_float_sse[52] __attribute__ ((aligned (16)))= {
/** Phase 0 **/
0.000066934819885F, -0.000024746231090F, -0.000309548558183F, -0.000222887868351F, 0.000600444364583F, 0.001028435034571F,
-0.000415155376511F, -0.002360217885043F, -0.001074452508674F, 0.003387718278030F, 0.004403880982903F, -0.002366580961318F,
-0.008880342178344F, -0.002706750052431F, 0.011875044696096F, 0.012672702796569F, -0.008999932866434F, -0.025598484755232F,
-0.004678633223600F, 0.035752731448006F, 0.033375903248330F, -0.032853362961989F, -0.082979340781260F, -0.006055839747599F,
0.197582489051885F, 0.378779991235200F, 0.378779991235200F, 0.197582489051885F, -0.006055839747599F, -0.082979340781260F,
-0.032853362961989F, 0.033375903248330F, 0.035752731448006F, -0.004678633223600F, -0.025598484755232F, -0.008999932866434F,
0.012672702796569F, 0.011875044696096F, -0.002706750052431F, -0.008880342178344F, -0.002366580961318F, 0.004403880982903F,
0.003387718278030F, -0.001074452508674F, -0.002360217885043F, -0.000415155376511F, 0.001028435034571F, 0.000600444364583F,
-0.000222887868351F, -0.000309548558183F, -0.000024746231090F, 0.000066934819885F
};
