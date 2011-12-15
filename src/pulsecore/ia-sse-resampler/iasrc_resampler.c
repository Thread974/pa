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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <xmmintrin.h>
#include <emmintrin.h>
#include <tmmintrin.h>

#include <pulse/xmalloc.h>
#include "iasrc_resampler.h"
#include "iasrc_resampler_coeffs.h"

/*
 * Index of the appropriate sample rate within the table.
 */
static int find_table_index(int* table, int tlen, int value)
{
    int i;
    int idx = -1;

    for (i = 0; i < tlen; i++)
        if(value == table[i]) {
            idx = i;
            break;
        }

    return idx;
}

/*
 * To check whether the sample rates are supported
 */
int iaresamplib_supported_conversion( int ip_samplerate, int op_samplerate)
{
    int ip_idx, op_idx;
    int resamp_supported;

    ip_idx = find_table_index((int*) &iaresamp_ip_samplerates, IA_SSE_NUM_SAMPLERATES, ip_samplerate);
    op_idx = find_table_index((int*) &iaresamp_op_samplerates, IA_SSE_NUM_SAMPLERATES, op_samplerate);

    /* Check resamp matrix to see whether the particular conversion is supported */
    if (ip_idx != -1 && op_idx != -1)
        resamp_supported = iaresamp_samplerate_matrix[ip_idx][op_idx];
    else
        resamp_supported = 0;

    return (resamp_supported);
}

/*******
 * The main process function for resampler.
 * Arguments----------
 * context       : the context of the current resampler instance.
 * inp           : Pointer to the input. All channel inputs are interleaved on a sample basis.
 * in_n_frames   : The number of input samples per channel.
 * out           : Pointer to the output. Output is also interleaved for all channels on a sample basis.
 * out_n_frames  : Number of output samples per channel.
 * Return Value---------
 * 0  : Success
 * -1 : Failure
 ********/
int iaresamplib_process_sse_float(iaresamp_ctx *context, float *inp, unsigned in_n_frames, float *out, unsigned *out_n_frames)
{
    iaresamp_ctx *ctx = (iaresamp_ctx*) context;

    register __m128 reg_res;
    register __m128 reg_res_left;
    register __m128 reg_res_right;
    register __m128 reg_coef;
    register __m128 reg_data0;
    register __m128 reg_data1;
    register __m128 reg_data2;

    int loopc;
    int num_inner_loops;
    int cphase;
    int sample_index;
    int c_per_phase;
    int urate;
    int drate;
    int remaining_samples;
    int samples_to_process;
    int state_init_size;
    int num_channels;
    int cnt_chn;
    int cnt_samp;

    float *p_inp;
    __m64 *p_out;
    float *p_out_mono;
    float *p_state_base;  /* Points to the start of the input  in the state buffer */
    float *p_state_mem;   /* Input incremented by the filter state memory, useful for loading new input*/
    float *p_state_mem_mchn;   /* Input incremented by the filter state memory, useful for loading new input*/
    float *p_coef_base;
    __m128 *p_coef;
    __m64 *p_sample;
    __m128 *p_sample_mono;
    int samples_to_process_per_chn;

    if(ctx == NULL)
        return -1;

    if (ctx->numcoeffs_per_phase==0) {
        memcpy(out, inp, in_n_frames*ctx->channels*sizeof(float));
        *out_n_frames = in_n_frames;
        return 0;
    }

    c_per_phase = ctx->numcoeffs_per_phase;
    num_inner_loops = ctx->numcoeffs_per_phase/4;
    urate = ctx->upsampleby;
    drate = ctx->dwnsampleby;
    num_channels = ctx->channels;
    cphase = ctx->sse_phase;
    state_init_size = (c_per_phase-1)*num_channels;
    *out_n_frames = 0;
    p_out = (__m64*) out;
    p_out_mono = out;
    p_inp = (float*) inp;

    p_state_mem  = ctx->sse_state_aligned + state_init_size;
    p_state_base = ctx->sse_state_aligned;
    p_coef_base  = ctx->coeff;

    reg_res       = _mm_setzero_ps();
    reg_res_left  = _mm_setzero_ps();
    reg_res_right = _mm_setzero_ps();
    reg_coef  = _mm_setzero_ps();
    reg_data0 = _mm_setzero_ps();
    reg_data1 = _mm_setzero_ps();
    reg_data2 = _mm_setzero_ps();

    // Stereo stream
    if (ctx->channels == 2) {
        remaining_samples=in_n_frames*num_channels;
        while (remaining_samples > 0) {
            samples_to_process=remaining_samples<SSE_FLOAT_MAX_STATE_SAMPLES_STEREO ? remaining_samples : SSE_FLOAT_MAX_STATE_SAMPLES_STEREO;
            remaining_samples -= samples_to_process;

            memcpy((void*) p_state_mem, (void*) p_inp, samples_to_process * sizeof(float));
            p_inp += samples_to_process;
            sample_index = 0;
            while (sample_index < samples_to_process) {
                if (cphase < urate) {
                    p_coef = (__m128*) (p_coef_base + cphase*c_per_phase);
                    p_sample = (__m64*) (p_state_base + sample_index);

                    reg_res_left = _mm_setzero_ps(); // Clear left channel result register
                    reg_res_right = _mm_setzero_ps(); // Clear left channel result register

                    for (loopc = 0; loopc < num_inner_loops; loopc++) {
                        reg_data0 = _mm_loadl_pi(reg_data0, p_sample);
                        reg_data0 = _mm_loadh_pi(reg_data0, p_sample+1);
                        reg_data1 = reg_data0;
                        reg_data2 = _mm_loadl_pi(reg_data2, p_sample+2);
                        reg_data2 = _mm_loadh_pi(reg_data2, p_sample+3);
                        p_sample += 4;
                        reg_data0 = _mm_shuffle_ps(reg_data0, reg_data2, 0x88);
                        reg_data1 = _mm_shuffle_ps(reg_data1, reg_data2, 0xDD);

                        reg_coef = _mm_load_ps((float*) p_coef);
                        p_coef += 1;

                        reg_data0 = _mm_mul_ps(reg_data0, reg_coef);
                        reg_data1 = _mm_mul_ps(reg_data1, reg_coef);

                        reg_res_left = _mm_add_ps(reg_res_left, reg_data0);
                        reg_res_right = _mm_add_ps(reg_res_right, reg_data1);
                    }

                    reg_res_left = _mm_hadd_ps(reg_res_left, reg_res_right);
                    reg_res_left = _mm_hadd_ps(reg_res_left, reg_res_left);

                    _mm_storel_pi(p_out, reg_res_left);
                    p_out += 1;
                    *out_n_frames += 1;
                    cphase += drate;
                }
                if (cphase >= urate) {
                    cphase -= urate;
                    sample_index += num_channels;
                }
            }
            memcpy((void*)(p_state_base),
                   (void*)(p_state_base+samples_to_process),
                   c_per_phase * sizeof(float)*num_channels);

        }
        ctx->sse_phase = cphase;
    }
    // Mono stream
    else if (ctx->channels == 1) {
        remaining_samples=in_n_frames;
        while (remaining_samples > 0) {
            samples_to_process=remaining_samples<SSE_FLOAT_MAX_STATE_SAMPLES_MONO ? remaining_samples : SSE_FLOAT_MAX_STATE_SAMPLES_MONO;
            remaining_samples -= samples_to_process;

            memcpy((void*) p_state_mem, (void*) p_inp, samples_to_process * sizeof(float));
            p_inp += samples_to_process;

            sample_index = 0;
            while (sample_index < samples_to_process) {
                if (cphase < urate) {
                    p_coef = (__m128*) (p_coef_base + cphase*c_per_phase);
                    p_sample_mono = (__m128*) (p_state_base + sample_index);
                    reg_res = _mm_setzero_ps(); // Clear left channel result register

                    for (loopc = 0; loopc < num_inner_loops; loopc++) {
                        reg_data0 = _mm_loadu_ps((float*) p_sample_mono);
                        p_sample_mono += 1;

                        reg_coef = _mm_load_ps((float*) p_coef);
                        p_coef += 1;

                        reg_data0 = _mm_mul_ps(reg_data0, reg_coef);

                        reg_res = _mm_add_ps(reg_res, reg_data0);
                    }

                    reg_res = _mm_hadd_ps(reg_res, reg_res);
                    reg_res = _mm_hadd_ps(reg_res, reg_res);

                    _mm_store_ss(p_out_mono, reg_res);
                    p_out_mono += 1;
                    *out_n_frames += 1;
                    cphase += drate;
                }
                if (cphase >= urate) {
                    cphase -= urate;
                    sample_index += 1;
                }
            }
            memcpy((void*)(p_state_base),
                   (void*)(p_state_base+samples_to_process),
                   c_per_phase * sizeof(float)*num_channels);

        }
        ctx->sse_phase = cphase;
    } else if (ctx->channels > 2) {  // Multichannel
        remaining_samples=in_n_frames*num_channels;
        while (remaining_samples > 0){
            samples_to_process        = remaining_samples<SSE_FLOAT_MAX_STATE_SAMPLES_MONO*num_channels ? remaining_samples : SSE_FLOAT_MAX_STATE_SAMPLES_MONO*num_channels;
            remaining_samples        -= samples_to_process;
            samples_to_process_per_chn = (int)((float)samples_to_process/num_channels + 0.5);
            for(cnt_chn = 0; cnt_chn < num_channels; cnt_chn++){
                p_state_mem_mchn = ctx->sse_state_aligned_mchn[cnt_chn] + (c_per_phase-1);
                for(cnt_samp = 0; cnt_samp < samples_to_process_per_chn; cnt_samp++){
                    p_state_mem_mchn[cnt_samp] = p_inp[cnt_chn + cnt_samp*num_channels];
                }
            }

            p_inp += samples_to_process;
            sample_index     = 0;
            while (sample_index < samples_to_process_per_chn) {

                if (cphase < urate) {
                    for(cnt_chn = 0; cnt_chn < num_channels; cnt_chn++){

                        p_coef        = (__m128*) (p_coef_base + cphase*c_per_phase);
                        p_sample_mono = (__m128*) (ctx->sse_state_aligned_mchn[cnt_chn] + sample_index);
                        reg_res       = _mm_setzero_ps(); // Clear channel result register

                        for (loopc = 0; loopc < num_inner_loops; loopc++) {
                            reg_data0 = _mm_loadu_ps((float*) p_sample_mono); /* load four data */
                            p_sample_mono += 1;

                            reg_coef  = _mm_load_ps((float*) p_coef);
                            p_coef   += 1;

                            reg_data0 = _mm_mul_ps(reg_data0, reg_coef);

                            reg_res   = _mm_add_ps(reg_res, reg_data0);
                        }

                        reg_res = _mm_hadd_ps(reg_res, reg_res);
                        reg_res = _mm_hadd_ps(reg_res, reg_res);

                        _mm_store_ss(p_out_mono, reg_res);
                        p_out_mono += 1;
                    }
                    *out_n_frames += 1;
                    cphase += drate;
                }
                if (cphase >= urate) {
                    cphase -= urate;
                    sample_index += 1;
                }
            }

            /* Memcopy the state back to the beginning */
            for(cnt_chn = 0; cnt_chn < num_channels; cnt_chn++){
                p_state_mem_mchn = ctx->sse_state_aligned_mchn[cnt_chn];
                memcpy((void*)(p_state_mem_mchn),
                       (void*)(p_state_mem_mchn+samples_to_process_per_chn),
                       c_per_phase * sizeof(float));
            }
        }
        ctx->sse_phase = cphase;
    }

    return 0;
}

/*
 * Create a new resampler for the given input and output sampling rates
 * Arguments :
 *        context = unallocated context
 *        num_channels = number of channels, 1 for mono, 2 for stereo etc...
 *        iprate       = input sample rate  in bps, example 44100, 48000 etc..
 *        oprate       = output sample rate in bps
 * return value :
 *        0 if succesful
 *        -1 if not
 */
int iaresamplib_new(iaresamp_ctx **context, int num_channels, int iprate, int oprate)
{
    int i;
    iaresamp_ctx *ctx;
    int ip_idx, op_idx;
    resample_config_t *confdata;
    resample_config_t *rconf;

#ifdef UNIT_TEST
    ctx = (iaresamp_ctx *)malloc(sizeof(iaresamp_ctx));
#else
    ctx = (iaresamp_ctx *)pa_xmalloc(sizeof(iaresamp_ctx));
#endif
    if(ctx == NULL)
        return -1;
    else
        *context = ctx;

    ctx->channels = num_channels;
    ctx->coeff_scrambled = 0;
    ctx->phase_ptr = 0;
    ctx->filter_state_ptr = 0;
    ctx->coeff_unaligned  = NULL;
    ctx->filter_state     = NULL;
    ctx->filter_state_unaligned = NULL;
    ctx->filter_state_scratch_unaligned = NULL;

    ip_idx = find_table_index((int*) &iaresamp_ip_samplerates, IA_SSE_NUM_SAMPLERATES, iprate);
    op_idx = find_table_index((int*) &iaresamp_op_samplerates, IA_SSE_NUM_SAMPLERATES, oprate);

    confdata = (resample_config_t*) NULL;
    rconf = config_table[ip_idx].config_data;
    for (i = 0; i < config_table[ip_idx].count; i++) {
        if (rconf[i].output_samplerate == op_idx) {
            confdata = &rconf[i];
            break;
        }
    }

    if (confdata == NULL) {
        goto IARESAMPLIB_NEW_ERROR;
    }

    ctx->numcoeffs_per_phase    = confdata->numcoeffs_per_phase;
    ctx->upsampleby             = confdata->upsampleby;
    ctx->dwnsampleby            = confdata->dwnsampleby;
    ctx->coeff_scrambled    = confdata->coeff_scrambled;
    ctx->cur_phase              = 0;
    ctx->cur_phase_idx          = 0;
#ifdef UNIT_TEST
    ctx->coeff_unaligned        = (float *)malloc(ctx->numcoeffs_per_phase*ctx->upsampleby*sizeof(float) + 16);
#else
    ctx->coeff_unaligned        = (float *)pa_xmalloc(ctx->numcoeffs_per_phase*ctx->upsampleby*sizeof(float) + 16);
#endif
    if(ctx->coeff_unaligned == NULL){
        goto IARESAMPLIB_NEW_ERROR;
    }

    ctx->coeff                = (float *)((long)((unsigned char *)ctx->coeff_unaligned + 16) & (~0xf) );
    for(i = 0; i < ctx->numcoeffs_per_phase*ctx->upsampleby; i++ ){
        ctx->coeff[i] = confdata->coef_ptr[i];
    }


    /* Allocate aligned memory */
#ifdef UNIT_TEST
    ctx->filter_state             = (float **)malloc(num_channels*sizeof(float*));
#else
    ctx->filter_state             = (float **)pa_xmalloc(num_channels*sizeof(float*));
#endif
    if(ctx->filter_state == NULL)
        goto IARESAMPLIB_NEW_ERROR;
#ifdef UNIT_TEST
    ctx->filter_state_unaligned   = (float **)malloc(num_channels*sizeof(float*));
#else
    ctx->filter_state_unaligned   = (float **)pa_xmalloc(num_channels*sizeof(float*));
#endif
    if(ctx->filter_state_unaligned == NULL)
        goto IARESAMPLIB_NEW_ERROR;
    for(i = 0; i< num_channels; i++){
        ctx->filter_state_unaligned[i]    = (float *)NULL;
    }

    for(i = 0; i< num_channels; i++){
#ifdef UNIT_TEST
        ctx->filter_state_unaligned[i]    = (float *)calloc((ctx->numcoeffs_per_phase + 16), sizeof(float));
#else
        ctx->filter_state_unaligned[i]    = (float *)pa_xmalloc0((ctx->numcoeffs_per_phase + 16)*sizeof(float));
#endif
        if(ctx->filter_state_unaligned[i] == NULL)
            goto IARESAMPLIB_NEW_ERROR;
        ctx->filter_state[i]              = (float *)((long)((unsigned char *)(ctx->filter_state_unaligned[i]) + 16) & (~0xf));
    }

    /* Allocate scratch memory for buffer swap */
#ifdef UNIT_TEST
    ctx->filter_state_scratch_unaligned = (float *)calloc((ctx->numcoeffs_per_phase + 16), sizeof(float));
#else
    ctx->filter_state_scratch_unaligned = (float *)pa_xmalloc0((ctx->numcoeffs_per_phase + 16)*sizeof(float));
#endif
    if(ctx->filter_state_scratch_unaligned == NULL)
        goto IARESAMPLIB_NEW_ERROR;
    ctx->filter_state_scratch           = (float *)((long)((unsigned char *)(ctx->filter_state_scratch_unaligned) + 16) & (~0xf));

    /* Allocate state memory for sse version */
    /* Additional memory is allocated, as in the multichannel version, each channel requires its own aligned memory */
#ifdef UNIT_TEST
    ctx->sse_state_unaligned    = (float*) calloc(((ctx->numcoeffs_per_phase+SSE_FLOAT_MAX_STATE_SAMPLES_MONO) + 16)*num_channels, sizeof(float));
#else
    ctx->sse_state_unaligned    = (float*) pa_xmalloc0(((ctx->numcoeffs_per_phase+SSE_FLOAT_MAX_STATE_SAMPLES_MONO) + 16)*num_channels*sizeof(float));
#endif
    ctx->sse_state_aligned      = (float*) ((long) ((unsigned char*) (ctx->sse_state_unaligned)+16) & (~0xf));
    /* state for multichannel case, where the num_channel is greater than 2 */
#ifdef UNIT_TEST
    ctx->sse_state_aligned_mchn = (float**)malloc(num_channels*sizeof(float*));
#else
    ctx->sse_state_aligned_mchn = (float**)pa_xmalloc(num_channels*sizeof(float*));
#endif
    for(i=0; i< num_channels; i++){
        /* Aligning such that each channel gets an aligned pointer for its state */
        ctx->sse_state_aligned_mchn[i] = (float*) ((long) ((unsigned char*) (ctx->sse_state_unaligned + (ctx->numcoeffs_per_phase+ (SSE_FLOAT_MAX_STATE_SAMPLES_MONO + 16) )*i )+16) & (~0xf));
    }
    ctx->sse_phase = 0;

    return 0;

 IARESAMPLIB_NEW_ERROR:
    iaresamplib_delete(context);
    return -1;
}



/*
 * free the resampler
 */
int iaresamplib_delete(iaresamp_ctx **context){
    int num_channels;
    int i;
    iaresamp_ctx *ctx;

    ctx = (iaresamp_ctx *)(*context);
    if(ctx == NULL)
        return -1;

    if(ctx != NULL){
        num_channels = ctx->channels;
        if(ctx->filter_state_unaligned != NULL){
            for(i=0; i<num_channels; i++){
                if(ctx->filter_state_unaligned[i] != NULL){
#ifdef UNIT_TEST
                    free(ctx->filter_state_unaligned[i]);
#else
                    pa_xfree(ctx->filter_state_unaligned[i]);
#endif
                    ctx->filter_state_unaligned[i] = NULL;
                }
            }
#ifdef UNIT_TEST
            free(ctx->filter_state_unaligned);
#else
            pa_xfree(ctx->filter_state_unaligned);
#endif
            ctx->filter_state_unaligned = NULL;
        }
        if(ctx->filter_state_scratch_unaligned != NULL){
#ifdef UNIT_TEST
            free(ctx->filter_state_scratch_unaligned);
#else
            pa_xfree(ctx->filter_state_scratch_unaligned);
#endif
            ctx->filter_state_scratch_unaligned = NULL;
        }
        if(ctx->coeff_unaligned != NULL){
#ifdef UNIT_TEST
            free(ctx->coeff_unaligned);
#else
            pa_xfree(ctx->coeff_unaligned);
#endif
            ctx->coeff_unaligned = NULL;
        }
        if(ctx->filter_state != NULL){
#ifdef UNIT_TEST
            free(ctx->filter_state);
#else
            pa_xfree(ctx->filter_state);
#endif
            ctx->filter_state = NULL;
        }

        if(ctx->sse_state_unaligned != NULL){
#ifdef UNIT_TEST
            free(ctx->sse_state_unaligned);
#else
            pa_xfree(ctx->sse_state_unaligned);
#endif
            ctx->sse_state_unaligned = NULL;
        }
        if(ctx->sse_state_aligned_mchn != NULL){
#ifdef UNIT_TEST
            free(ctx->sse_state_aligned_mchn);
#else
            pa_xfree(ctx->sse_state_aligned_mchn);
#endif
            ctx->sse_state_aligned_mchn = NULL;
        }

#ifdef UNIT_TEST
        free(ctx);
#else
        pa_xfree(ctx);
#endif
        *context = NULL;
    }

    return 0;
}


/*
 * Reset the resampler
 */
int iaresamplib_reset(iaresamp_ctx *context)
{
    int i, j;
    int num_channels;
    int numcoeffs_per_phase;
    iaresamp_ctx *ctx;

    ctx = (iaresamp_ctx *)(context);
    if(ctx == NULL)
        return -1;

    num_channels        = ctx->channels;
    numcoeffs_per_phase = ctx->numcoeffs_per_phase;

    for(i=0; i<num_channels; i++)
        for(j=0; j < numcoeffs_per_phase; j++)
            ctx->filter_state[i][j] = 0;

    ctx->cur_phase     = 0;
    ctx->cur_phase_idx = 0;

    return 0;
}
