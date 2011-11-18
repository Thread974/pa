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
* Filter for conversion from 48000Hz to 32000Hz
* Coefficients per phase   = 40
* Upsample rate            = 2
* Downsample rate          = 3
* Resample frequency       = 96000
* Filter order             = 80
* Pass band frequency      = 13000
* Kaiser window beta value = 7.00
*/
float const coeff_48000_32000_float_sse[80] __attribute__ ((aligned (16)))= {
/** Phase 0 **/
0.000163598645391F, -0.000137372363644F, -0.000659745182186F, 0.000750775838191F, 0.001509059801834F, -0.002433569445115F,
-0.002431844969736F, 0.005944105826452F, 0.002616666371461F, -0.012036007614920F, -0.000531598097144F, 0.021287106884839F,
-0.006351163763126F, -0.034191608010849F, 0.022646358847126F, 0.052313259748974F, -0.061294679223746F, -0.086041196859242F,
0.213454004399359F, 0.525194646283602F, 0.404238821884463F, 0.028883471817247F, -0.108612743646714F, 0.007395537206805F,
0.053972277827920F, -0.014735699851508F, -0.028183097806651F, 0.014254590999488F, 0.013622143989823F, -0.010901512895606F,
-0.005550078085972F, 0.007006047162326F, 0.001596997824751F, -0.003773423631436F, -0.000083948905071F, 0.001636834396040F,
-0.000225355870152F, -0.000514085558105F, 0.000124721999176F, 0.000077704025655F,
/** Phase 1 **/
0.000077704025655F, 0.000124721999176F, -0.000514085558105F, -0.000225355870152F, 0.001636834396040F, -0.000083948905071F,
-0.003773423631436F, 0.001596997824751F, 0.007006047162326F, -0.005550078085972F, -0.010901512895606F, 0.013622143989823F,
0.014254590999488F, -0.028183097806651F, -0.014735699851508F, 0.053972277827920F, 0.007395537206805F, -0.108612743646714F,
0.028883471817247F, 0.404238821884463F, 0.525194646283602F, 0.213454004399359F, -0.086041196859242F, -0.061294679223746F,
0.052313259748974F, 0.022646358847126F, -0.034191608010849F, -0.006351163763126F, 0.021287106884839F, -0.000531598097144F,
-0.012036007614920F, 0.002616666371461F, 0.005944105826452F, -0.002431844969736F, -0.002433569445115F, 0.001509059801834F,
0.000750775838191F, -0.000659745182186F, -0.000137372363644F, 0.000163598645391F
};
