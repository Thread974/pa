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

#ifndef IASRC_RESAMPLER_H
#define IASRC_RESAMPLER_H

#ifdef __cplusplus
extern "C" {
#endif

/*
    Intel context/state for the optimized resampler
*/

#define SSE_FLOAT_MAX_STATE_FRAMES          320
#define SSE_FLOAT_MAX_STATE_SAMPLES_STEREO  SSE_FLOAT_MAX_STATE_FRAMES*2
#define SSE_FLOAT_MAX_STATE_SAMPLES_MONO    SSE_FLOAT_MAX_STATE_FRAMES

typedef struct {
    int channels;
    int numcoeffs_per_phase;
    int upsampleby;
    int dwnsampleby;
    int cur_phase;
    int cur_phase_idx;
    float ** filter_state;             /* state to be kept for each channel */
    float ** filter_state_unaligned;   /* state to be kept for each channel */
    float * filter_state_scratch;      /* Buffer for doing state rotation*/
    float * filter_state_scratch_unaligned;      /* Buffer for doing state rotation*/
    float * coeff;
    float * coeff_unaligned;
    int filter_state_ptr;    /* points within the filter state; common across channels*/
    int phase_ptr;           /* Counts the number of inputs added modulo the downsample number; common across channels */
    int coeff_scrambled;

    int sse_phase;
    float *sse_state_aligned;
    float *sse_state_unaligned;
    float **sse_state_aligned_mchn;  /* This is specifically for handling multichannel cases (more than 2 channels)*/

} iaresamp_ctx;

int iaresamplib_process_sse_float(iaresamp_ctx *context, float *inp, unsigned in_n_frames, float *out, unsigned *out_n_frames);

/*
To check whether the sample rate conversion is supported
*/
int iaresamplib_supported_conversion( int ip_samplerate, int op_samplerate);

/*
 * The IA architecture optimized resampler
 */
int iaresamplib_process_float(iaresamp_ctx *ctx, float *inp, unsigned in_n_frames, float *out, unsigned *out_n_frames);

/*
 * Create a new resampler for the given input and output sampling rates
 */
int iaresamplib_new(iaresamp_ctx **ctx, int num_channels, int iprate, int oprate);

/*
 * Free the resampler
 */
int iaresamplib_delete(iaresamp_ctx **ctx);

/*
 * Reset the resampler
 */
int iaresamplib_reset(iaresamp_ctx *ctx);

#ifdef __cplusplus
}
#endif



#endif //IASRC_RESAMPLER_H
