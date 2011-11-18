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
* Filter for conversion from 96000Hz to 48000Hz
* Coefficients per phase   = 36
* Upsample rate            = 1
* Downsample rate          = 2
* Resample frequency       = 96000
* Filter order             = 36
* Pass band frequency      = 19500
* Kaiser window beta value = 7.00
*/
float const coeff_96000_48000_float_sse[36] __attribute__ ((aligned (16)))= {
/** Phase 0 **/
-0.000036343697057F, 0.000266381248808F, 0.000585754785451F, -0.000462379763136F, -0.002345308808770F, -0.000913486648919F,
0.004913965135304F, 0.006226520742957F, -0.005115127445554F, -0.016464720326181F, -0.003341730734831F, 0.027888557068345F,
0.028008243598734F, -0.029253773202156F, -0.077399870760788F, -0.005845614838786F, 0.195081956913136F, 0.378206976733443F,
0.378206976733443F, 0.195081956913136F, -0.005845614838786F, -0.077399870760788F, -0.029253773202156F, 0.028008243598734F,
0.027888557068345F, -0.003341730734831F, -0.016464720326181F, -0.005115127445554F, 0.006226520742957F, 0.004913965135304F,
-0.000913486648919F, -0.002345308808770F, -0.000462379763136F, 0.000585754785451F, 0.000266381248808F, -0.000036343697057F
};
