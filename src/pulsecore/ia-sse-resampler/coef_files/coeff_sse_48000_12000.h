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
* Filter for conversion from 48000Hz to 12000Hz
* Coefficients per phase   = 100
* Upsample rate            = 1
* Downsample rate          = 4
* Resample frequency       = 48000
* Filter order             = 100
* Pass band frequency      = 4875
* Kaiser window beta value = 7.00
*/
float const coeff_48000_12000_float_sse[100] __attribute__ ((aligned (16)))= {
/** Phase 0 **/
0.000006521379724F, -0.000027189744447F, -0.000079252023559F, -0.000121984587165F, -0.000114947048212F, -0.000026742678495F,
0.000138132348318F, 0.000322951431958F, 0.000428146896507F, 0.000351597122343F, 0.000048051173469F, -0.000420004670837F,
-0.000872894364685F, -0.001066449965188F, -0.000799666021114F, -0.000037236992614F, 0.001014163756139F, 0.001931281541944F,
0.002219715086345F, 0.001540398432591F, -0.000074831911977F, -0.002130480205444F, -0.003777555745654F, -0.004126522419468F,
-0.002662731295334F, 0.000410991186650F, 0.004087131758992F, 0.006824854472789F, 0.007136583015756F, 0.004288267307916F,
-0.001185694875657F, -0.007430864799555F, -0.011809285101356F, -0.011908371199356F, -0.006671207100938F, 0.002826378276754F,
0.013364517273694F, 0.020494449521187F, 0.020177012535504F, 0.010603383454415F, -0.006509474920822F, -0.025758288865389F,
-0.039280732346431F, -0.039120152248330F, -0.020011640974030F, 0.018368331231200F, 0.070549636925362F, 0.126248383970973F,
0.172996247546978F, 0.199647074458545F, 0.199647074458545F, 0.172996247546978F, 0.126248383970973F, 0.070549636925362F,
0.018368331231200F, -0.020011640974030F, -0.039120152248330F, -0.039280732346431F, -0.025758288865389F, -0.006509474920822F,
0.010603383454415F, 0.020177012535504F, 0.020494449521187F, 0.013364517273694F, 0.002826378276754F, -0.006671207100938F,
-0.011908371199356F, -0.011809285101356F, -0.007430864799555F, -0.001185694875657F, 0.004288267307916F, 0.007136583015756F,
0.006824854472789F, 0.004087131758992F, 0.000410991186650F, -0.002662731295334F, -0.004126522419468F, -0.003777555745654F,
-0.002130480205444F, -0.000074831911977F, 0.001540398432591F, 0.002219715086345F, 0.001931281541944F, 0.001014163756139F,
-0.000037236992614F, -0.000799666021114F, -0.001066449965188F, -0.000872894364685F, -0.000420004670837F, 0.000048051173469F,
0.000351597122343F, 0.000428146896507F, 0.000322951431958F, 0.000138132348318F, -0.000026742678495F, -0.000114947048212F,
-0.000121984587165F, -0.000079252023559F, -0.000027189744447F, 0.000006521379724F
};
