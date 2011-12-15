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

#ifndef IASRC_COEFFS_H
#define IASRC_COEFFS_H

enum {
    rate8000 = 0,
    rate11025,
    rate12000,
    rate16000,
    rate22050,
    rate24000,
    rate32000,
    rate44100,
    rate48000,
    rate96000,
    IA_SSE_NUM_SAMPLERATES
} ;

/*
  Supported input sample rates
*/
static int iaresamp_ip_samplerates[IA_SSE_NUM_SAMPLERATES] = {
    8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000, 96000
};

/*
  Supported output sample rates
*/
static int iaresamp_op_samplerates[IA_SSE_NUM_SAMPLERATES] = {
    8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000, 96000
};

/*
 * Matrix of supported sample rates.
 * Dont support copy for now.
 */
static int iaresamp_samplerate_matrix[IA_SSE_NUM_SAMPLERATES][IA_SSE_NUM_SAMPLERATES] = {
    /*           8000  11025  12000 16000 22050 24000 32000 44100 48000 96000*/
    /* 8000  */ {  1,  0,     0,    0,    0,    0,    0,    1,    1,    0},
    /* 11025 */ {  0,  1,     0,    0,    0,    0,    0,    1,    1,    0},
    /* 12000 */ {  0,  0,     1,    0,    0,    0,    0,    1,    1,    0},
    /* 16000 */ {  0,  0,     0,    1,    0,    0,    0,    1,    1,    0},
    /* 22050 */ {  0,  0,     0,    0,    1,    0,    0,    1,    1,    0},
    /* 24000 */ {  0,  0,     0,    0,    0,    1,    0,    1,    1,    0},
    /* 32000 */ {  0,  0,     0,    0,    0,    0,    1,    1,    1,    0},
    /* 44100 */ {  1,  1,     1,    1,    1,    1,    1,    1,    1,    1},
    /* 48000 */ {  1,  1,     1,    1,    1,    1,    1,    1,    1,    1},
    /* 96000 */ {  0,  0,     0,    0,    0,    0,    0,    1,    1,    1}
};


typedef struct {
    int output_samplerate;
    int numcoeffs_per_phase;
    int upsampleby;
    int dwnsampleby;
    int coeff_scrambled;
    float *coef_ptr;
} resample_config_t;

#include "coef_files/coeff_sse_8000_44100.h"
#include "coef_files/coeff_sse_8000_48000.h"
#include "coef_files/coeff_sse_11025_44100.h"
#include "coef_files/coeff_sse_11025_48000.h"
#include "coef_files/coeff_sse_12000_44100.h"
#include "coef_files/coeff_sse_12000_48000.h"
//#include "coef_files/coeff_sse_16000_44100.h"
#define coeff_16000_44100_float_sse coeff_8000_44100_float_sse
#include "coef_files/coeff_sse_16000_48000.h"
#include "coef_files/coeff_sse_22050_44100.h"
#include "coef_files/coeff_sse_22050_48000.h"
//#include "coef_files/coef_24000_44100.h"
#define coeff_24000_44100_float_sse coeff_12000_44100_float_sse
#include "coef_files/coeff_sse_24000_48000.h"
//#include "coef_files/coef_32000_44100.h"
#define coeff_32000_44100_float_sse coeff_8000_44100_float_sse
//#include "coef_files/coef_32000_48000.h"
#define coeff_32000_48000_float_sse coeff_16000_48000_float_sse
#include "coef_files/coeff_sse_44100_8000.h"
#include "coef_files/coeff_sse_44100_11025.h"
#include "coef_files/coeff_sse_44100_12000.h"
#include "coef_files/coeff_sse_44100_16000.h"
#include "coef_files/coeff_sse_44100_22050.h"
#include "coef_files/coeff_sse_44100_24000.h"
#include "coef_files/coeff_sse_44100_32000.h"
#include "coef_files/coeff_sse_44100_48000.h"
#include "coef_files/coeff_sse_44100_96000.h"
#include "coef_files/coeff_sse_48000_8000.h"
#include "coef_files/coeff_sse_48000_11025.h"
#include "coef_files/coeff_sse_48000_12000.h"
#include "coef_files/coeff_sse_48000_16000.h"
#include "coef_files/coeff_sse_48000_22050.h"
#include "coef_files/coeff_sse_48000_24000.h"
#include "coef_files/coeff_sse_48000_32000.h"
#include "coef_files/coeff_sse_48000_44100.h"
#include "coef_files/coeff_sse_48000_96000.h"
#include "coef_files/coeff_sse_96000_44100.h"
#include "coef_files/coeff_sse_96000_48000.h"


#define RATE8000_NBR_OUTPUT_RATES 3
static const resample_config_t rate8000_input_config[RATE8000_NBR_OUTPUT_RATES] = {
    {rate8000, 0, 1, 1, 0, NULL},
    {rate44100, 28, 441, 80, 0, (float*) &coeff_8000_44100_float_sse},
    {rate48000, 32, 6, 1, 0, (float*) &coeff_8000_48000_float_sse}
};

#define RATE11025_NBR_OUTPUT_RATES 3
static const resample_config_t rate11025_input_config[RATE11025_NBR_OUTPUT_RATES] = {
    {rate11025, 0, 1, 1, 0, NULL},
    {rate44100, 28, 4, 1, 0, (float*) &coeff_11025_44100_float_sse},
    {rate48000, 24, 640, 147, 0, (float*) &coeff_11025_48000_float_sse}
};

#define RATE12000_NBR_OUTPUT_RATES 3
static const resample_config_t rate12000_input_config[RATE12000_NBR_OUTPUT_RATES] = {
    {rate12000, 0, 1, 1, 0, NULL},
    {rate44100, 28, 147, 40, 0, (float*) &coeff_12000_44100_float_sse},
    {rate48000, 28, 4, 1, 0, (float*) &coeff_12000_48000_float_sse}
};

#define RATE16000_NBR_OUTPUT_RATES 3
static const resample_config_t rate16000_input_config[RATE16000_NBR_OUTPUT_RATES] = {
    {rate16000, 0, 1, 1, 0, NULL},
    {rate44100, 28, 441, 160, 0, (float*) &coeff_16000_44100_float_sse},
    {rate48000, 28, 3, 1, 0, (float*) &coeff_16000_48000_float_sse}
};

#define RATE22050_NBR_OUTPUT_RATES 3
static const resample_config_t rate22050_input_config[RATE22050_NBR_OUTPUT_RATES] = {
    {rate22050, 0, 1, 1, 0, NULL},
    {rate44100, 28, 2, 1, 0, (float*) &coeff_22050_44100_float_sse},
    {rate48000, 28, 320, 147, 0, (float*) &coeff_22050_48000_float_sse}
};

#define RATE24000_NBR_OUTPUT_RATES 3
static const resample_config_t rate24000_input_config[RATE24000_NBR_OUTPUT_RATES] = {
    {rate24000, 0, 1, 1, 0, NULL},
    {rate44100, 28, 147, 80, 0, (float*) &coeff_24000_44100_float_sse},
    {rate48000, 28, 2, 1, 0, (float*) &coeff_24000_48000_float_sse}
};

#define RATE32000_NBR_OUTPUT_RATES 3
static const resample_config_t rate32000_input_config[RATE32000_NBR_OUTPUT_RATES] = {
    {rate32000, 0, 1, 1, 0, NULL},
    {rate44100, 28, 441, 320, 0, (float*) &coeff_32000_44100_float_sse},
    {rate48000, 28, 3, 2, 0, (float*) &coeff_32000_48000_float_sse}
};

#define RATE44100_NBR_OUTPUT_RATES 10
static const resample_config_t rate44100_input_config[RATE44100_NBR_OUTPUT_RATES] = {
    {rate8000, 152, 80, 441, 0, (float*) &coeff_44100_8000_float_sse},
    {rate11025, 112, 1, 4, 0, (float*) &coeff_44100_11025_float_sse},
    {rate12000, 100, 40, 147, 0, (float*) &coeff_44100_12000_float_sse},
    {rate16000, 76, 160, 441, 0, (float*) &coeff_44100_16000_float_sse},
    {rate22050, 56, 1, 2, 0, (float*) &coeff_44100_22050_float_sse},
    {rate24000, 52, 80, 147, 0, (float*) &coeff_44100_24000_float_sse},
    {rate32000, 40, 320, 441, 0, (float*) &coeff_44100_32000_float_sse},
    {rate44100, 0, 1, 1, 0, NULL},
    {rate48000, 16, 160, 147, 0, (float*) &coeff_44100_48000_float_sse},
    {rate96000, 20, 320, 147, 0, (float*) &coeff_44100_96000_float_sse}
};

#define RATE48000_NBR_OUTPUT_RATES 10
static const resample_config_t rate48000_input_config[RATE48000_NBR_OUTPUT_RATES] = {
    {rate8000, 152, 1, 6, 0, (float*) &coeff_48000_8000_float_sse},
    {rate11025, 112, 147, 640, 0, (float*) &coeff_48000_11025_float_sse},
    {rate12000, 100, 1, 4, 0, (float*) &coeff_48000_12000_float_sse},
    {rate16000, 76, 1, 3, 0, (float*) &coeff_48000_16000_float_sse},
    {rate22050, 56, 147, 320, 0, (float*) &coeff_48000_22050_float_sse},
    {rate24000, 52, 1, 2, 0, (float*) &coeff_48000_24000_float_sse},
    {rate32000, 40, 2, 3, 0, (float*) &coeff_48000_32000_float_sse},
    {rate44100, 16, 147, 160, 0, (float*) &coeff_48000_44100_float_sse},
    {rate48000, 0, 1, 1, 0, NULL},
    {rate96000, 20, 2, 1, 0, (float*) &coeff_48000_96000_float_sse}
};

#define RATE96000_NBR_OUTPUT_RATES 3
static const resample_config_t rate96000_input_config[RATE96000_NBR_OUTPUT_RATES] = {
    {rate44100, 28, 147, 320, 0, (float*) &coeff_96000_44100_float_sse},
    {rate48000, 36, 1, 2, 0, (float*) &coeff_96000_48000_float_sse},
    {rate96000, 0, 1, 1, 0, NULL}
};

typedef struct {
    resample_config_t *config_data;
    int count;
} config_table_t;

static const config_table_t config_table[IA_SSE_NUM_SAMPLERATES] = {
    {(resample_config_t*) &rate8000_input_config, RATE8000_NBR_OUTPUT_RATES},
    {(resample_config_t*) &rate11025_input_config, RATE11025_NBR_OUTPUT_RATES},
    {(resample_config_t*) &rate12000_input_config, RATE12000_NBR_OUTPUT_RATES},
    {(resample_config_t*) &rate16000_input_config, RATE16000_NBR_OUTPUT_RATES},
    {(resample_config_t*) &rate22050_input_config, RATE22050_NBR_OUTPUT_RATES},
    {(resample_config_t*) &rate24000_input_config, RATE24000_NBR_OUTPUT_RATES},
    {(resample_config_t*) &rate32000_input_config, RATE32000_NBR_OUTPUT_RATES},
    {(resample_config_t*) &rate44100_input_config, RATE44100_NBR_OUTPUT_RATES},
    {(resample_config_t*) &rate48000_input_config, RATE48000_NBR_OUTPUT_RATES},
    {(resample_config_t*) &rate96000_input_config, RATE96000_NBR_OUTPUT_RATES}
};



#endif
