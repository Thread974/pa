/***
  This file is part of PulseAudio.

  Copyright 2008-2009 Joao Paulo Rechi Vita

  PulseAudio is free software; you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as
  published by the Free Software Foundation; either version 2.1 of the
  License, or (at your option) any later version.

  PulseAudio is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with PulseAudio; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
  USA.
***/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <errno.h>
#include <linux/sockios.h>
#include <arpa/inet.h>

#include <pulse/rtclock.h>
#include <pulse/sample.h>
#include <pulse/timeval.h>
#include <pulse/xmalloc.h>

#include <pulsecore/i18n.h>
#include <pulsecore/module.h>
#include <pulsecore/modargs.h>
#include <pulsecore/core-rtclock.h>
#include <pulsecore/core-util.h>
#include <pulsecore/core-error.h>
#include <pulsecore/shared.h>
#include <pulsecore/socket-util.h>
#include <pulsecore/thread.h>
#include <pulsecore/thread-mq.h>
#include <pulsecore/poll.h>
#include <pulsecore/rtpoll.h>
#include <pulsecore/time-smoother.h>
#include <pulsecore/namereg.h>
#include <pulsecore/dbus-shared.h>

#include "module-bluetooth-device-symdef.h"
#include "ipc.h"
#include "sbc.h"
#include "a2dp-codecs.h"
#include "rtp.h"
#include "bluetooth-util.h"

#define BITPOOL_DEC_LIMIT 32
#define BITPOOL_DEC_STEP 5
#define HSP_MAX_GAIN 15

PA_MODULE_AUTHOR("Joao Paulo Rechi Vita");
PA_MODULE_DESCRIPTION("Bluetooth audio sink and source");
PA_MODULE_VERSION(PACKAGE_VERSION);
PA_MODULE_LOAD_ONCE(FALSE);
PA_MODULE_USAGE(
        "name=<name for the card/sink/source, to be prefixed> "
        "card_name=<name for the card> "
        "card_properties=<properties for the card> "
        "sink_name=<name for the sink> "
        "sink_properties=<properties for the sink> "
        "source_name=<name for the source> "
        "source_properties=<properties for the source> "
        "address=<address of the device> "
        "profile=<a2dp|hsp|hfgw> "
        "rate=<sample rate> "
        "channels=<number of channels> "
        "path=<device object path> "
        "auto_connect=<automatically connect?> "
        "sco_sink=<SCO over PCM sink name> "
        "sco_source=<SCO over PCM source name>");

/* TODO: not close fd when entering suspend mode in a2dp */

static const char* const valid_modargs[] = {
    "name",
    "card_name",
    "card_properties",
    "sink_name",
    "sink_properties",
    "source_name",
    "source_properties",
    "address",
    "profile",
    "rate",
    "channels",
    "path",
    "auto_connect",
    "sco_sink",
    "sco_source",
    NULL
};

#define A2DP_SOURCE_ENDPOINT "/MediaEndpoint/A2DPSource"
#define A2DP_SOURCE_ENDPOINT_MPEG "/MediaEndpoint/A2DPSourceMpeg"

typedef enum {
    A2DP_MODE_SBC,
    A2DP_MODE_MPEG,
} a2dp_mode_t;

struct a2dp_info {
    sbc_capabilities_t sbc_capabilities;
    mpeg_capabilities_t mpeg_capabilities;

    a2dp_mode_t mode;
    pa_bool_t has_mpeg;

    sbc_t sbc;                           /* Codec data */
    pa_bool_t sbc_initialized;           /* Keep track if the encoder is initialized */
    size_t codesize, frame_length;       /* SBC Codesize, frame_length. We simply cache those values here */

    void* buffer;                        /* Codec transfer buffer */
    size_t buffer_size;                  /* Size of the buffer */

    uint16_t seq_num;                    /* Cumulative packet sequence */
    uint8_t min_bitpool;
    uint8_t max_bitpool;
};

struct hsp_info {
    pcm_capabilities_t pcm_capabilities;
    pa_sink *sco_sink;
    void (*sco_sink_set_volume)(pa_sink *s);
    pa_source *sco_source;
    void (*sco_source_set_volume)(pa_source *s);
    pa_hook_slot *sink_state_changed_slot;
    pa_hook_slot *source_state_changed_slot;
};

struct bluetooth_msg {
    pa_msgobject parent;
    pa_card *card;
};

typedef struct bluetooth_msg bluetooth_msg;
PA_DEFINE_PRIVATE_CLASS(bluetooth_msg, pa_msgobject);
#define BLUETOOTH_MSG(o) (bluetooth_msg_cast(o))

struct userdata {
    pa_core *core;
    pa_module *module;

    char *address;
    char *path;
    char *transport;
    char *accesstype;

    pa_bluetooth_discovery *discovery;
    pa_bool_t auto_connect;

    pa_dbus_connection *connection;

    pa_card *card;
    pa_sink *sink;
    pa_source *source;

    pa_thread_mq thread_mq;
    pa_rtpoll *rtpoll;
    pa_rtpoll_item *rtpoll_item;
    pa_thread *thread;
    bluetooth_msg *msg;

    uint64_t read_index, write_index;
    pa_usec_t started_at;
    pa_smoother *read_smoother;

    pa_memchunk write_memchunk;

    pa_sample_spec sample_spec, requested_sample_spec;

    int service_fd;
    int stream_fd;

    size_t link_mtu;
    size_t block_size;

    struct a2dp_info a2dp;
    struct hsp_info hsp;

    enum profile profile;

    pa_modargs *modargs;

    int stream_write_type;
    int service_write_type, service_read_type;

    pa_bool_t filter_added;

    /* required for MPEG transport */
    size_t leftover_bytes;
};

enum {
    BLUETOOTH_MESSAGE_SET_PROFILE,
    BLUETOOTH_MESSAGE_MAX
};

#define FIXED_LATENCY_PLAYBACK_A2DP (25*PA_USEC_PER_MSEC)
#define FIXED_LATENCY_RECORD_A2DP (25*PA_USEC_PER_MSEC)
#define FIXED_LATENCY_PLAYBACK_HSP (125*PA_USEC_PER_MSEC)
#define FIXED_LATENCY_RECORD_HSP (25*PA_USEC_PER_MSEC)

#define MAX_PLAYBACK_CATCH_UP_USEC (100*PA_USEC_PER_MSEC)

#define USE_SCO_OVER_PCM(u) (u->profile == PROFILE_HSP && (u->hsp.sco_sink && u->hsp.sco_source))

#define MPEG_MIN_FRAME_SIZE 4
#define MPEG_MAX_FRAME_SIZE 1728

static int init_bt(struct userdata *u);
static int init_profile(struct userdata *u);
static int bt_transport_acquire(struct userdata *u, pa_bool_t start);
static int bt_transport_config(struct userdata *u);

static int service_send(struct userdata *u, const bt_audio_msg_header_t *msg) {
    ssize_t r;

    pa_assert(u);
    pa_assert(msg);
    pa_assert(msg->length > 0);

    if (u->service_fd < 0) {
        pa_log_warn("Service not connected");
        return -1;
    }

    pa_log_debug("Sending %s -> %s",
                 pa_strnull(bt_audio_strtype(msg->type)),
                 pa_strnull(bt_audio_strname(msg->name)));

    if ((r = pa_loop_write(u->service_fd, msg, msg->length, &u->service_write_type)) == (ssize_t) msg->length)
        return 0;

    if (r < 0)
        pa_log_error("Error sending data to audio service: %s", pa_cstrerror(errno));
    else
        pa_log_error("Short write()");

    return -1;
}

static int service_recv(struct userdata *u, bt_audio_msg_header_t *msg, size_t room) {
    ssize_t r;

    pa_assert(u);
    pa_assert(u->service_fd >= 0);
    pa_assert(msg);
    pa_assert(room >= sizeof(*msg));

    pa_log_debug("Trying to receive message from audio service...");

    /* First, read the header */
    if ((r = pa_loop_read(u->service_fd, msg, sizeof(*msg), &u->service_read_type)) != sizeof(*msg))
        goto read_fail;

    if (msg->length < sizeof(*msg)) {
        pa_log_error("Invalid message size.");
        return -1;
    }

    if (msg->length > room) {
        pa_log_error("Not enough room.");
        return -1;
    }

    /* Secondly, read the payload */
    if (msg->length > sizeof(*msg)) {

        size_t remains = msg->length - sizeof(*msg);

        if ((r = pa_loop_read(u->service_fd,
                              (uint8_t*) msg + sizeof(*msg),
                              remains,
                              &u->service_read_type)) != (ssize_t) remains)
            goto read_fail;
    }

    pa_log_debug("Received %s <- %s",
                 pa_strnull(bt_audio_strtype(msg->type)),
                 pa_strnull(bt_audio_strname(msg->name)));

    return 0;

read_fail:

    if (r < 0)
        pa_log_error("Error receiving data from audio service: %s", pa_cstrerror(errno));
    else
        pa_log_error("Short read()");

    return -1;
}

static ssize_t service_expect(struct userdata*u, bt_audio_msg_header_t *rsp, size_t room, uint8_t expected_name, size_t expected_size) {
    int r;

    pa_assert(u);
    pa_assert(u->service_fd >= 0);
    pa_assert(rsp);

    if ((r = service_recv(u, rsp, room)) < 0)
        return r;

    if ((rsp->type != BT_INDICATION && rsp->type != BT_RESPONSE) ||
        rsp->name != expected_name ||
        (expected_size > 0 && rsp->length != expected_size)) {

        if (rsp->type == BT_ERROR && rsp->length == sizeof(bt_audio_error_t))
            pa_log_error("Received error condition: %s", pa_cstrerror(((bt_audio_error_t*) rsp)->posix_errno));
        else
            pa_log_error("Bogus message %s received while %s was expected",
                         pa_strnull(bt_audio_strname(rsp->name)),
                         pa_strnull(bt_audio_strname(expected_name)));
        return -1;
    }

    return 0;
}

/* Run from main thread */
static int parse_mpeg_caps(struct userdata *u, uint8_t seid, const struct bt_get_capabilities_rsp *rsp) {
    uint16_t bytes_left;
    const codec_capabilities_t *codec;

    pa_assert(u);
    pa_assert(rsp);

    u->a2dp.has_mpeg = FALSE;

    bytes_left = rsp->h.length - sizeof(*rsp);

    if (bytes_left < sizeof(codec_capabilities_t)) {
        pa_log_error("Packet too small to store codec information.");
        return -1;
    }

    codec = (codec_capabilities_t *) rsp->data; /** ALIGNMENT? **/

    pa_log_debug("Payload size is %lu %lu", (unsigned long) bytes_left, (unsigned long) sizeof(*codec));

    if (codec->transport != BT_CAPABILITIES_TRANSPORT_A2DP) {
        pa_log_error("Got capabilities for wrong codec.");
        return -1;
    }

    while (bytes_left > 0) {
        if ((codec->type == BT_A2DP_MPEG12_SINK) && (!codec->lock)) {
            break;
        }
        bytes_left -= codec->length;
        codec = (const codec_capabilities_t*) ((const uint8_t*) codec + codec->length);
    }

    if (bytes_left <= 0 || codec->length != sizeof(u->a2dp.mpeg_capabilities))
        return -1;

    pa_assert(codec->type == BT_A2DP_MPEG12_SINK);

    if (codec->configured && seid == 0)
        return codec->seid;

    u->a2dp.has_mpeg = TRUE;
    memcpy(&u->a2dp.mpeg_capabilities, codec, sizeof(u->a2dp.mpeg_capabilities));

    pa_log_info("MPEG caps detected");
    pa_log_info("channel_mode %d crc %d layer %d frequency %d mpf %d bitrate %d",
                u->a2dp.mpeg_capabilities.channel_mode,
                u->a2dp.mpeg_capabilities.crc,
                u->a2dp.mpeg_capabilities.layer,
                u->a2dp.mpeg_capabilities.frequency,
                u->a2dp.mpeg_capabilities.mpf,
                u->a2dp.mpeg_capabilities.bitrate);

    return 0;
}

/* Run from main thread */
static int parse_caps(struct userdata *u, uint8_t seid, const struct bt_get_capabilities_rsp *rsp) {
    uint16_t bytes_left;
    const codec_capabilities_t *codec;

    pa_assert(u);
    pa_assert(rsp);

    bytes_left = rsp->h.length - sizeof(*rsp);

    if (bytes_left < sizeof(codec_capabilities_t)) {
        pa_log_error("Packet too small to store codec information.");
        return -1;
    }

    codec = (codec_capabilities_t *) rsp->data; /** ALIGNMENT? **/

    pa_log_debug("Payload size is %lu %lu", (unsigned long) bytes_left, (unsigned long) sizeof(*codec));

    if (((u->profile == PROFILE_A2DP || u->profile == PROFILE_A2DP_SOURCE) && codec->transport != BT_CAPABILITIES_TRANSPORT_A2DP) ||
        ((u->profile == PROFILE_HSP || u->profile == PROFILE_HFGW) && codec->transport != BT_CAPABILITIES_TRANSPORT_SCO)) {
        pa_log_error("Got capabilities for wrong codec.");
        return -1;
    }

    if (u->profile == PROFILE_HSP || u->profile == PROFILE_HFGW) {

        if (bytes_left <= 0 || codec->length != sizeof(u->hsp.pcm_capabilities))
            return -1;

        pa_assert(codec->type == BT_HFP_CODEC_PCM);

        if (codec->configured && seid == 0)
            return codec->seid;

        memcpy(&u->hsp.pcm_capabilities, codec, sizeof(u->hsp.pcm_capabilities));

    } else if (u->profile == PROFILE_A2DP) {

        while (bytes_left > 0) {
            if ((codec->type == BT_A2DP_SBC_SINK) && !codec->lock)
                break;

            bytes_left -= codec->length;
            codec = (const codec_capabilities_t*) ((const uint8_t*) codec + codec->length);
        }

        if (bytes_left <= 0 || codec->length != sizeof(u->a2dp.sbc_capabilities))
            return -1;

        pa_assert(codec->type == BT_A2DP_SBC_SINK);

        if (codec->configured && seid == 0)
            return codec->seid;

        memcpy(&u->a2dp.sbc_capabilities, codec, sizeof(u->a2dp.sbc_capabilities));
        pa_log_info("SBC caps detected");

    } else if (u->profile == PROFILE_A2DP_SOURCE) {

        while (bytes_left > 0) {
            if ((codec->type == BT_A2DP_SBC_SOURCE) && !codec->lock)
                break;

            bytes_left -= codec->length;
            codec = (const codec_capabilities_t*) ((const uint8_t*) codec + codec->length);
        }

        if (bytes_left <= 0 || codec->length != sizeof(u->a2dp.sbc_capabilities))
            return -1;

        pa_assert(codec->type == BT_A2DP_SBC_SOURCE);

        if (codec->configured && seid == 0)
            return codec->seid;

        memcpy(&u->a2dp.sbc_capabilities, codec, sizeof(u->a2dp.sbc_capabilities));
    }

    return 0;
}

typedef union {
    struct bt_get_capabilities_req getcaps_req;
    struct bt_get_capabilities_rsp getcaps_rsp;
    bt_audio_error_t error;
    uint8_t buf[BT_SUGGESTED_BUFFER_SIZE];
} bt_getcaps_msg_t;

/* Run from main thread */
static int get_caps_msg(struct userdata *u, uint8_t seid, bt_getcaps_msg_t *msg) {

    pa_assert(u);

    memset(msg, 0, sizeof(bt_getcaps_msg_t));
    msg->getcaps_req.h.type = BT_REQUEST;
    msg->getcaps_req.h.name = BT_GET_CAPABILITIES;
    msg->getcaps_req.h.length = sizeof(struct bt_get_capabilities_req);
    msg->getcaps_req.seid = seid;

    pa_strlcpy(msg->getcaps_req.object, u->path, sizeof(msg->getcaps_req.object));
    if (u->profile == PROFILE_A2DP || u->profile == PROFILE_A2DP_SOURCE)
        msg->getcaps_req.transport = BT_CAPABILITIES_TRANSPORT_A2DP;
    else {
        pa_assert(u->profile == PROFILE_HSP || u->profile == PROFILE_HFGW);
        msg->getcaps_req.transport = BT_CAPABILITIES_TRANSPORT_SCO;
    }
    msg->getcaps_req.flags = u->auto_connect ? BT_FLAG_AUTOCONNECT : 0;

    if (service_send(u, &msg->getcaps_req.h) < 0)
        return -1;

    if (service_expect(u, &msg->getcaps_rsp.h, sizeof(bt_getcaps_msg_t), BT_GET_CAPABILITIES, 0) < 0)
        return -1;

    return 0;
}

/* Run from main thread */
static int get_caps(struct userdata *u, uint8_t seid) {
    bt_getcaps_msg_t msg;
    int ret;

    pa_assert(u);

    if (get_caps_msg(u, seid, &msg) < 0)
        return -1;

    ret = parse_caps(u, seid, &msg.getcaps_rsp);
    if (ret < 0)
        return -1;

    if (ret > 0) {
        /* refine seid caps */
        if (get_caps_msg(u, ret, &msg) < 0)
            return -1;

        ret = parse_caps(u, ret, &msg.getcaps_rsp);
        if (ret < 0)
            return -1;
    }

    seid = 0;

    if (u->profile == PROFILE_A2DP) {
        /* try to find mpeg end-point */
        if (get_caps_msg(u, seid, &msg) < 0)
            return 0;

        ret = parse_mpeg_caps(u, seid, &msg.getcaps_rsp);
        if (ret < 0)
            return 0;

        if (ret > 0) {
            /* refine seid caps */
            if (get_caps_msg(u, ret, &msg) < 0)
                return 0;

            ret = parse_mpeg_caps(u, ret, &msg.getcaps_rsp);
            if (ret < 0)
                return 0;
        }
    }

    return 0;
}

static int close_stream(struct userdata *u) {
    union {
        struct bt_close_req close_req;
        struct bt_close_rsp close_rsp;
        bt_audio_error_t error;
        uint8_t buf[BT_SUGGESTED_BUFFER_SIZE];
    } msg;

    memset(&msg, 0, sizeof(msg));
    msg.close_req.h.type = BT_REQUEST;
    msg.close_req.h.name = BT_CLOSE;
    msg.close_req.h.length = sizeof(msg.close_req);

    if (service_send(u, &msg.close_req.h) < 0)
        return -1;

    if (service_expect(u, &msg.close_rsp.h, sizeof(msg), BT_CLOSE, sizeof(msg.close_rsp)) < 0)
        return -1;

    return 0;
}

/* Run from main thread */
static uint8_t a2dp_default_bitpool(uint8_t freq, uint8_t mode) {

    switch (freq) {
        case BT_SBC_SAMPLING_FREQ_16000:
        case BT_SBC_SAMPLING_FREQ_32000:
            return 53;

        case BT_SBC_SAMPLING_FREQ_44100:

            switch (mode) {
                case BT_A2DP_CHANNEL_MODE_MONO:
                case BT_A2DP_CHANNEL_MODE_DUAL_CHANNEL:
                    return 31;

                case BT_A2DP_CHANNEL_MODE_STEREO:
                case BT_A2DP_CHANNEL_MODE_JOINT_STEREO:
                    return 53;

                default:
                    pa_log_warn("Invalid channel mode %u", mode);
                    return 53;
            }

        case BT_SBC_SAMPLING_FREQ_48000:

            switch (mode) {
                case BT_A2DP_CHANNEL_MODE_MONO:
                case BT_A2DP_CHANNEL_MODE_DUAL_CHANNEL:
                    return 29;

                case BT_A2DP_CHANNEL_MODE_STEREO:
                case BT_A2DP_CHANNEL_MODE_JOINT_STEREO:
                    return 51;

                default:
                    pa_log_warn("Invalid channel mode %u", mode);
                    return 51;
            }

        default:
            pa_log_warn("Invalid sampling freq %u", freq);
            return 53;
    }
}

/* Run from main thread */
static int setup_a2dp(struct userdata *u) {
    sbc_capabilities_t *cap;
    mpeg_capabilities_t *mcap;
    int i;

    pa_assert(u);
    pa_assert(u->profile == PROFILE_A2DP || u->profile == PROFILE_A2DP_SOURCE);

    if (u->a2dp.mode == A2DP_MODE_SBC) {

        static const struct {
            uint32_t rate;
            uint8_t cap;
        } freq_table[] = {
            { 16000U, BT_SBC_SAMPLING_FREQ_16000 },
            { 32000U, BT_SBC_SAMPLING_FREQ_32000 },
            { 44100U, BT_SBC_SAMPLING_FREQ_44100 },
            { 48000U, BT_SBC_SAMPLING_FREQ_48000 }
        };

        cap = &u->a2dp.sbc_capabilities;

        /* Find the lowest freq that is at least as high as the requested
         * sampling rate */
        for (i = 0; (unsigned) i < PA_ELEMENTSOF(freq_table); i++)
            if (freq_table[i].rate >= u->sample_spec.rate && (cap->frequency & freq_table[i].cap)) {
                u->sample_spec.rate = freq_table[i].rate;
                cap->frequency = freq_table[i].cap;
                break;
            }

        if ((unsigned) i == PA_ELEMENTSOF(freq_table)) {
            for (--i; i >= 0; i--) {
                if (cap->frequency & freq_table[i].cap) {
                    u->sample_spec.rate = freq_table[i].rate;
                    cap->frequency = freq_table[i].cap;
                    break;
                }
            }

            if (i < 0) {
                pa_log("Not suitable sample rate");
                return -1;
            }
        }

        pa_assert((unsigned) i < PA_ELEMENTSOF(freq_table));

        if (cap->capability.configured)
            return 0;

        if (u->sample_spec.channels <= 1) {
            if (cap->channel_mode & BT_A2DP_CHANNEL_MODE_MONO) {
                cap->channel_mode = BT_A2DP_CHANNEL_MODE_MONO;
                u->sample_spec.channels = 1;
            } else
                u->sample_spec.channels = 2;
        }

        if (u->sample_spec.channels >= 2) {
            u->sample_spec.channels = 2;

            if (cap->channel_mode & BT_A2DP_CHANNEL_MODE_JOINT_STEREO)
                cap->channel_mode = BT_A2DP_CHANNEL_MODE_JOINT_STEREO;
            else if (cap->channel_mode & BT_A2DP_CHANNEL_MODE_STEREO)
                cap->channel_mode = BT_A2DP_CHANNEL_MODE_STEREO;
            else if (cap->channel_mode & BT_A2DP_CHANNEL_MODE_DUAL_CHANNEL)
                cap->channel_mode = BT_A2DP_CHANNEL_MODE_DUAL_CHANNEL;
            else if (cap->channel_mode & BT_A2DP_CHANNEL_MODE_MONO) {
                cap->channel_mode = BT_A2DP_CHANNEL_MODE_MONO;
                u->sample_spec.channels = 1;
            } else {
                pa_log("No supported channel modes");
                return -1;
            }
        }

        if (cap->block_length & BT_A2DP_BLOCK_LENGTH_16)
            cap->block_length = BT_A2DP_BLOCK_LENGTH_16;
        else if (cap->block_length & BT_A2DP_BLOCK_LENGTH_12)
            cap->block_length = BT_A2DP_BLOCK_LENGTH_12;
        else if (cap->block_length & BT_A2DP_BLOCK_LENGTH_8)
            cap->block_length = BT_A2DP_BLOCK_LENGTH_8;
        else if (cap->block_length & BT_A2DP_BLOCK_LENGTH_4)
            cap->block_length = BT_A2DP_BLOCK_LENGTH_4;
        else {
            pa_log_error("No supported block lengths");
            return -1;
        }

        if (cap->subbands & BT_A2DP_SUBBANDS_8)
            cap->subbands = BT_A2DP_SUBBANDS_8;
        else if (cap->subbands & BT_A2DP_SUBBANDS_4)
            cap->subbands = BT_A2DP_SUBBANDS_4;
        else {
            pa_log_error("No supported subbands");
            return -1;
        }

        if (cap->allocation_method & BT_A2DP_ALLOCATION_LOUDNESS)
            cap->allocation_method = BT_A2DP_ALLOCATION_LOUDNESS;
        else if (cap->allocation_method & BT_A2DP_ALLOCATION_SNR)
            cap->allocation_method = BT_A2DP_ALLOCATION_SNR;

        cap->min_bitpool = (uint8_t) PA_MAX(MIN_BITPOOL, cap->min_bitpool);
        cap->max_bitpool = (uint8_t) PA_MIN(a2dp_default_bitpool(cap->frequency, cap->channel_mode), cap->max_bitpool);

    } else {
        /* Now configure the MPEG caps if we have them */
        int rate;

        pa_assert(u->a2dp.has_mpeg);

        mcap = &u->a2dp.mpeg_capabilities;
        rate = u->sample_spec.rate;

        if (u->sample_spec.channels == 1)
            mcap->channel_mode = BT_A2DP_CHANNEL_MODE_MONO;
        else
            mcap->channel_mode = BT_A2DP_CHANNEL_MODE_STEREO;

        mcap->crc = 0; /* CRC is broken in some encoders */
        mcap->layer = BT_MPEG_LAYER_3;

        if (rate == 44100)
            mcap->frequency = BT_MPEG_SAMPLING_FREQ_44100;
        else if (rate == 48000)
            mcap->frequency = BT_MPEG_SAMPLING_FREQ_48000;
        else if (rate == 32000)
            mcap->frequency = BT_MPEG_SAMPLING_FREQ_32000;
        else if (rate == 24000)
            mcap->frequency = BT_MPEG_SAMPLING_FREQ_24000;
        else if (rate == 22050)
            mcap->frequency = BT_MPEG_SAMPLING_FREQ_22050;
        else if (rate == 16000)
            mcap->frequency = BT_MPEG_SAMPLING_FREQ_16000;
        else {
            pa_log("unsupported MPEG sampling frequency");
            return -1;
        }

        mcap->mpf = 0; /* don't use optional IETF payload, send raw frames */
        mcap->bitrate = 0x8000; /* set for vbr, this covers all cases */
    }

    return 0;
}

/* Run from main thread */
static void setup_sbc(struct a2dp_info *a2dp, enum profile p) {
    sbc_capabilities_t *active_capabilities;

    pa_assert(a2dp);

    active_capabilities = &a2dp->sbc_capabilities;

    if (a2dp->sbc_initialized)
        sbc_reinit(&a2dp->sbc, 0);
    else
        sbc_init(&a2dp->sbc, 0);
    a2dp->sbc_initialized = TRUE;

    switch (active_capabilities->frequency) {
        case BT_SBC_SAMPLING_FREQ_16000:
            a2dp->sbc.frequency = SBC_FREQ_16000;
            break;
        case BT_SBC_SAMPLING_FREQ_32000:
            a2dp->sbc.frequency = SBC_FREQ_32000;
            break;
        case BT_SBC_SAMPLING_FREQ_44100:
            a2dp->sbc.frequency = SBC_FREQ_44100;
            break;
        case BT_SBC_SAMPLING_FREQ_48000:
            a2dp->sbc.frequency = SBC_FREQ_48000;
            break;
        default:
            pa_assert_not_reached();
    }

    switch (active_capabilities->channel_mode) {
        case BT_A2DP_CHANNEL_MODE_MONO:
            a2dp->sbc.mode = SBC_MODE_MONO;
            break;
        case BT_A2DP_CHANNEL_MODE_DUAL_CHANNEL:
            a2dp->sbc.mode = SBC_MODE_DUAL_CHANNEL;
            break;
        case BT_A2DP_CHANNEL_MODE_STEREO:
            a2dp->sbc.mode = SBC_MODE_STEREO;
            break;
        case BT_A2DP_CHANNEL_MODE_JOINT_STEREO:
            a2dp->sbc.mode = SBC_MODE_JOINT_STEREO;
            break;
        default:
            pa_assert_not_reached();
    }

    switch (active_capabilities->allocation_method) {
        case BT_A2DP_ALLOCATION_SNR:
            a2dp->sbc.allocation = SBC_AM_SNR;
            break;
        case BT_A2DP_ALLOCATION_LOUDNESS:
            a2dp->sbc.allocation = SBC_AM_LOUDNESS;
            break;
        default:
            pa_assert_not_reached();
    }

    switch (active_capabilities->subbands) {
        case BT_A2DP_SUBBANDS_4:
            a2dp->sbc.subbands = SBC_SB_4;
            break;
        case BT_A2DP_SUBBANDS_8:
            a2dp->sbc.subbands = SBC_SB_8;
            break;
        default:
            pa_assert_not_reached();
    }

    switch (active_capabilities->block_length) {
        case BT_A2DP_BLOCK_LENGTH_4:
            a2dp->sbc.blocks = SBC_BLK_4;
            break;
        case BT_A2DP_BLOCK_LENGTH_8:
            a2dp->sbc.blocks = SBC_BLK_8;
            break;
        case BT_A2DP_BLOCK_LENGTH_12:
            a2dp->sbc.blocks = SBC_BLK_12;
            break;
        case BT_A2DP_BLOCK_LENGTH_16:
            a2dp->sbc.blocks = SBC_BLK_16;
            break;
        default:
            pa_assert_not_reached();
    }

    a2dp->min_bitpool = active_capabilities->min_bitpool;
    a2dp->max_bitpool = active_capabilities->max_bitpool;

    /* Set minimum bitpool for source to get the maximum possible block_size */
    a2dp->sbc.bitpool = p == PROFILE_A2DP ? a2dp->max_bitpool : a2dp->min_bitpool;
    a2dp->codesize = sbc_get_codesize(&a2dp->sbc);
    a2dp->frame_length = sbc_get_frame_length(&a2dp->sbc);
}

static int bt_transport_reconfigure_cb(int err, void *data) {
    struct userdata *u = data;
    const pa_bluetooth_device *d;
    const pa_bluetooth_transport *t;

    pa_assert(u);

    pa_log_debug("Configuration for mode %s returned %d", (u->a2dp.mode == A2DP_MODE_SBC) ? "SBC":"MPEG", err);

    if (err < 0)
        return err;

    if (!(d = pa_bluetooth_discovery_get_by_path(u->discovery, u->path))) {
        pa_log_error("Failed to get device object.");
        return -1;
    }

    /* check if profile has a new transport */
    if (!(t = pa_bluetooth_device_get_transport(d, u->profile))) {
        pa_log("No transport found for profile %d", u->profile);
        return -2;
    }

    /* Acquire new transport */
    u->transport = pa_xstrdup(t->path);
    u->a2dp.has_mpeg = t->has_mpeg;
    pa_log_debug("Configured for mode %s", (u->a2dp.mode == A2DP_MODE_SBC) ? "SBC":"MPEG");

    if (bt_transport_config(u) < 0)
        return -1;

    return bt_transport_acquire(u, TRUE);
}

static int bt_transport_reconfigure(struct userdata *u, const char *endpoint) {
    const pa_bluetooth_transport *t;

    pa_log_debug("Configure for mode %s", (u->a2dp.mode == A2DP_MODE_SBC) ? "SBC":"MPEG");

    t = pa_bluetooth_discovery_get_transport(u->discovery, u->transport);
    if (!t) {
        pa_log("Transport %s no longer available", u->transport);
        pa_xfree(u->transport);
        u->transport = NULL;
        return -1;
    }

    pa_bluetooth_transport_reconfigure(t, endpoint, bt_transport_reconfigure_cb, u);

    /* After request configuration, transport will be recreated */
    pa_xfree(u->transport);
    u->transport = NULL;

    return 0;
}

/* Run from main thread */
static int set_conf(struct userdata *u) {
    union {
        struct bt_open_req open_req;
        struct bt_open_rsp open_rsp;
        struct bt_set_configuration_req setconf_req;
        struct bt_set_configuration_rsp setconf_rsp;
        bt_audio_error_t error;
        uint8_t buf[BT_SUGGESTED_BUFFER_SIZE];
    } msg;

    memset(&msg, 0, sizeof(msg));
    msg.open_req.h.type = BT_REQUEST;
    msg.open_req.h.name = BT_OPEN;
    msg.open_req.h.length = sizeof(msg.open_req);

    pa_strlcpy(msg.open_req.object, u->path, sizeof(msg.open_req.object));

    if (u->profile == PROFILE_A2DP || u->profile == PROFILE_A2DP_SOURCE) {
        if (u->a2dp.mode == A2DP_MODE_SBC)
            msg.open_req.seid = u->a2dp.sbc_capabilities.capability.seid;
        else if (u->a2dp.mode == A2DP_MODE_MPEG) {
            pa_assert(u->a2dp.has_mpeg);
            msg.open_req.seid = u->a2dp.mpeg_capabilities.capability.seid;
        }
    } else
        msg.open_req.seid = BT_A2DP_SEID_RANGE + 1;

    msg.open_req.lock = u->profile == PROFILE_A2DP ? BT_WRITE_LOCK : BT_READ_LOCK | BT_WRITE_LOCK;

    if (service_send(u, &msg.open_req.h) < 0)
        return -1;

    if (service_expect(u, &msg.open_rsp.h, sizeof(msg), BT_OPEN, sizeof(msg.open_rsp)) < 0)
        return -1;

    if (u->profile == PROFILE_A2DP || u->profile == PROFILE_A2DP_SOURCE ) {

        /* for passthrough we expect little-endian data to be compatible with the ALSA iec958
           devices */
        u->sample_spec.format = PA_SAMPLE_S16LE;

        if (setup_a2dp(u) < 0)
            return -1;
    } else {
        pa_assert(u->profile == PROFILE_HSP || u->profile == PROFILE_HFGW);

        u->sample_spec.format = PA_SAMPLE_S16LE;
        u->sample_spec.channels = 1;
        u->sample_spec.rate = 8000;
    }

    memset(&msg, 0, sizeof(msg));
    msg.setconf_req.h.type = BT_REQUEST;
    msg.setconf_req.h.name = BT_SET_CONFIGURATION;
    msg.setconf_req.h.length = sizeof(msg.setconf_req);

    if (u->profile == PROFILE_A2DP || u->profile == PROFILE_A2DP_SOURCE) {
        if (u->a2dp.mode == A2DP_MODE_SBC)
            memcpy(&msg.setconf_req.codec, &u->a2dp.sbc_capabilities, sizeof(u->a2dp.sbc_capabilities));
        else if (u->a2dp.mode == A2DP_MODE_MPEG)
            memcpy(&msg.setconf_req.codec, &u->a2dp.mpeg_capabilities, sizeof(u->a2dp.mpeg_capabilities));
    } else {
        msg.setconf_req.codec.transport = BT_CAPABILITIES_TRANSPORT_SCO;
        msg.setconf_req.codec.seid = BT_A2DP_SEID_RANGE + 1;
        msg.setconf_req.codec.length = sizeof(pcm_capabilities_t);
    }
    msg.setconf_req.h.length += msg.setconf_req.codec.length - sizeof(msg.setconf_req.codec);

    if (service_send(u, &msg.setconf_req.h) < 0) {
        close_stream(u);
        return -1;
    }

    if (service_expect(u, &msg.setconf_rsp.h, sizeof(msg), BT_SET_CONFIGURATION, sizeof(msg.setconf_rsp)) < 0) {
        close_stream(u);
        return -1;
    }

    u->link_mtu = msg.setconf_rsp.link_mtu;

    /* setup SBC encoder now we agree on parameters */
    if (u->profile == PROFILE_A2DP || u->profile == PROFILE_A2DP_SOURCE) {
        if (u->a2dp.mode == A2DP_MODE_SBC) {
            setup_sbc(&u->a2dp, u->profile);

            u->block_size =
                ((u->link_mtu - sizeof(struct rtp_header) - sizeof(struct sbc_rtp_payload))
                 / u->a2dp.frame_length
                 * u->a2dp.codesize);

            pa_log_info("SBC parameters:\n\tallocation=%u\n\tsubbands=%u\n\tblocks=%u\n\tbitpool=%u\n",
                    u->a2dp.sbc.allocation, u->a2dp.sbc.subbands, u->a2dp.sbc.blocks, u->a2dp.sbc.bitpool);
        } else if (u->a2dp.mode == A2DP_MODE_MPEG) {
            /* available payload per packet */
            u->block_size = 1152*4; /* this is the size of an IEC61937 frame for MPEG layer 3 */
        }

        u->leftover_bytes = 0;
        if (u->write_memchunk.memblock) {
            pa_memblock_unref(u->write_memchunk.memblock);
            pa_memchunk_reset(&u->write_memchunk);
        }
    } else
        u->block_size = u->link_mtu;

    return 0;
}

/* from IO thread */
static void a2dp_set_bitpool(struct userdata *u, uint8_t bitpool)
{
    struct a2dp_info *a2dp;

    pa_assert(u);

    a2dp = &u->a2dp;

    if (a2dp->sbc.bitpool == bitpool)
        return;

    if (bitpool > a2dp->max_bitpool)
        bitpool = a2dp->max_bitpool;
    else if (bitpool < a2dp->min_bitpool)
        bitpool = a2dp->min_bitpool;

    a2dp->sbc.bitpool = bitpool;

    a2dp->codesize = sbc_get_codesize(&a2dp->sbc);
    a2dp->frame_length = sbc_get_frame_length(&a2dp->sbc);

    pa_log_debug("Bitpool has changed to %u", a2dp->sbc.bitpool);

    u->block_size =
            (u->link_mtu - sizeof(struct rtp_header) - sizeof(struct sbc_rtp_payload))
            / a2dp->frame_length * a2dp->codesize;

    pa_sink_set_max_request_within_thread(u->sink, u->block_size);
    pa_sink_set_fixed_latency_within_thread(u->sink,
            FIXED_LATENCY_PLAYBACK_A2DP + pa_bytes_to_usec(u->block_size, &u->sample_spec));
}

/* from IO thread, except in SCO over PCM */

static int setup_stream(struct userdata *u) {
    struct pollfd *pollfd;
    int one;

    pa_make_fd_nonblock(u->stream_fd);
    pa_make_socket_low_delay(u->stream_fd);

    one = 1;
    if (setsockopt(u->stream_fd, SOL_SOCKET, SO_TIMESTAMP, &one, sizeof(one)) < 0)
        pa_log_warn("Failed to enable SO_TIMESTAMP: %s", pa_cstrerror(errno));

    pa_log_debug("Stream properly set up, we're ready to roll!");

    if (u->profile == PROFILE_A2DP)
        a2dp_set_bitpool(u, u->a2dp.max_bitpool);

    u->rtpoll_item = pa_rtpoll_item_new(u->rtpoll, PA_RTPOLL_NEVER, 1);
    pollfd = pa_rtpoll_item_get_pollfd(u->rtpoll_item, NULL);
    pollfd->fd = u->stream_fd;
    pollfd->events = pollfd->revents = 0;

    u->read_index = u->write_index = 0;
    u->started_at = 0;

    if (u->source)
        u->read_smoother = pa_smoother_new(
                PA_USEC_PER_SEC,
                PA_USEC_PER_SEC*2,
                TRUE,
                TRUE,
                10,
                pa_rtclock_now(),
                TRUE);

    return 0;
}

static int start_stream_fd(struct userdata *u) {
    union {
        bt_audio_msg_header_t rsp;
        struct bt_start_stream_req start_req;
        struct bt_start_stream_rsp start_rsp;
        struct bt_new_stream_ind streamfd_ind;
        bt_audio_error_t error;
        uint8_t buf[BT_SUGGESTED_BUFFER_SIZE];
    } msg;

    pa_assert(u);
    pa_assert(u->rtpoll);
    pa_assert(!u->rtpoll_item);
    pa_assert(u->stream_fd < 0);

    memset(msg.buf, 0, BT_SUGGESTED_BUFFER_SIZE);
    msg.start_req.h.type = BT_REQUEST;
    msg.start_req.h.name = BT_START_STREAM;
    msg.start_req.h.length = sizeof(msg.start_req);

    if (service_send(u, &msg.start_req.h) < 0)
        return -1;

    if (service_expect(u, &msg.rsp, sizeof(msg), BT_START_STREAM, sizeof(msg.start_rsp)) < 0)
        return -1;

    if (service_expect(u, &msg.rsp, sizeof(msg), BT_NEW_STREAM, sizeof(msg.streamfd_ind)) < 0)
        return -1;

    if ((u->stream_fd = bt_audio_service_get_data_fd(u->service_fd)) < 0) {
        pa_log("Failed to get stream fd from audio service.");
        return -1;
    }

    return setup_stream(u);
}

/* from IO thread */
static int stop_stream_fd(struct userdata *u) {
    union {
        bt_audio_msg_header_t rsp;
        struct bt_stop_stream_req start_req;
        struct bt_stop_stream_rsp start_rsp;
        bt_audio_error_t error;
        uint8_t buf[BT_SUGGESTED_BUFFER_SIZE];
    } msg;
    int r = 0;

    pa_assert(u);
    pa_assert(u->rtpoll);

    if (u->rtpoll_item) {
        pa_rtpoll_item_free(u->rtpoll_item);
        u->rtpoll_item = NULL;
    }

    if (u->stream_fd >= 0) {
        memset(msg.buf, 0, BT_SUGGESTED_BUFFER_SIZE);
        msg.start_req.h.type = BT_REQUEST;
        msg.start_req.h.name = BT_STOP_STREAM;
        msg.start_req.h.length = sizeof(msg.start_req);

        if (service_send(u, &msg.start_req.h) < 0 ||
            service_expect(u, &msg.rsp, sizeof(msg), BT_STOP_STREAM, sizeof(msg.start_rsp)) < 0)
            r = -1;

        pa_close(u->stream_fd);
        u->stream_fd = -1;
    }

    if (u->read_smoother) {
        pa_smoother_free(u->read_smoother);
        u->read_smoother = NULL;
    }

    return r;
}

static void bt_transport_release(struct userdata *u) {
    const char *accesstype = "rw";
    const pa_bluetooth_transport *t;

    /* Ignore if already released */
    if (!u->accesstype)
        return;

    pa_log_debug("Releasing transport %s", u->transport);

    t = pa_bluetooth_discovery_get_transport(u->discovery, u->transport);
    if (t)
        pa_bluetooth_transport_release(t, accesstype);

    pa_xfree(u->accesstype);
    u->accesstype = NULL;

    if (u->rtpoll_item) {
        pa_rtpoll_item_free(u->rtpoll_item);
        u->rtpoll_item = NULL;
    }

    if (u->stream_fd >= 0) {
        pa_close(u->stream_fd);
        u->stream_fd = -1;
    }

    if (u->read_smoother) {
        pa_smoother_free(u->read_smoother);
        u->read_smoother = NULL;
    }
}

static int bt_transport_acquire(struct userdata *u, pa_bool_t start) {
    const char *accesstype = "rw";
    const pa_bluetooth_transport *t;

    if (u->accesstype) {
        if (start)
            goto done;
        return 0;
    }

    pa_log_debug("Acquiring transport %s", u->transport);

    t = pa_bluetooth_discovery_get_transport(u->discovery, u->transport);
    if (!t) {
        pa_log("Transport %s no longer available", u->transport);
        pa_xfree(u->transport);
        u->transport = NULL;
        return -1;
    }

    /* FIXME: Handle in/out MTU properly when unix socket is not longer supported */
    u->stream_fd = pa_bluetooth_transport_acquire(t, accesstype, NULL, &u->link_mtu);
    if (u->stream_fd < 0)
        return -1;

    u->accesstype = pa_xstrdup(accesstype);
    pa_log_info("Transport %s acquired: fd %d", u->transport, u->stream_fd);

    if (!start)
        return 0;

done:
    pa_log_info("Transport %s resuming", u->transport);
    return setup_stream(u);
}

/* Run from IO thread */
static int sink_process_msg(pa_msgobject *o, int code, void *data, int64_t offset, pa_memchunk *chunk) {
    struct userdata *u = PA_SINK(o)->userdata;
    pa_bool_t failed = FALSE;
    int r;

    pa_assert(u->sink == PA_SINK(o));

    switch (code) {

        case PA_SINK_MESSAGE_ADD_INPUT: {
            /* If you change anything here, make sure to change the
             * sink input handling a few lines down at
             * PA_SINK_MESSAGE_FINISH_MOVE, too. */

            /* FIXME: returning failure here causes the server to just die.
             * Would be better to be able to abort the stream gracefully. */

            pa_sink_input *i = PA_SINK_INPUT(data);
            a2dp_mode_t mode;

            pa_log_debug("PA_SINK_MESSAGE_ADD_INPUT i %p encoding %d:%s passthrough %d, has_mpeg %d", i, i->format->encoding,
                        (i->format->encoding == PA_ENCODING_PCM) ? "PCM" : (i->format->encoding == PA_ENCODING_MPEG_IEC61937) ? "IEC" : "Other",
                        pa_sink_is_passthrough(u->sink),
                        u->a2dp.has_mpeg);

            if (u->profile == PROFILE_A2DP) {
                if (pa_sink_is_passthrough(u->sink) && u->a2dp.has_mpeg)
                    mode = A2DP_MODE_MPEG;
                else
                    mode = A2DP_MODE_SBC;

                if (PA_UNLIKELY(mode != u->a2dp.mode)) {
                    if (u->transport) {
                        const char *endpoint = A2DP_SOURCE_ENDPOINT;
                        bt_transport_release(u);

                        u->a2dp.mode = mode;
                        pa_log_debug("DBUS Switch to mode %s",  u->a2dp.mode == A2DP_MODE_SBC ? "SBC" : "MPEG");

                        if (u->a2dp.mode == A2DP_MODE_MPEG)
                          endpoint = A2DP_SOURCE_ENDPOINT_MPEG;

                        if (bt_transport_reconfigure(u, endpoint) < 0)
                          failed = TRUE;
                    } else {
                        /* FIXME: Just suspend should suffice? This resets the smoother */
                        if (stop_stream_fd(u) < 0 || close_stream(u) < 0) {
                            failed = TRUE;
                            break;
                        }

                        u->a2dp.mode = mode;
                        pa_log_debug("UNIX Switch to mode %s",  u->a2dp.mode == A2DP_MODE_SBC ? "SBC" : "MPEG");

                        if (set_conf(u) < 0) {
                            failed = TRUE;
                            break;
                        }

                        if (start_stream_fd(u) < 0)
                            failed = TRUE;
                    }
                }
            }

            break;
        }


        case PA_SINK_MESSAGE_SET_STATE:

            switch ((pa_sink_state_t) PA_PTR_TO_UINT(data)) {

                case PA_SINK_SUSPENDED:
                    pa_assert(PA_SINK_IS_OPENED(u->sink->thread_info.state));

                    /* Stop the device if the source is suspended as well */
                    if (!u->source || u->source->state == PA_SOURCE_SUSPENDED) {
                        /* We deliberately ignore whether stopping
                         * actually worked. Since the stream_fd is
                         * closed it doesn't really matter */
                        if (u->transport)
                            bt_transport_release(u);
                        else
                            stop_stream_fd(u);
                    }

                    break;

                case PA_SINK_IDLE:
                case PA_SINK_RUNNING:
                    if (u->sink->thread_info.state != PA_SINK_SUSPENDED)
                        break;

                    /* Resume the device if the source was suspended as well */
                    if (!u->source || u->source->state == PA_SOURCE_SUSPENDED) {
                        if (u->transport) {
                            if (bt_transport_acquire(u, TRUE) < 0)
                                failed = TRUE;
                        } else if (start_stream_fd(u) < 0)
                            failed = TRUE;
                    }
                    break;

                case PA_SINK_UNLINKED:
                case PA_SINK_INIT:
                case PA_SINK_INVALID_STATE:
                    ;
            }
            break;

        case PA_SINK_MESSAGE_GET_LATENCY: {

            if (u->read_smoother) {
                pa_usec_t wi, ri;

                ri = pa_smoother_get(u->read_smoother, pa_rtclock_now());
                wi = pa_bytes_to_usec(u->write_index + u->block_size, &u->sample_spec);

                *((pa_usec_t*) data) = wi > ri ? wi - ri : 0;
            } else {
                pa_usec_t ri, wi;

                ri = pa_rtclock_now() - u->started_at;
                wi = pa_bytes_to_usec(u->write_index, &u->sample_spec);

                *((pa_usec_t*) data) = wi > ri ? wi - ri : 0;
            }

            *((pa_usec_t*) data) += u->sink->thread_info.fixed_latency;
            return 0;
        }
    }

    r = pa_sink_process_msg(o, code, data, offset, chunk);

    return (r < 0 || !failed) ? r : -1;
}

/* Run from IO thread */
static int source_process_msg(pa_msgobject *o, int code, void *data, int64_t offset, pa_memchunk *chunk) {
    struct userdata *u = PA_SOURCE(o)->userdata;
    pa_bool_t failed = FALSE;
    int r;

    pa_assert(u->source == PA_SOURCE(o));

    switch (code) {

        case PA_SOURCE_MESSAGE_SET_STATE:

            switch ((pa_source_state_t) PA_PTR_TO_UINT(data)) {

                case PA_SOURCE_SUSPENDED:
                    pa_assert(PA_SOURCE_IS_OPENED(u->source->thread_info.state));

                    /* Stop the device if the sink is suspended as well */
                    if (!u->sink || u->sink->state == PA_SINK_SUSPENDED) {
                        if (u->transport)
                            bt_transport_release(u);
                        else
                            stop_stream_fd(u);
                    }

                    if (u->read_smoother)
                        pa_smoother_pause(u->read_smoother, pa_rtclock_now());
                    break;

                case PA_SOURCE_IDLE:
                case PA_SOURCE_RUNNING:
                    if (u->source->thread_info.state != PA_SOURCE_SUSPENDED)
                        break;

                    /* Resume the device if the sink was suspended as well */
                    if (!u->sink || u->sink->thread_info.state == PA_SINK_SUSPENDED) {
                        if (u->transport) {
                            if (bt_transport_acquire(u, TRUE) < 0)
                                failed = TRUE;
                        } else if (start_stream_fd(u) < 0)
                            failed = TRUE;
                    }
                    /* We don't resume the smoother here. Instead we
                     * wait until the first packet arrives */
                    break;

                case PA_SOURCE_UNLINKED:
                case PA_SOURCE_INIT:
                case PA_SOURCE_INVALID_STATE:
                    ;
            }
            break;

        case PA_SOURCE_MESSAGE_GET_LATENCY: {
            pa_usec_t wi, ri;

            if (u->read_smoother) {
                wi = pa_smoother_get(u->read_smoother, pa_rtclock_now());
                ri = pa_bytes_to_usec(u->read_index, &u->sample_spec);

                *((pa_usec_t*) data) = (wi > ri ? wi - ri : 0) + u->source->thread_info.fixed_latency;
            } else
                *((pa_usec_t*) data) = 0;

            return 0;
        }

    }

    r = pa_source_process_msg(o, code, data, offset, chunk);

    return (r < 0 || !failed) ? r : -1;
}

/* Called from main thread context */
static int device_process_msg(pa_msgobject *obj, int code, void *data, int64_t offset, pa_memchunk *chunk) {
    struct bluetooth_msg *u = BLUETOOTH_MSG(obj);

    switch (code) {
        case BLUETOOTH_MESSAGE_SET_PROFILE: {
            const char *profile = data;
            pa_log_debug("Switch profile to %s requested", profile);

            if (pa_card_set_profile(u->card, profile, FALSE) < 0)
                pa_log_debug("Failed to switch profile to %s", profile);
            break;
        }
    }
    return 0;
}

/* Run from IO thread */
static int hsp_process_render(struct userdata *u) {
    int ret = 0;

    pa_assert(u);
    pa_assert(u->profile == PROFILE_HSP || u->profile == PROFILE_HFGW);
    pa_assert(u->sink);

    /* First, render some data */
    if (!u->write_memchunk.memblock)
        pa_sink_render_full(u->sink, u->block_size, &u->write_memchunk);

    pa_assert(u->write_memchunk.length == u->block_size);

    for (;;) {
        ssize_t l;
        const void *p;

        /* Now write that data to the socket. The socket is of type
         * SEQPACKET, and we generated the data of the MTU size, so this
         * should just work. */

        p = (const uint8_t*) pa_memblock_acquire(u->write_memchunk.memblock) + u->write_memchunk.index;
        l = pa_write(u->stream_fd, p, u->write_memchunk.length, &u->stream_write_type);
        pa_memblock_release(u->write_memchunk.memblock);

        pa_assert(l != 0);

        if (l < 0) {

            if (errno == EINTR)
                /* Retry right away if we got interrupted */
                continue;

            else if (errno == EAGAIN)
                /* Hmm, apparently the socket was not writable, give up for now */
                break;

            pa_log_error("Failed to write data to SCO socket: %s", pa_cstrerror(errno));
            ret = -1;
            break;
        }

        pa_assert((size_t) l <= u->write_memchunk.length);

        if ((size_t) l != u->write_memchunk.length) {
            pa_log_error("Wrote memory block to socket only partially! %llu written, wanted to write %llu.",
                        (unsigned long long) l,
                        (unsigned long long) u->write_memchunk.length);
            ret = -1;
            break;
        }

        u->write_index += (uint64_t) u->write_memchunk.length;
        pa_memblock_unref(u->write_memchunk.memblock);
        pa_memchunk_reset(&u->write_memchunk);

        ret = 1;
        break;
    }

    return ret;
}

/* Run from IO thread */
static int hsp_process_push(struct userdata *u) {
    int ret = 0;
    pa_memchunk memchunk;

    pa_assert(u);
    pa_assert(u->profile == PROFILE_HSP || u->profile == PROFILE_HFGW);
    pa_assert(u->source);
    pa_assert(u->read_smoother);

    memchunk.memblock = pa_memblock_new(u->core->mempool, u->block_size);
    memchunk.index = memchunk.length = 0;

    for (;;) {
        ssize_t l;
        void *p;
        struct msghdr m;
        struct cmsghdr *cm;
        uint8_t aux[1024];
        struct iovec iov;
        pa_bool_t found_tstamp = FALSE;
        pa_usec_t tstamp;

        memset(&m, 0, sizeof(m));
        memset(&aux, 0, sizeof(aux));
        memset(&iov, 0, sizeof(iov));

        m.msg_iov = &iov;
        m.msg_iovlen = 1;
        m.msg_control = aux;
        m.msg_controllen = sizeof(aux);

        p = pa_memblock_acquire(memchunk.memblock);
        iov.iov_base = p;
        iov.iov_len = pa_memblock_get_length(memchunk.memblock);
        l = recvmsg(u->stream_fd, &m, 0);
        pa_memblock_release(memchunk.memblock);

        if (l <= 0) {

            if (l < 0 && errno == EINTR)
                /* Retry right away if we got interrupted */
                continue;

            else if (l < 0 && errno == EAGAIN)
                /* Hmm, apparently the socket was not readable, give up for now. */
                break;

            pa_log_error("Failed to read data from SCO socket: %s", l < 0 ? pa_cstrerror(errno) : "EOF");
            ret = -1;
            break;
        }

        pa_assert((size_t) l <= pa_memblock_get_length(memchunk.memblock));

        memchunk.length = (size_t) l;
        u->read_index += (uint64_t) l;

        for (cm = CMSG_FIRSTHDR(&m); cm; cm = CMSG_NXTHDR(&m, cm))
            if (cm->cmsg_level == SOL_SOCKET && cm->cmsg_type == SO_TIMESTAMP) {
                struct timeval *tv = (struct timeval*) CMSG_DATA(cm);
                pa_rtclock_from_wallclock(tv);
                tstamp = pa_timeval_load(tv);
                found_tstamp = TRUE;
                break;
            }

        if (!found_tstamp) {
            pa_log_warn("Couldn't find SO_TIMESTAMP data in auxiliary recvmsg() data!");
            tstamp = pa_rtclock_now();
        }

        pa_smoother_put(u->read_smoother, tstamp, pa_bytes_to_usec(u->read_index, &u->sample_spec));
        pa_smoother_resume(u->read_smoother, tstamp, TRUE);

        pa_source_post(u->source, &memchunk);

        ret = 1;
        break;
    }

    pa_memblock_unref(memchunk.memblock);

    return ret;
}

/* Run from IO thread */
static void a2dp_prepare_buffer(struct userdata *u) {
    pa_assert(u);

    if (u->a2dp.buffer_size >= u->link_mtu)
        return;

    u->a2dp.buffer_size = 2 * u->link_mtu;
    pa_xfree(u->a2dp.buffer);
    u->a2dp.buffer = pa_xmalloc(u->a2dp.buffer_size);
}

/* Run from IO thread */
static int a2dp_process_render(struct userdata *u) {
    struct a2dp_info *a2dp;
    struct rtp_header *header;
    struct sbc_rtp_payload *payload;
    size_t nbytes;
    void *d;
    const void *p;
    size_t to_write, to_encode;
    unsigned frame_count;
    int ret = 0;

    pa_assert(u);
    pa_assert(u->profile == PROFILE_A2DP);
    pa_assert(u->sink);

    /* First, render some data */
    if (!u->write_memchunk.memblock)
        pa_sink_render_full(u->sink, u->block_size, &u->write_memchunk);

    pa_assert(u->write_memchunk.length == u->block_size);

    a2dp_prepare_buffer(u);

    a2dp = &u->a2dp;
    header = a2dp->buffer;
    payload = (struct sbc_rtp_payload*) ((uint8_t*) a2dp->buffer + sizeof(*header));

    frame_count = 0;

    /* Try to create a packet of the full MTU */

    p = (const uint8_t*) pa_memblock_acquire(u->write_memchunk.memblock) + u->write_memchunk.index;
    to_encode = u->write_memchunk.length;

    d = (uint8_t*) a2dp->buffer + sizeof(*header) + sizeof(*payload);
    to_write = a2dp->buffer_size - sizeof(*header) - sizeof(*payload);

    while (PA_LIKELY(to_encode > 0 && to_write > 0)) {
        ssize_t written;
        ssize_t encoded;

        encoded = sbc_encode(&a2dp->sbc,
                             p, to_encode,
                             d, to_write,
                             &written);

        if (PA_UNLIKELY(encoded <= 0)) {
            pa_log_error("SBC encoding error (%li)", (long) encoded);
            pa_memblock_release(u->write_memchunk.memblock);
            return -1;
        }

/*         pa_log_debug("SBC: encoded: %lu; written: %lu", (unsigned long) encoded, (unsigned long) written); */
/*         pa_log_debug("SBC: codesize: %lu; frame_length: %lu", (unsigned long) a2dp->codesize, (unsigned long) a2dp->frame_length); */

        pa_assert_fp((size_t) encoded <= to_encode);
        pa_assert_fp((size_t) encoded == a2dp->codesize);

        pa_assert_fp((size_t) written <= to_write);
        pa_assert_fp((size_t) written == a2dp->frame_length);

        p = (const uint8_t*) p + encoded;
        to_encode -= encoded;

        d = (uint8_t*) d + written;
        to_write -= written;

        frame_count++;
    }

    pa_memblock_release(u->write_memchunk.memblock);

    pa_assert(to_encode == 0);

    PA_ONCE_BEGIN {
        pa_log_debug("Using SBC encoder implementation: %s", pa_strnull(sbc_get_implementation_info(&a2dp->sbc)));
    } PA_ONCE_END;

    /* write it to the fifo */
    memset(a2dp->buffer, 0, sizeof(*header) + sizeof(*payload));
    header->v = 2;
    header->pt = 1;
    header->sequence_number = htons(a2dp->seq_num++);
    header->timestamp = htonl(u->write_index / pa_frame_size(&u->sample_spec));
    header->ssrc = htonl(1);
    payload->frame_count = frame_count;

    nbytes = (uint8_t*) d - (uint8_t*) a2dp->buffer;

    for (;;) {
        ssize_t l;

        l = pa_write(u->stream_fd, a2dp->buffer, nbytes, &u->stream_write_type);

        pa_assert(l != 0);

        if (l < 0) {

            if (errno == EINTR)
                /* Retry right away if we got interrupted */
                continue;

            else if (errno == EAGAIN)
                /* Hmm, apparently the socket was not writable, give up for now */
                break;

            pa_log_error("Failed to write data to socket: %s", pa_cstrerror(errno));
            ret = -1;
            break;
        }

        pa_assert((size_t) l <= nbytes);

        if ((size_t) l != nbytes) {
            pa_log_warn("Wrote memory block to socket only partially! %llu written, wanted to write %llu.",
                        (unsigned long long) l,
                        (unsigned long long) nbytes);
            ret = -1;
            break;
        }

        u->write_index += (uint64_t) u->write_memchunk.length;
        pa_memblock_unref(u->write_memchunk.memblock);
        pa_memchunk_reset(&u->write_memchunk);

        ret = 1;

        break;
    }

    return ret;
}


static uint32_t uextract(uint32_t x, uint32_t position, uint32_t nbits)
{
    int mask;

    pa_assert(position <= 31);
    pa_assert(nbits <= 31);

    x = x >> position;
    mask = (1<<nbits)-1;
    x = x & mask;

    return x;
}


#define MPEG_LAYER_INDEX    4
#define MPEG_BITRATE_INDEX 16
#define MPEG_SAMPFREQ_INDEX 4
#define MPEG_INDEX 2

static unsigned short const mpeg_sampling_frequencies[MPEG_INDEX][MPEG_SAMPFREQ_INDEX] =
    {
        {22050, 24000, 16000, 0},
        {44100, 48000, 32000, 0}
    };

static short const mpeg_layer_bitrates[MPEG_INDEX][MPEG_BITRATE_INDEX] =
    {
        { 0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, -1},
        { 0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, -1}
    };

static short const mpeg_frame_length[MPEG_INDEX][MPEG_LAYER_INDEX] =
    {
        {0, 576, 1152, 384},
        {0, 1152, 1152, 384}
    };

static pa_bool_t mp3_synclength(uint32_t hi, uint32_t *len, uint32_t *sample_len)
{
    unsigned int	tmp;
    unsigned int	idex;
    unsigned int	id;
    unsigned int	bitrate;
    unsigned int	freq;
    unsigned int	bits;

    pa_assert(len != NULL);
    pa_assert(sample_len != NULL);

    tmp = uextract(hi, 21, 11);
    if (tmp == 0x7ff) {			/* valid sync word */
        tmp = uextract(hi, 19, 2);
        if (tmp != 1) {			/* valid IDex */

            idex = tmp >> 1;
            if (idex != 0) { /* MP3 2.5, not supported by A2DP */

                id = tmp & 1;
                tmp = uextract(hi, 17, 2);
                if (tmp == 0x1) {	/* layer 3 */

                    bitrate = uextract(hi, 12, 4);
                    if ((bitrate != 0) &&	/* not free format */
                        (bitrate != 0xf)) {	/* not reserved */

                        freq = uextract(hi, 10, 2);
                        if (freq != 3) {	/* valid sampling frequency */

                            tmp = uextract(hi, 9, 1);
                            bitrate = mpeg_layer_bitrates[id][bitrate] * 1000;
                            bits =
                                (unsigned int) ((bitrate  * mpeg_frame_length[id][1]) /
                                                mpeg_sampling_frequencies[id][freq]);

                            bits /= 8;	/* # of bytes */
                            if (tmp) { /* padding */
                                bits += 1;
                            }

                            /* sanity check */
                            if (bits > MPEG_MIN_FRAME_SIZE && bits <= MPEG_MAX_FRAME_SIZE) { /* max frame length */
                                *len = bits;
                                *sample_len = mpeg_frame_length[id][1];
                                return TRUE;
                            }
                        }
                    }
                }
            }
        }
    } else {
        if (hi!=0)
            pa_log("no sync word found %x", hi);
    }
    return FALSE;
}

static pa_bool_t do_sync_iec958(const uint8_t **p_src, uint32_t *input_bytes)
{
    pa_bool_t sync_found = FALSE;
    uint32_t preambles;
    const uint8_t *src;

    pa_assert(*p_src);
    src = *p_src;

    for (;;) {

        if (*input_bytes <= 4)
            break;

        /* we need 4 bytes to detect the Pa,Pb preambles */
        preambles = src[0]<<24 | src[1]<<16 | src[2]<<8 | src[3];
#define IEC958_MAGIC_NUMBER  0x72F81F4E /* little endian encoded */
        if (preambles == IEC958_MAGIC_NUMBER) { /* little endian assumed */
            sync_found = TRUE;
            break;
        } else {
            /* try to find a new syncword */
            src += 1;
            *input_bytes -= 1;
        }
    }
    *p_src=src;

    return sync_found;
}

/* Run from IO thread */
static int a2dp_passthrough_process_render(struct userdata *u) {
    struct a2dp_info *a2dp;
    struct rtp_header *header;
    struct mpeg_rtp_payload *payload;
    size_t nbytes;
    uint8_t *d;
    const uint8_t *p;
    size_t bytes_remaining = 0;     /* in the sink buffer */
    pa_bool_t sync_found = FALSE;
    pa_bool_t confirmed_sync;
    uint32_t frame_length;
    uint32_t frag_offset;
    uint32_t max_bytes;
    int ret = 0;

    pa_assert(u);
    pa_assert(u->sink);

    /* inits for output buffer */
    a2dp_prepare_buffer(u);

    /* allocate memory if needed */
    if (!u->write_memchunk.memblock) {
        pa_memchunk *c;

        pa_assert(u->leftover_bytes == 0);
        c = &u->write_memchunk;
        c->memblock = pa_memblock_new(u->sink->core->mempool, u->block_size);
        c->index = 0;
        c->length = u->block_size;
    }

    /* if no sync was found, handle the remaining part of the previous buffer */
    if (u->leftover_bytes) {
        pa_memchunk *c;
        pa_memblock *n;
        void *tdata, *sdata;
        size_t l;

        c = &u->write_memchunk;
        n = pa_memblock_new(u->sink->core->mempool, u->block_size);

        sdata = pa_memblock_acquire(c->memblock);
        tdata = pa_memblock_acquire(n);

        l = c->length;
        memcpy(tdata, (uint8_t*) sdata + c->index, l);

        pa_memblock_release(c->memblock);
        pa_memblock_release(n);

        pa_memblock_unref(c->memblock);

        c->memblock = n;
        c->index = l;
        c->length = u->block_size - l;
    }

    /* fill memchunck, previous leftover bytes have been copied into beginning of frame already */
    pa_sink_render_into_full(u->sink, &u->write_memchunk);

    bytes_remaining = u->block_size;

    /* try to find an IEC958 pattern in the buffer */
    p = (const uint8_t*) pa_memblock_acquire(u->write_memchunk.memblock);
    sync_found = do_sync_iec958(&p, &bytes_remaining);
    pa_memblock_release(u->write_memchunk.memblock);

    /* update pointers */
    u->write_memchunk.index = u->block_size-bytes_remaining;
    u->write_memchunk.length = bytes_remaining;
    u->leftover_bytes = bytes_remaining;

    if (sync_found==TRUE && u->leftover_bytes!=u->block_size) {
        /* only a partial frame found, handle in the next call */
        sync_found = FALSE;
    }

    confirmed_sync = FALSE;
    if (sync_found==TRUE) {
        /* we are aligned on an IEC958 frame boundary */
        p = (const uint8_t*) pa_memblock_acquire(u->write_memchunk.memblock);

        if (p[4] == 0x05) { /* MPEG payload */
            uint32_t frame_length2 = 0;
            uint32_t sample_len = 0;

            /* valid MPEG payload, now extract frame length */

            frame_length = p[7]<<8 | p[6]; /* in bits */
            frame_length >>= 3;              /* in bytes */

            if (mp3_synclength(p[9]<<24 | p[8]<<16 | p[11]<<8 | p[10], &frame_length2, &sample_len) == TRUE) {
                if (frame_length == frame_length2) {
                    /* now we have verified that the IEC frame and MPEG frame are in
                       agreement, we should be good to go */
                    confirmed_sync = TRUE;
                }
            } else {
                pa_log("IEC958 sync found but no MP3 sync found");
            }
        }
        pa_memblock_release(u->write_memchunk.memblock);
    }

    /* no valid sync found, clean-up and return */
    if (confirmed_sync == FALSE) {
        int toflush;

        toflush = u->block_size - bytes_remaining;
        if (toflush == 0)
            toflush = u->block_size;
        u->write_index += (uint64_t)toflush;

        /* clean-up and move on */
        if (u->leftover_bytes == u->block_size) {
            pa_memblock_unref(u->write_memchunk.memblock);
            pa_memchunk_reset(&u->write_memchunk);
            u->leftover_bytes = 0;
        }

        return 1;
    }

    /* now push the MP3 data into the A2DP link */

    /* set pointers for RTP header and payload */
    a2dp = &u->a2dp;
    header = a2dp->buffer;
    payload = (struct mpeg_rtp_payload*) ((uint8_t*) a2dp->buffer + sizeof(*header));

    /* set pointer for MP3 frame */
    p = (const uint8_t*) pa_memblock_acquire(u->write_memchunk.memblock);
    p += 8;

    max_bytes = u->link_mtu - sizeof(*header) - sizeof(*payload);
    max_bytes >>= 1;
    max_bytes <<= 1; /* make sure we handle byte pairs */

    /* round to ceiling for frame length, required for byte swaps */
    frame_length ++;
    frame_length >>= 1;
    frame_length <<= 1;

    frag_offset = 0;

    while (frame_length != 0) { /* several iterations if frame_length larger than mtu */
        uint32_t length;
        uint32_t i;

        length = frame_length;
        if (length > max_bytes)
            length = max_bytes;
        frame_length -= length;

        d = (uint8_t*) a2dp->buffer + sizeof(*header) + sizeof(*payload);

        /* swap bytes, input is assumed 16-bit little endian */
        pa_assert(!(length&1));

        for (i=0; i<length/2; i++) {
            *(d+1) = *p;
            *d = *(p+1);
            d += 2;
            p += 2;
        }

        nbytes = (uint8_t*) d - (uint8_t*) a2dp->buffer;
        pa_assert(nbytes != 0);

        /* fill RTP header and payload */
        memset(a2dp->buffer, 0, sizeof(*header) + sizeof(*payload));
        header->v = 2;   /* rtp packet v2    */
        header->pt = 14; /* MPA payload type */
        header->timestamp =
            htonl(u->write_index / pa_frame_size(&u->sample_spec)
                  * 90000
                  / u->sample_spec.rate); /* 90kHz timestamp */
        header->m = 1;   /* talk_spurt is always set */
        header->sequence_number = htons(a2dp->seq_num);
        header->ssrc = htonl(1);

        payload->mbz = 0;                   /* always zero */
        payload->frag_offset = frag_offset; /* non-zero only when frame is spread across multiple packets */

        pa_assert(nbytes != 0);
        pa_assert(nbytes <= u->link_mtu);

        ret = 0;
        for (;;) {
            ssize_t l;

            l = pa_write(u->stream_fd, a2dp->buffer, nbytes, &u->stream_write_type);

            pa_assert(l != 0);

            if (l < 0) {

                if (errno == EINTR)
                    /* Retry right away if we got interrupted */
                    continue;

                else if (errno == EAGAIN)
                    /* Hmm, apparently the socket was not writable, give up for now */
                    break;

                pa_log_error("Failed to write data to socket: %s", pa_cstrerror(errno));
                ret  = -1;
                break;
            }

            pa_assert((size_t) l <= nbytes);

            if ((size_t) l != nbytes) {
                pa_log_warn("Wrote memory block to socket only partially! %llu written, wanted to write %llu.",
                            (unsigned long long) l,
                            (unsigned long long) nbytes);
                ret = -1;
                break;
            }

            ret = 1;
            break;
        }

        if (ret != 1) {
            /* stop this loop */
            frame_length = 0;
        }

        frag_offset += length;
    }

    pa_memblock_release(u->write_memchunk.memblock);

    /* clean-up and move on */
    if (u->leftover_bytes == u->block_size) {
        pa_memblock_unref(u->write_memchunk.memblock);
        pa_memchunk_reset(&u->write_memchunk);
        u->leftover_bytes = 0;
    }

    /* update counters */
    u->write_index += (uint64_t) u->block_size;
    a2dp->seq_num++;

    return ret;
}

/* Size of an IEC958 padded MPEG frame. */
#define MPEGP_IEC_FRAME_SIZE (1152*4)
/* Size of the IEC958 header. */
#define MPEGP_IEC_HEADER_SIZE 8
/* Size of the MPEG header. */
#define MPEGP_MPEG_HEADER_SIZE 4

typedef struct {
  uint8_t sync_byte;

  uint8_t protect:1;
  uint8_t layer:2;
  uint8_t sync:3;
  uint8_t version:2;

  uint8_t priv:1;
  uint8_t padding:1;
  uint8_t sampling:2;
  uint8_t bitrate:4;

  uint8_t emphasys:2;
  uint8_t original:1;
  uint8_t copyright:1;
  uint8_t extension:2;
  uint8_t channelmode:2;
} mpeg_header;

static int mp3_length(uint32_t header)
{
    mpeg_header *hdr = (mpeg_header *)&header;
    int mpeg_frame_size = mpeg_layer_bitrates[hdr->version & 0x1][hdr->bitrate] * 1000
                                          * mpeg_frame_length[hdr->version & 0x1][hdr->layer]
                                          / mpeg_sampling_frequencies[hdr->version & 0x1][hdr->sampling] / 8;

    if (hdr->padding)
        mpeg_frame_size ++;

    return mpeg_frame_size;
}

static int a2dp_process_push(struct userdata *u) {
    int ret = 0;
    pa_memchunk memchunk;

    pa_assert(u);
    pa_assert(u->profile == PROFILE_A2DP_SOURCE);
    pa_assert(u->source);
    pa_assert(u->read_smoother);

    if (u->write_index == 0) {
        memchunk.memblock = pa_memblock_new(u->core->mempool, u->block_size);
        memchunk.index = memchunk.length = 0;
    } else {
        memchunk = u->write_memchunk;
    }

    for (;;) {
        pa_bool_t found_tstamp = FALSE;
        pa_bool_t complete = FALSE;
        pa_usec_t tstamp;
        struct a2dp_info *a2dp;
        struct rtp_header *header;
        int payload_size;
        const void *p;
        void *d;
        ssize_t l;
        size_t to_write, to_decode;

        a2dp_prepare_buffer(u);

        a2dp = &u->a2dp;
        header = a2dp->buffer;

        l = pa_read(u->stream_fd, a2dp->buffer, a2dp->buffer_size, &u->stream_write_type);

        if (l <= 0) {

            if (l < 0 && errno == EINTR)
                /* Retry right away if we got interrupted */
                continue;

            else if (l < 0 && errno == EAGAIN)
                /* Hmm, apparently the socket was not readable, give up for now. */
                break;

            pa_log_error("Failed to read data from socket: %s", l < 0 ? pa_cstrerror(errno) : "EOF");
            ret = -1;
            break;
        }

        pa_assert((size_t) l <= a2dp->buffer_size);

        u->read_index += (uint64_t) l;

        /* TODO: get timestamp from rtp */
        if (!found_tstamp) {
            /* pa_log_warn("Couldn't find SO_TIMESTAMP data in auxiliary recvmsg() data!"); */
            tstamp = pa_rtclock_now();
        }

        pa_smoother_put(u->read_smoother, tstamp, pa_bytes_to_usec(u->read_index, &u->sample_spec));
        pa_smoother_resume(u->read_smoother, tstamp, TRUE);

        payload_size = (a2dp->mode == A2DP_MODE_SBC) ? sizeof(struct sbc_rtp_payload) : sizeof(struct mpeg_rtp_payload);
        p = (uint8_t*) a2dp->buffer + sizeof(*header) + payload_size;
        to_decode = l - sizeof(*header) - payload_size;

        d = pa_memblock_acquire(memchunk.memblock);
        if (memchunk.length == 0)
            memchunk.length = pa_memblock_get_length(memchunk.memblock);

        to_write = memchunk.length;

        while (PA_LIKELY(to_decode > 0)) {
            size_t written;
            ssize_t decoded;

            switch (a2dp->mode) {
            case A2DP_MODE_SBC:

                decoded = sbc_decode(&a2dp->sbc,
                                     p, to_decode,
                                     d, to_write,
                                     &written);

                if (PA_UNLIKELY(decoded <= 0)) {
                    pa_log_error("SBC decoding error (%li)", (long) decoded);
                    pa_memblock_release(memchunk.memblock);
                    pa_memblock_unref(memchunk.memblock);
                    return -1;
                }

/*                 pa_log_debug("SBC: decoded: %lu; written: %lu", (unsigned long) decoded, (unsigned long) written); */
/*                 pa_log_debug("SBC: frame_length: %lu; codesize: %lu", (unsigned long) a2dp->frame_length, (unsigned long) a2dp->codesize); */

                /* Reset frame length, it can be changed due to bitpool change */
                a2dp->frame_length = sbc_get_frame_length(&a2dp->sbc);

                pa_assert_fp((size_t) decoded <= to_decode);
                pa_assert_fp((size_t) decoded == a2dp->frame_length);
                pa_assert_fp((size_t) written == a2dp->codesize);
                complete = TRUE;
                break;

            case A2DP_MODE_MPEG: {
                uint8_t *iec;
                const uint8_t *orig;
                size_t mp3_len, payload_len, swap_len, sample_length;
                size_t i;

                orig = p;
                pa_log_debug("MPEG: first bytes received : %x %x %x %x", (int)orig[0], (int)orig[1], (int)orig[2], (int)orig[3]);
                if (u->write_index == 0) {

                    if (mp3_synclength(orig[0]<<24 | orig[1]<<16 | orig[2]<<8 | orig[3], &mp3_len, &sample_length)) {

                        mp3_len = mp3_length(orig[0]<<24 | orig[1]<<16 | orig[2]<<8 | orig[3]);

                        /* round to ceiling for payload length, required because of byte swaps */
                        payload_len = mp3_len + 1;
                        payload_len >>= 1;
                        payload_len <<= 1;

                        /* memblock contains enough room for full IEC frame */

                        /* If the frame is not complete, then either
                              store and wait for another frame
                       or push the existing data into PA (is it possible or useful, the zero padding would not be added?)
                          his should not add latency since nothing can be done using an incomplete frame
                          note that the iec header is added only on a starting frame
                          */
                        /* Maximum MPEG frame as seen elsewhere in this code : MPEG_MAX_FRAME_SIZE bytes */
                        /* Example bluetooth MTU 672 bytes */

                        /* Build IEC header (16le formatting) */
                        iec = d;
                        *iec++ = 0x72;
                        *iec++ = 0xf8;
                        *iec++ = 0x1f;
                        *iec++ = 0x4e;
                        *iec++ = 0x05;
                        *iec++ = 0x05;
                        *iec++ = (mp3_len * 8) & 0xFF;
                        *iec++ = ((mp3_len * 8) >> 8) & 0xFF;

                        /* Take the incoming bytes and swap to 16LE */
                        swap_len = PA_MIN(to_decode,payload_len);
                        for (i = 0; i < swap_len / 2; i++) {
                            *iec++ = *(orig + 1);
                            *iec++ = *orig;
                            orig += 2;
                        }
                        if (swap_len % 2) {
                            *iec++ = 0;
                            *iec++ = *orig++;
                        }

                        /* FIXME: Should use mp3_len or payload_len? */
                        /* Payload_len works with packet sent from pulse, but may not work if packet is sent by someone else */
                        if (payload_len < to_decode) {
                            /* This +1 is a really dirty hack because A2DP process render has sent a packet of the wrong size */
                            /* Not implemented, should push packet and continue buffer processing */
                            /* Not sure this happen in real life though */
                            pa_assert_not_reached();
                        } else if(payload_len == to_decode) {
                            pa_assert(payload_len == (size_t)iec - (size_t)d - 8);
                            /* If an MP3 packet is processed, pad with zeroes and push */
                            memset(iec, 0, u->block_size - payload_len - 8);
                            decoded = to_decode;
                            written = memchunk.length;
                            complete = TRUE;
                        } else /* payload_len > to_decode */ {
                            /* If we have only a part of an MP3 packet, then keep track of current state for next packet */
                            pa_log_debug("MPEG: fragmented MP3 frame, mp3_len %u, to_decode %u", mp3_len, to_decode);
                            u->write_index = to_decode; /* Number of MPEG data written until now */
                            pa_assert(u->write_index % 2 == 0);
                            u->write_memchunk = memchunk;
                            pa_memblock_ref(memchunk.memblock);
                            decoded = to_decode;
                            written = decoded;
                        }

                        pa_log_debug("MPEG: decoded: %lu; written: %lu", (unsigned long) decoded, (unsigned long) written);
                    } else {
                        pa_log("MPEG: Invalid frame, dropped");
                        pa_assert_not_reached();
                    }
                } else {
                    const uint8_t *hdr = d;
                    pa_log_debug("MPEG: first bytes stored : %x %x %x %x %x %x %x %x %x %x %x %x", (int)hdr[0], (int)hdr[1], (int)hdr[2], (int)hdr[3], (int)hdr[4], (int)hdr[5], (int)hdr[6], (int)hdr[7], (int)hdr[8], (int)hdr[9], (int)hdr[10], (int)hdr[11]);
                    mp3_len = mp3_length(hdr[10]<<24 | hdr[11]<<16 | hdr[8]<<8 | hdr[9]);
                    pa_log_debug("MPEG: Continuation frame  u->write_index %lu, mp3_len %u, to_decode %u", (long unsigned int)u->write_index, mp3_len, to_decode);

                    /* round to ceiling for payload length, required because of byte swaps */
                    payload_len = mp3_len + 1;
                    payload_len >>= 1;
                    payload_len <<= 1;

                    pa_assert(u->write_index % 2 == 0);
                    pa_assert(u->write_index < MPEG_MAX_FRAME_SIZE);
                    pa_assert(mp3_len <= MPEG_MAX_FRAME_SIZE);

                    iec = d;
                    iec += u->write_index + 8;

                    /* Take the incoming bytes and swap to 16LE */
                    swap_len = PA_MIN(to_decode, payload_len - u->write_index);
                    for (i = 0; i < swap_len / 2; i++) {
                        *iec++ = *(orig + 1);
                        *iec++ = *orig;
                        orig += 2;
                    }
                    if (swap_len % 2) {
                        *iec++ = 0;
                        *iec++ = *orig++;
                    }

                    if (payload_len == u->write_index + swap_len) {
                        pa_assert(payload_len == (size_t)iec - (size_t)d - 8);
                        /* Pad with zeroes */
                        pa_log_debug("MPEG: fragmented MP3 frame, mp3_len %u, swap_len %u", mp3_len, swap_len);
                        memset(iec, 0, u->block_size - u->write_index - swap_len - 8);
                        decoded = to_decode;
                        written = memchunk.length;
                        complete = TRUE;
                    } else if (payload_len > u->write_index + swap_len) {
                        pa_log_debug("MPEG: to be continued");
                        u->write_index += swap_len;
                        pa_assert(u->write_index % 2 == 0);
                        decoded = to_decode;
                        written = decoded;
                    } else {
                        pa_log("MPEG: Something is wrong, mp3_len %u < u->write_index %u +  swap_len %u", mp3_len, (unsigned int)u->write_index, swap_len);
                    }
                }
                break;
            }
            default:
                pa_assert_not_reached();
            }

            p = (const uint8_t*) p + decoded;
            to_decode -= decoded;

            d = (uint8_t*) d + written;
            to_write -= written;
        }

        memchunk.length -= to_write;
        pa_memblock_release(memchunk.memblock);

        if (complete) {
            pa_source_post(u->source, &memchunk);
            pa_memblock_unref(memchunk.memblock);

            u->write_index = u->write_memchunk.index = u->write_memchunk.length = 0;
            u->write_memchunk.memblock = NULL;
        } else {
            pa_log_debug("MPEG: frame kept for later");
        }

        ret = 1;
        break;
    }

    return ret;
}

static void a2dp_reduce_bitpool(struct userdata *u)
{
    struct a2dp_info *a2dp;
    uint8_t bitpool;

    pa_assert(u);

    a2dp = &u->a2dp;

    /* Check if bitpool is already at its limit */
    if (a2dp->sbc.bitpool <= BITPOOL_DEC_LIMIT)
        return;

    bitpool = a2dp->sbc.bitpool - BITPOOL_DEC_STEP;

    if (bitpool < BITPOOL_DEC_LIMIT)
        bitpool = BITPOOL_DEC_LIMIT;

    a2dp_set_bitpool(u, bitpool);
}

static void thread_func(void *userdata) {
    struct userdata *u = userdata;
    unsigned do_write = 0;
    pa_bool_t writable = FALSE;

    pa_assert(u);

    pa_log_debug("IO Thread starting up");

    if (u->core->realtime_scheduling)
        pa_make_realtime(u->core->realtime_priority);

    pa_thread_mq_install(&u->thread_mq);

    if (u->transport) {
        if (bt_transport_acquire(u, TRUE) < 0)
            goto fail;
    } else if (start_stream_fd(u) < 0)
        goto fail;

    for (;;) {
        struct pollfd *pollfd;
        int ret;
        pa_bool_t disable_timer = TRUE;

        pollfd = u->rtpoll_item ? pa_rtpoll_item_get_pollfd(u->rtpoll_item, NULL) : NULL;

        if (u->source && PA_SOURCE_IS_LINKED(u->source->thread_info.state)) {

            /* We should send two blocks to the device before we expect
             * a response. */

            if (u->write_index == 0 && u->read_index <= 0)
                do_write = 2;

            if (pollfd && (pollfd->revents & POLLIN)) {
                int n_read;

                if (u->profile == PROFILE_HSP || u->profile == PROFILE_HFGW)
                    n_read = hsp_process_push(u);
                else
                    n_read = a2dp_process_push(u);

                if (n_read < 0)
                    goto fail;

                /* We just read something, so we are supposed to write something, too */
                do_write += n_read;
            }
        }

        if (u->sink && PA_SINK_IS_LINKED(u->sink->thread_info.state)) {

            if (u->sink->thread_info.rewind_requested)
                pa_sink_process_rewind(u->sink, 0);

            if (pollfd) {
                if (pollfd->revents & POLLOUT)
                    writable = TRUE;

                if ((!u->source || !PA_SOURCE_IS_LINKED(u->source->thread_info.state)) && do_write <= 0 && writable) {
                    pa_usec_t time_passed;
                    pa_usec_t audio_sent;

                    /* Hmm, there is no input stream we could synchronize
                     * to. So let's do things by time */

                    time_passed = pa_rtclock_now() - u->started_at;
                    audio_sent = pa_bytes_to_usec(u->write_index, &u->sample_spec);

                    if (audio_sent <= time_passed) {
                        pa_usec_t audio_to_send = time_passed - audio_sent;

                        /* Never try to catch up for more than 100ms */
                        if (u->write_index > 0 && audio_to_send > MAX_PLAYBACK_CATCH_UP_USEC) {
                            pa_usec_t skip_usec;
                            uint64_t skip_bytes;

                            skip_usec = audio_to_send - MAX_PLAYBACK_CATCH_UP_USEC;
                            skip_bytes = pa_usec_to_bytes(skip_usec, &u->sample_spec);

                            if (skip_bytes > 0) {
                                pa_memchunk tmp;

                                pa_log_warn("Skipping %llu us (= %llu bytes) in audio stream",
                                            (unsigned long long) skip_usec,
                                            (unsigned long long) skip_bytes);

                                pa_sink_render_full(u->sink, skip_bytes, &tmp);
                                pa_memblock_unref(tmp.memblock);
                                u->write_index += skip_bytes;

                                if (u->profile == PROFILE_A2DP)
                                    a2dp_reduce_bitpool(u);
                            }
                        }

                        do_write = 1;
                    }
                }

                if (writable && do_write > 0) {
                    int n_written;

                    if (u->write_index <= 0)
                        u->started_at = pa_rtclock_now();

                    if (u->profile == PROFILE_A2DP) {
                        if (u->a2dp.mode == A2DP_MODE_SBC) {
                            if ((n_written = a2dp_process_render(u)) < 0)
                                goto fail;
                        } else if (u->a2dp.mode == A2DP_MODE_MPEG) {
                            if ((n_written = a2dp_passthrough_process_render(u)) < 0)
                                goto fail;
                        } else
                            pa_assert_not_reached();
                    } else {
                        if ((n_written = hsp_process_render(u)) < 0)
                            goto fail;
                    }

                    if (n_written == 0)
                        pa_log("Broken kernel: we got EAGAIN on write() after POLLOUT!");

                    do_write -= n_written;
                    writable = FALSE;
                }

                if ((!u->source || !PA_SOURCE_IS_LINKED(u->source->thread_info.state)) && do_write <= 0) {
                    pa_usec_t sleep_for;
                    pa_usec_t time_passed, next_write_at;

                    if (writable) {
                        /* Hmm, there is no input stream we could synchronize
                         * to. So let's estimate when we need to wake up the latest */
                        time_passed = pa_rtclock_now() - u->started_at;
                        next_write_at = pa_bytes_to_usec(u->write_index, &u->sample_spec);
                        sleep_for = time_passed < next_write_at ? next_write_at - time_passed : 0;
                        /* pa_log("Sleeping for %lu; time passed %lu, next write at %lu", (unsigned long) sleep_for, (unsigned long) time_passed, (unsigned long)next_write_at); */
                    } else
                        /* drop stream every 500 ms */
                        sleep_for = PA_USEC_PER_MSEC * 500;

                    pa_rtpoll_set_timer_relative(u->rtpoll, sleep_for);
                    disable_timer = FALSE;
                }
            }
        }

        if (disable_timer)
            pa_rtpoll_set_timer_disabled(u->rtpoll);

        /* Hmm, nothing to do. Let's sleep */
        if (pollfd)
            pollfd->events = (short) (((u->sink && PA_SINK_IS_LINKED(u->sink->thread_info.state) && !writable) ? POLLOUT : 0) |
                                      (u->source && PA_SOURCE_IS_LINKED(u->source->thread_info.state) ? POLLIN : 0));

        if ((ret = pa_rtpoll_run(u->rtpoll, TRUE)) < 0)
            goto fail;

        if (ret == 0)
            goto finish;

        pollfd = u->rtpoll_item ? pa_rtpoll_item_get_pollfd(u->rtpoll_item, NULL) : NULL;

        if (pollfd && (pollfd->revents & ~(POLLOUT|POLLIN))) {
            pa_log_info("FD error: %s%s%s%s",
                        pollfd->revents & POLLERR ? "POLLERR " :"",
                        pollfd->revents & POLLHUP ? "POLLHUP " :"",
                        pollfd->revents & POLLPRI ? "POLLPRI " :"",
                        pollfd->revents & POLLNVAL ? "POLLNVAL " :"");
            goto fail;
        }
    }

fail:
    /* If this was no regular exit from the loop we have to continue processing messages until we receive PA_MESSAGE_SHUTDOWN */
    pa_log_debug("IO thread failed");
    pa_asyncmsgq_post(pa_thread_mq_get()->outq, PA_MSGOBJECT(u->msg), BLUETOOTH_MESSAGE_SET_PROFILE, "off", 0, NULL, NULL);
    pa_asyncmsgq_wait_for(u->thread_mq.inq, PA_MESSAGE_SHUTDOWN);

finish:
    pa_log_debug("IO thread shutting down");
}

/* Run from main thread */
static DBusHandlerResult filter_cb(DBusConnection *bus, DBusMessage *m, void *userdata) {
    DBusError err;
    struct userdata *u;

    pa_assert(bus);
    pa_assert(m);
    pa_assert_se(u = userdata);

    dbus_error_init(&err);

    pa_log_debug("dbus: interface=%s, path=%s, member=%s\n",
                 dbus_message_get_interface(m),
                 dbus_message_get_path(m),
                 dbus_message_get_member(m));

    if (!dbus_message_has_path(m, u->path) && !dbus_message_has_path(m, u->transport))
        goto fail;

    if (dbus_message_is_signal(m, "org.bluez.Headset", "SpeakerGainChanged") ||
        dbus_message_is_signal(m, "org.bluez.Headset", "MicrophoneGainChanged")) {

        dbus_uint16_t gain;
        pa_cvolume v;

        if (!dbus_message_get_args(m, &err, DBUS_TYPE_UINT16, &gain, DBUS_TYPE_INVALID) || gain > HSP_MAX_GAIN) {
            pa_log("Failed to parse org.bluez.Headset.{Speaker|Microphone}GainChanged: %s", err.message);
            goto fail;
        }

        if (u->profile == PROFILE_HSP) {
            if (u->sink && dbus_message_is_signal(m, "org.bluez.Headset", "SpeakerGainChanged")) {
                pa_volume_t volume = (pa_volume_t) (gain * PA_VOLUME_NORM / HSP_MAX_GAIN);

                /* increment volume by one to correct rounding errors */
                if (volume < PA_VOLUME_NORM)
                    volume++;

                pa_cvolume_set(&v, u->sample_spec.channels, volume);
                pa_sink_volume_changed(u->sink, &v);

            } else if (u->source && dbus_message_is_signal(m, "org.bluez.Headset", "MicrophoneGainChanged")) {
                pa_volume_t volume = (pa_volume_t) (gain * PA_VOLUME_NORM / HSP_MAX_GAIN);

                /* increment volume by one to correct rounding errors */
                if (volume < PA_VOLUME_NORM)
                    volume++;

                pa_cvolume_set(&v, u->sample_spec.channels, volume);
                pa_source_volume_changed(u->source, &v);
            }
        }
    } else if (dbus_message_is_signal(m, "org.bluez.MediaTransport", "PropertyChanged")) {
        DBusMessageIter arg_i;
        pa_bluetooth_transport *t;
        pa_bool_t nrec;

        t = (pa_bluetooth_transport *) pa_bluetooth_discovery_get_transport(u->discovery, u->transport);
        pa_assert(t);

        if (!dbus_message_iter_init(m, &arg_i)) {
            pa_log("Failed to parse PropertyChanged: %s", err.message);
            goto fail;
        }

        nrec = t->nrec;

        if (pa_bluetooth_transport_parse_property(t, &arg_i) < 0)
            goto fail;

        if (nrec != t->nrec) {
            pa_log_debug("dbus: property 'NREC' changed to value '%s'", t->nrec ? "True" : "False");
            pa_proplist_sets(u->source->proplist, "bluetooth.nrec", t->nrec ? "1" : "0");
        }
    } else if (dbus_message_is_signal(m, "org.bluez.HandsfreeGateway", "PropertyChanged")) {
        const char *key;
        DBusMessageIter iter;
        DBusMessageIter variant;
        pa_bt_audio_state_t state = PA_BT_AUDIO_STATE_INVALID;

        if (!dbus_message_iter_init(m, &iter)) {
            pa_log("Failed to parse PropertyChanged: %s", err.message);
            goto fail;
        }

        if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_STRING) {
            pa_log("Property name not a string.");
            goto fail;
        }

        dbus_message_iter_get_basic(&iter, &key);

        if (!dbus_message_iter_next(&iter)) {
            pa_log("Property value missing");
            goto fail;
        }

        dbus_message_iter_recurse(&iter, &variant);

        if (dbus_message_iter_get_arg_type(&variant) == DBUS_TYPE_STRING) {
            const char *value;
            dbus_message_iter_get_basic(&variant, &value);

            if (pa_streq(key, "State")) {
                pa_log_debug("dbus: HSHFAG property 'State' changed to value '%s'", value);
                state = pa_bt_audio_state_from_string(value);
            }
        }

        switch(state) {
            case PA_BT_AUDIO_STATE_INVALID:
            case PA_BT_AUDIO_STATE_DISCONNECTED:
            case PA_BT_AUDIO_STATE_CONNECTED:
            case PA_BT_AUDIO_STATE_CONNECTING:
                goto fail;

            case PA_BT_AUDIO_STATE_PLAYING:
                if (u->card) {
                    pa_log_debug("Changing profile to hfgw");
                    if (pa_card_set_profile(u->card, "hfgw", FALSE) < 0)
                        pa_log("Failed to change profile to hfgw");
                }
                break;
        }
    }

fail:
    dbus_error_free(&err);

    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

/* Run from main thread */
static void sink_set_volume_cb(pa_sink *s) {
    DBusMessage *m;
    dbus_uint16_t gain;
    pa_volume_t volume;
    struct userdata *u;
    char *k;

    pa_assert(s);
    pa_assert(s->core);

    k = pa_sprintf_malloc("bluetooth-device@%p", (void*) s);
    u = pa_shared_get(s->core, k);
    pa_xfree(k);

    pa_assert(u);
    pa_assert(u->sink == s);
    pa_assert(u->profile == PROFILE_HSP);

    gain = (pa_cvolume_max(&s->real_volume) * HSP_MAX_GAIN) / PA_VOLUME_NORM;

    if (gain > HSP_MAX_GAIN)
        gain = HSP_MAX_GAIN;

    volume = (pa_volume_t) (gain * PA_VOLUME_NORM / HSP_MAX_GAIN);

    /* increment volume by one to correct rounding errors */
    if (volume < PA_VOLUME_NORM)
        volume++;

    pa_cvolume_set(&s->real_volume, u->sample_spec.channels, volume);

    pa_assert_se(m = dbus_message_new_method_call("org.bluez", u->path, "org.bluez.Headset", "SetSpeakerGain"));
    pa_assert_se(dbus_message_append_args(m, DBUS_TYPE_UINT16, &gain, DBUS_TYPE_INVALID));
    pa_assert_se(dbus_connection_send(pa_dbus_connection_get(u->connection), m, NULL));
    dbus_message_unref(m);
}

/* Run from main thread */
static void source_set_volume_cb(pa_source *s) {
    DBusMessage *m;
    dbus_uint16_t gain;
    pa_volume_t volume;
    struct userdata *u;
    char *k;

    pa_assert(s);
    pa_assert(s->core);

    k = pa_sprintf_malloc("bluetooth-device@%p", (void*) s);
    u = pa_shared_get(s->core, k);
    pa_xfree(k);

    pa_assert(u);
    pa_assert(u->source == s);
    pa_assert(u->profile == PROFILE_HSP);

    gain = (pa_cvolume_max(&s->real_volume) * HSP_MAX_GAIN) / PA_VOLUME_NORM;

    if (gain > HSP_MAX_GAIN)
        gain = HSP_MAX_GAIN;

    volume = (pa_volume_t) (gain * PA_VOLUME_NORM / HSP_MAX_GAIN);

    /* increment volume by one to correct rounding errors */
    if (volume < PA_VOLUME_NORM)
        volume++;

    pa_cvolume_set(&s->real_volume, u->sample_spec.channels, volume);

    pa_assert_se(m = dbus_message_new_method_call("org.bluez", u->path, "org.bluez.Headset", "SetMicrophoneGain"));
    pa_assert_se(dbus_message_append_args(m, DBUS_TYPE_UINT16, &gain, DBUS_TYPE_INVALID));
    pa_assert_se(dbus_connection_send(pa_dbus_connection_get(u->connection), m, NULL));
    dbus_message_unref(m);
}

/* Run from main thread */
static char *get_name(const char *type, pa_modargs *ma, const char *device_id, pa_bool_t *namereg_fail) {
    char *t;
    const char *n;

    pa_assert(type);
    pa_assert(ma);
    pa_assert(device_id);
    pa_assert(namereg_fail);

    t = pa_sprintf_malloc("%s_name", type);
    n = pa_modargs_get_value(ma, t, NULL);
    pa_xfree(t);

    if (n) {
        *namereg_fail = TRUE;
        return pa_xstrdup(n);
    }

    if ((n = pa_modargs_get_value(ma, "name", NULL)))
        *namereg_fail = TRUE;
    else {
        n = device_id;
        *namereg_fail = FALSE;
    }

    return pa_sprintf_malloc("bluez_%s.%s", type, n);
}

static int sco_over_pcm_state_update(struct userdata *u, pa_bool_t changed) {
    pa_assert(u);
    pa_assert(USE_SCO_OVER_PCM(u));

    if (PA_SINK_IS_OPENED(pa_sink_get_state(u->hsp.sco_sink)) ||
        PA_SOURCE_IS_OPENED(pa_source_get_state(u->hsp.sco_source))) {

        if (u->service_fd >= 0 && u->stream_fd >= 0)
            return 0;

        init_bt(u);

        pa_log_debug("Resuming SCO over PCM");
        if (init_profile(u) < 0) {
            pa_log("Can't resume SCO over PCM");
            return -1;
        }

        if (u->transport)
            return bt_transport_acquire(u, TRUE);

        return start_stream_fd(u);
    }

    if (changed) {
        if (u->service_fd < 0 && u->stream_fd < 0)
            return 0;

        pa_log_debug("Closing SCO over PCM");

        if (u->transport)
            bt_transport_release(u);
        else if (u->stream_fd >= 0)
            stop_stream_fd(u);

        if (u->service_fd >= 0) {
            pa_close(u->service_fd);
            u->service_fd = -1;
        }
    }

    return 0;
}

static pa_hook_result_t sink_state_changed_cb(pa_core *c, pa_sink *s, struct userdata *u) {
    pa_assert(c);
    pa_sink_assert_ref(s);
    pa_assert(u);

    if (s != u->hsp.sco_sink)
        return PA_HOOK_OK;

    sco_over_pcm_state_update(u, TRUE);

    return PA_HOOK_OK;
}

static pa_hook_result_t source_state_changed_cb(pa_core *c, pa_source *s, struct userdata *u) {
    pa_assert(c);
    pa_source_assert_ref(s);
    pa_assert(u);

    if (s != u->hsp.sco_source)
        return PA_HOOK_OK;

    sco_over_pcm_state_update(u, TRUE);

    return PA_HOOK_OK;
}

static pa_idxset* sink_get_formats(pa_sink *s) {
    struct userdata *u;
    pa_idxset *formats;
    pa_format_info *f;

    pa_assert(s);

    formats = pa_idxset_new(NULL, NULL);

    f = pa_format_info_new();
    f->encoding = PA_ENCODING_PCM;
    pa_idxset_put(formats, f, NULL);

    u = (struct userdata *) s->userdata;

    if (u->profile == PROFILE_A2DP && u->a2dp.has_mpeg) {
        f = pa_format_info_new();
        f->encoding = PA_ENCODING_MPEG_IEC61937;
        /* FIXME: Populate supported rates, layers, ... */
        pa_idxset_put(formats, f, NULL);
    }

    return formats;
}

static pa_idxset* source_get_formats(pa_source *s) {
    struct userdata *u;
    pa_idxset *formats;
    pa_format_info *f;

    pa_assert(s);

    formats = pa_idxset_new(NULL, NULL);

    f = pa_format_info_new();
    f->encoding = PA_ENCODING_PCM;
    pa_idxset_put(formats, f, NULL);

    u = (struct userdata *) s->userdata;

    if (u->profile == PROFILE_A2DP && u->a2dp.has_mpeg) {
        f = pa_format_info_new();
        f->encoding = PA_ENCODING_MPEG_IEC61937;
        /* FIXME: Populate supported rates, layers, ... */
        pa_idxset_put(formats, f, NULL);
    }

    return formats;
}

/* Run from main thread */
static int add_sink(struct userdata *u) {
    char *k;

    if (USE_SCO_OVER_PCM(u)) {
        pa_proplist *p;

        u->sink = u->hsp.sco_sink;
        p = pa_proplist_new();
        pa_proplist_sets(p, "bluetooth.protocol", "sco");
        pa_proplist_update(u->sink->proplist, PA_UPDATE_MERGE, p);
        pa_proplist_free(p);

        if (!u->hsp.sink_state_changed_slot)
            u->hsp.sink_state_changed_slot = pa_hook_connect(&u->core->hooks[PA_CORE_HOOK_SINK_STATE_CHANGED], PA_HOOK_NORMAL, (pa_hook_cb_t) sink_state_changed_cb, u);

    } else {
        pa_sink_new_data data;
        pa_bool_t b;

        pa_sink_new_data_init(&data);
        data.driver = __FILE__;
        data.module = u->module;
        pa_sink_new_data_set_sample_spec(&data, &u->sample_spec);
        pa_proplist_sets(data.proplist, "bluetooth.protocol", u->profile == PROFILE_A2DP ? "a2dp" : u->profile == PROFILE_HFGW ? "hfgw" : "sco");
        if (u->profile == PROFILE_HSP)
            pa_proplist_sets(data.proplist, PA_PROP_DEVICE_INTENDED_ROLES, "phone");
        data.card = u->card;
        data.name = get_name("sink", u->modargs, u->address, &b);
        data.namereg_fail = b;

        if (pa_modargs_get_proplist(u->modargs, "sink_properties", data.proplist, PA_UPDATE_REPLACE) < 0) {
            pa_log("Invalid properties");
            pa_sink_new_data_done(&data);
            return -1;
        }

        u->sink = pa_sink_new(u->core, &data, PA_SINK_HARDWARE|PA_SINK_LATENCY);
        pa_sink_new_data_done(&data);

        if (!u->sink) {
            pa_log_error("Failed to create sink");
            return -1;
        }

        u->sink->userdata = u;
        u->sink->parent.process_msg = sink_process_msg;
        u->sink->get_formats = sink_get_formats;

        pa_sink_set_max_request(u->sink, u->block_size);
        pa_sink_set_fixed_latency(u->sink,
                                  ((u->profile == PROFILE_A2DP) ?
                                   FIXED_LATENCY_PLAYBACK_A2DP :
                                   FIXED_LATENCY_PLAYBACK_HSP) + pa_bytes_to_usec(u->block_size, &u->sample_spec));
    }

    if (u->profile == PROFILE_HSP) {
        pa_sink_set_set_volume_callback(u->sink, sink_set_volume_cb);
        u->sink->n_volume_steps = 16;

        k = pa_sprintf_malloc("bluetooth-device@%p", (void*) u->sink);
        pa_shared_set(u->core, k, u);
        pa_xfree(k);
    }

    return 0;
}

/* Run from main thread */
static int add_source(struct userdata *u) {
    char *k;

    if (USE_SCO_OVER_PCM(u)) {
        u->source = u->hsp.sco_source;
        pa_proplist_sets(u->source->proplist, "bluetooth.protocol", "hsp");

        if (!u->hsp.source_state_changed_slot)
            u->hsp.source_state_changed_slot = pa_hook_connect(&u->core->hooks[PA_CORE_HOOK_SOURCE_STATE_CHANGED], PA_HOOK_NORMAL, (pa_hook_cb_t) source_state_changed_cb, u);

    } else {
        pa_source_new_data data;
        pa_bool_t b;

        pa_source_new_data_init(&data);
        data.driver = __FILE__;
        data.module = u->module;
        pa_source_new_data_set_sample_spec(&data, &u->sample_spec);
        pa_proplist_sets(data.proplist, "bluetooth.protocol", u->profile == PROFILE_A2DP_SOURCE ? "a2dp_source" : u->profile == PROFILE_HFGW ? "hfgw" : "hsp");
        if ((u->profile == PROFILE_HSP) || (u->profile == PROFILE_HFGW))
            pa_proplist_sets(data.proplist, PA_PROP_DEVICE_INTENDED_ROLES, "phone");

        data.card = u->card;
        data.name = get_name("source", u->modargs, u->address, &b);
        data.namereg_fail = b;

        if (pa_modargs_get_proplist(u->modargs, "source_properties", data.proplist, PA_UPDATE_REPLACE) < 0) {
            pa_log("Invalid properties");
            pa_source_new_data_done(&data);
            return -1;
        }

        u->source = pa_source_new(u->core, &data, PA_SOURCE_HARDWARE|PA_SOURCE_LATENCY);
        pa_source_new_data_done(&data);

        if (!u->source) {
            pa_log_error("Failed to create source");
            return -1;
        }

        u->source->userdata = u;
        u->source->parent.process_msg = source_process_msg;
        u->source->get_formats = source_get_formats;

        pa_source_set_fixed_latency(u->source,
                                    (u->profile == PROFILE_A2DP_SOURCE ? FIXED_LATENCY_RECORD_A2DP : FIXED_LATENCY_RECORD_HSP) +
                                    pa_bytes_to_usec(u->block_size, &u->sample_spec));
    }

    if ((u->profile == PROFILE_HSP) || (u->profile == PROFILE_HFGW)) {
        if (u->transport) {
            const pa_bluetooth_transport *t;
            t = pa_bluetooth_discovery_get_transport(u->discovery, u->transport);
            pa_assert(t);
            pa_proplist_sets(u->source->proplist, "bluetooth.nrec", t->nrec ? "1" : "0");
        } else
            pa_proplist_sets(u->source->proplist, "bluetooth.nrec", (u->hsp.pcm_capabilities.flags & BT_PCM_FLAG_NREC) ? "1" : "0");
    }

    if (u->profile == PROFILE_HSP) {
        pa_source_set_set_volume_callback(u->source, source_set_volume_cb);
        u->source->n_volume_steps = 16;

        k = pa_sprintf_malloc("bluetooth-device@%p", (void*) u->source);
        pa_shared_set(u->core, k, u);
        pa_xfree(k);
    }

    return 0;
}

/* Run from main thread */
static void shutdown_bt(struct userdata *u) {
    pa_assert(u);

    if (u->stream_fd >= 0) {
        pa_close(u->stream_fd);
        u->stream_fd = -1;

        u->stream_write_type = 0;
    }

    if (u->service_fd >= 0) {
        pa_close(u->service_fd);
        u->service_fd = -1;
        u->service_write_type = 0;
        u->service_read_type = 0;
    }

    if (u->write_memchunk.memblock) {
        pa_memblock_unref(u->write_memchunk.memblock);
        pa_memchunk_reset(&u->write_memchunk);
    }
}

static int bt_transport_config_a2dp_sbc(struct userdata *u) {
    const pa_bluetooth_transport *t;
    struct a2dp_info *a2dp = &u->a2dp;
    a2dp_sbc_t *config;

    t = pa_bluetooth_discovery_get_transport(u->discovery, u->transport);
    pa_assert(t);

    config = (a2dp_sbc_t *) t->config;

    u->sample_spec.format = PA_SAMPLE_S16LE;

    if (a2dp->sbc_initialized)
        sbc_reinit(&a2dp->sbc, 0);
    else
        sbc_init(&a2dp->sbc, 0);
    a2dp->sbc_initialized = TRUE;

    switch (config->frequency) {
        case BT_SBC_SAMPLING_FREQ_16000:
            a2dp->sbc.frequency = SBC_FREQ_16000;
            u->sample_spec.rate = 16000U;
            break;
        case BT_SBC_SAMPLING_FREQ_32000:
            a2dp->sbc.frequency = SBC_FREQ_32000;
            u->sample_spec.rate = 32000U;
            break;
        case BT_SBC_SAMPLING_FREQ_44100:
            a2dp->sbc.frequency = SBC_FREQ_44100;
            u->sample_spec.rate = 44100U;
            break;
        case BT_SBC_SAMPLING_FREQ_48000:
            a2dp->sbc.frequency = SBC_FREQ_48000;
            u->sample_spec.rate = 48000U;
            break;
        default:
            pa_assert_not_reached();
    }

    switch (config->channel_mode) {
        case BT_A2DP_CHANNEL_MODE_MONO:
            a2dp->sbc.mode = SBC_MODE_MONO;
            u->sample_spec.channels = 1;
            break;
        case BT_A2DP_CHANNEL_MODE_DUAL_CHANNEL:
            a2dp->sbc.mode = SBC_MODE_DUAL_CHANNEL;
            u->sample_spec.channels = 2;
            break;
        case BT_A2DP_CHANNEL_MODE_STEREO:
            a2dp->sbc.mode = SBC_MODE_STEREO;
            u->sample_spec.channels = 2;
            break;
        case BT_A2DP_CHANNEL_MODE_JOINT_STEREO:
            a2dp->sbc.mode = SBC_MODE_JOINT_STEREO;
            u->sample_spec.channels = 2;
            break;
        default:
            pa_assert_not_reached();
    }

    switch (config->allocation_method) {
        case BT_A2DP_ALLOCATION_SNR:
            a2dp->sbc.allocation = SBC_AM_SNR;
            break;
        case BT_A2DP_ALLOCATION_LOUDNESS:
            a2dp->sbc.allocation = SBC_AM_LOUDNESS;
            break;
        default:
            pa_assert_not_reached();
    }

    switch (config->subbands) {
        case BT_A2DP_SUBBANDS_4:
            a2dp->sbc.subbands = SBC_SB_4;
            break;
        case BT_A2DP_SUBBANDS_8:
            a2dp->sbc.subbands = SBC_SB_8;
            break;
        default:
            pa_assert_not_reached();
    }

    switch (config->block_length) {
        case BT_A2DP_BLOCK_LENGTH_4:
            a2dp->sbc.blocks = SBC_BLK_4;
            break;
        case BT_A2DP_BLOCK_LENGTH_8:
            a2dp->sbc.blocks = SBC_BLK_8;
            break;
        case BT_A2DP_BLOCK_LENGTH_12:
            a2dp->sbc.blocks = SBC_BLK_12;
            break;
        case BT_A2DP_BLOCK_LENGTH_16:
            a2dp->sbc.blocks = SBC_BLK_16;
            break;
        default:
            pa_assert_not_reached();
    }

    a2dp->min_bitpool = config->min_bitpool;
    a2dp->max_bitpool = config->max_bitpool;

    /* Set minimum bitpool for source to get the maximum possible block_size */
    a2dp->sbc.bitpool = u->profile == PROFILE_A2DP ? a2dp->max_bitpool : a2dp->min_bitpool;
    a2dp->codesize = sbc_get_codesize(&a2dp->sbc);
    a2dp->frame_length = sbc_get_frame_length(&a2dp->sbc);

    u->block_size =
        ((u->link_mtu - sizeof(struct rtp_header) - sizeof(struct sbc_rtp_payload))
         / a2dp->frame_length
         * a2dp->codesize);

    pa_log_info("SBC parameters:\n\tallocation=%u\n\tsubbands=%u\n\tblocks=%u\n\tbitpool=%u\n",
                a2dp->sbc.allocation, a2dp->sbc.subbands, a2dp->sbc.blocks, a2dp->sbc.bitpool);

    return 0;
}

static int bt_transport_config_a2dp_mpeg(struct userdata *u) {
    const pa_bluetooth_transport *t;
    a2dp_mpeg_t *config;

    t = pa_bluetooth_discovery_get_transport(u->discovery, u->transport);
    pa_assert(t);

    config = (a2dp_mpeg_t *) t->config;

    u->sample_spec.format = PA_SAMPLE_S16LE;

    switch (config->frequency) {
        case BT_MPEG_SAMPLING_FREQ_16000:
            u->sample_spec.rate = 16000U;
            break;
        case BT_MPEG_SAMPLING_FREQ_22050:
            u->sample_spec.rate = 22050U;
            break;
        case BT_MPEG_SAMPLING_FREQ_24000:
            u->sample_spec.rate = 24000U;
            break;
        case BT_MPEG_SAMPLING_FREQ_32000:
            u->sample_spec.rate = 32000U;
            break;
        case BT_MPEG_SAMPLING_FREQ_44100:
            u->sample_spec.rate = 44100U;
            break;
        case BT_MPEG_SAMPLING_FREQ_48000:
            u->sample_spec.rate = 48000U;
            break;
        default:
            pa_assert_not_reached();
    }

    switch (config->channel_mode) {
        case BT_A2DP_CHANNEL_MODE_MONO:
            u->sample_spec.channels = 1;
            break;
        case BT_A2DP_CHANNEL_MODE_DUAL_CHANNEL:
            u->sample_spec.channels = 2;
            break;
        case BT_A2DP_CHANNEL_MODE_STEREO:
            u->sample_spec.channels = 2;
            break;
        case BT_A2DP_CHANNEL_MODE_JOINT_STEREO:
            u->sample_spec.channels = 2;
            break;
        default:
            pa_assert_not_reached();
    }

    u->block_size = 1152*4; /* Maximum size of an IEC frame */
    u->leftover_bytes = 0;

    pa_log_info("MPEG selected\n");

    return 0;
}

static int bt_transport_config(struct userdata *u) {
    if (u->profile == PROFILE_HSP || u->profile == PROFILE_HFGW) {
        u->block_size = u->link_mtu;
        u->sample_spec.format = PA_SAMPLE_S16LE;
        u->sample_spec.channels = 1;
        u->sample_spec.rate = 8000;
        return 0;
    }

    if (u->leftover_bytes) {
        pa_log_debug("SBC parameters: %d bytes forgotten", u->leftover_bytes);
        u->leftover_bytes = 0;
    }
    if(u->write_memchunk.memblock) {
        pa_log_debug("SBC parameters: memblock forgotten");
        pa_memblock_unref(u->write_memchunk.memblock);
        pa_memchunk_reset(&u->write_memchunk);
    }

    if (u->a2dp.mode == A2DP_MODE_MPEG)
        return bt_transport_config_a2dp_mpeg(u);

    return bt_transport_config_a2dp_sbc(u);
}

/* Run from main thread */
static int bt_transport_open(struct userdata *u) {
    if (bt_transport_acquire(u, FALSE) < 0)
        return -1;

    return bt_transport_config(u);
}

/* Run from main thread */
static int init_bt(struct userdata *u) {
    pa_assert(u);

    shutdown_bt(u);

    u->stream_write_type = 0;
    u->service_write_type = 0;
    u->service_read_type = 0;

    if ((u->service_fd = bt_audio_service_open()) < 0) {
        pa_log_warn("Bluetooth audio service not available");
        return -1;
    }

    pa_log_debug("Connected to the bluetooth audio service");

    return 0;
}

/* Run from main thread */
static int setup_bt(struct userdata *u) {
    const pa_bluetooth_device *d;
    const pa_bluetooth_transport *t;

    pa_assert(u);

    if (!(d = pa_bluetooth_discovery_get_by_path(u->discovery, u->path))) {
        pa_log_error("Failed to get device object.");
        return -1;
    }

    /* release transport if exist */
    if (u->transport) {
        bt_transport_release(u);
        pa_xfree(u->transport);
        u->transport = NULL;
    }

    /* check if profile has a transport */
    t = pa_bluetooth_device_get_transport(d, u->profile);
    if (t) {
        u->transport = pa_xstrdup(t->path);
        u->a2dp.has_mpeg = t->has_mpeg;
        /* Connect for SBC to start with, switch later if required */
        u->a2dp.mode = A2DP_MODE_SBC;
        return bt_transport_open(u);
    }

    if (get_caps(u, 0) < 0)
        return -1;

    pa_log_debug("Got device capabilities");

    if (u->profile == PROFILE_A2DP) {
        /* Connect for SBC to start with, switch later if required */
        u->a2dp.mode = A2DP_MODE_SBC;
    }

    if (set_conf(u) < 0)
        return -1;

    pa_log_debug("Connection to the device configured");

    if (USE_SCO_OVER_PCM(u)) {
        pa_log_debug("Configured to use SCO over PCM");
        return 0;
    }

    pa_log_debug("Got the stream socket");

    return 0;
}

/* Run from main thread */
static int init_profile(struct userdata *u) {
    int r = 0;
    pa_assert(u);
    pa_assert(u->profile != PROFILE_OFF);

    if (setup_bt(u) < 0)
        return -1;

    if (u->profile == PROFILE_A2DP ||
        u->profile == PROFILE_HSP ||
        u->profile == PROFILE_HFGW)
        if (add_sink(u) < 0)
            r = -1;

    if (u->profile == PROFILE_HSP ||
        u->profile == PROFILE_A2DP_SOURCE ||
        u->profile == PROFILE_HFGW)
        if (add_source(u) < 0)
            r = -1;

    return r;
}

/* Run from main thread */
static void stop_thread(struct userdata *u) {
    char *k;

    pa_assert(u);

    if (u->thread) {
        pa_asyncmsgq_send(u->thread_mq.inq, NULL, PA_MESSAGE_SHUTDOWN, NULL, 0, NULL);
        pa_thread_free(u->thread);
        u->thread = NULL;
    }

    if (u->rtpoll_item) {
        pa_rtpoll_item_free(u->rtpoll_item);
        u->rtpoll_item = NULL;
    }

    if (u->hsp.sink_state_changed_slot) {
        pa_hook_slot_free(u->hsp.sink_state_changed_slot);
        u->hsp.sink_state_changed_slot = NULL;
    }

    if (u->hsp.source_state_changed_slot) {
        pa_hook_slot_free(u->hsp.source_state_changed_slot);
        u->hsp.source_state_changed_slot = NULL;
    }

    if (u->sink) {
        if (u->profile == PROFILE_HSP) {
            k = pa_sprintf_malloc("bluetooth-device@%p", (void*) u->sink);
            pa_shared_remove(u->core, k);
            pa_xfree(k);
        }

        pa_sink_unref(u->sink);
        u->sink = NULL;
    }

    if (u->source) {
        if (u->profile == PROFILE_HSP) {
            k = pa_sprintf_malloc("bluetooth-device@%p", (void*) u->source);
            pa_shared_remove(u->core, k);
            pa_xfree(k);
        }

        pa_source_unref(u->source);
        u->source = NULL;
    }

    if (u->rtpoll) {
        pa_thread_mq_done(&u->thread_mq);

        pa_rtpoll_free(u->rtpoll);
        u->rtpoll = NULL;
    }

    if (u->read_smoother) {
        pa_smoother_free(u->read_smoother);
        u->read_smoother = NULL;
    }
}

/* Run from main thread */
static int start_thread(struct userdata *u) {
    pa_assert(u);
    pa_assert(!u->thread);
    pa_assert(!u->rtpoll);
    pa_assert(!u->rtpoll_item);

    u->rtpoll = pa_rtpoll_new();
    pa_thread_mq_init(&u->thread_mq, u->core->mainloop, u->rtpoll);

    if (USE_SCO_OVER_PCM(u)) {
        if (sco_over_pcm_state_update(u, FALSE) < 0) {
            char *k;

            if (u->sink) {
                k = pa_sprintf_malloc("bluetooth-device@%p", (void*) u->sink);
                pa_shared_remove(u->core, k);
                pa_xfree(k);
                u->sink = NULL;
            }
            if (u->source) {
                k = pa_sprintf_malloc("bluetooth-device@%p", (void*) u->source);
                pa_shared_remove(u->core, k);
                pa_xfree(k);
                u->source = NULL;
            }
            return -1;
        }

        pa_sink_ref(u->sink);
        pa_source_ref(u->source);
        /* FIXME: monitor stream_fd error */
        return 0;
    }

    if (!(u->thread = pa_thread_new("bluetooth", thread_func, u))) {
        pa_log_error("Failed to create IO thread");
        stop_thread(u);
        return -1;
    }

    if (u->sink) {
        pa_sink_set_asyncmsgq(u->sink, u->thread_mq.inq);
        pa_sink_set_rtpoll(u->sink, u->rtpoll);
        pa_sink_put(u->sink);

        if (u->sink->set_volume)
            u->sink->set_volume(u->sink);
    }

    if (u->source) {
        pa_source_set_asyncmsgq(u->source, u->thread_mq.inq);
        pa_source_set_rtpoll(u->source, u->rtpoll);
        pa_source_put(u->source);

        if (u->source->set_volume)
            u->source->set_volume(u->source);
    }

    return 0;
}

static void save_sco_volume_callbacks(struct userdata *u) {
    pa_assert(u);
    pa_assert(USE_SCO_OVER_PCM(u));

    u->hsp.sco_sink_set_volume = u->hsp.sco_sink->set_volume;
    u->hsp.sco_source_set_volume = u->hsp.sco_source->set_volume;
}

static void restore_sco_volume_callbacks(struct userdata *u) {
    pa_assert(u);
    pa_assert(USE_SCO_OVER_PCM(u));

    pa_sink_set_set_volume_callback(u->hsp.sco_sink, u->hsp.sco_sink_set_volume);
    pa_source_set_set_volume_callback(u->hsp.sco_source, u->hsp.sco_source_set_volume);
}

/* Run from main thread */
static int card_set_profile(pa_card *c, pa_card_profile *new_profile) {
    struct userdata *u;
    enum profile *d;
    pa_queue *inputs = NULL, *outputs = NULL;
    const pa_bluetooth_device *device;

    pa_assert(c);
    pa_assert(new_profile);
    pa_assert_se(u = c->userdata);

    d = PA_CARD_PROFILE_DATA(new_profile);

    if (!(device = pa_bluetooth_discovery_get_by_path(u->discovery, u->path))) {
        pa_log_error("Failed to get device object.");
        return -PA_ERR_IO;
    }

    /* The state signal is sent by bluez, so it is racy to check
       strictly for CONNECTED, we should also accept STREAMING state
       as being good enough. However, if the profile is used
       concurrently (which is unlikely), ipc will fail later on, and
       module will be unloaded. */
    if (device->headset_state < PA_BT_AUDIO_STATE_CONNECTED && *d == PROFILE_HSP) {
        pa_log_warn("HSP is not connected, refused to switch profile");
        return -PA_ERR_IO;
    }
    else if (device->audio_sink_state < PA_BT_AUDIO_STATE_CONNECTED && *d == PROFILE_A2DP) {
        pa_log_warn("A2DP is not connected, refused to switch profile");
        return -PA_ERR_IO;
    }
    else if (device->hfgw_state < PA_BT_AUDIO_STATE_CONNECTED && *d == PROFILE_HFGW) {
        pa_log_warn("HandsfreeGateway is not connected, refused to switch profile");
        return -PA_ERR_IO;
    }

    if (u->sink) {
        inputs = pa_sink_move_all_start(u->sink, NULL);

        if (!USE_SCO_OVER_PCM(u))
            pa_sink_unlink(u->sink);
    }

    if (u->source) {
        outputs = pa_source_move_all_start(u->source, NULL);

        if (!USE_SCO_OVER_PCM(u))
            pa_source_unlink(u->source);
    }

    stop_thread(u);

    if (u->profile != PROFILE_OFF && u->transport) {
        bt_transport_release(u);
        pa_xfree(u->transport);
        u->transport = NULL;
    }

    shutdown_bt(u);

    if (USE_SCO_OVER_PCM(u))
        restore_sco_volume_callbacks(u);

    u->profile = *d;
    u->sample_spec = u->requested_sample_spec;

    if (USE_SCO_OVER_PCM(u))
        save_sco_volume_callbacks(u);

    init_bt(u);

    if (u->profile != PROFILE_OFF)
        init_profile(u);

    if (u->sink || u->source)
        start_thread(u);

    if (inputs) {
        if (u->sink)
            pa_sink_move_all_finish(u->sink, inputs, FALSE);
        else
            pa_sink_move_all_fail(inputs);
    }

    if (outputs) {
        if (u->source)
            pa_source_move_all_finish(u->source, outputs, FALSE);
        else
            pa_source_move_all_fail(outputs);
    }

    return 0;
}

/* Run from main thread */
static int add_card(struct userdata *u, const pa_bluetooth_device *device) {
    pa_card_new_data data;
    pa_bool_t b;
    pa_card_profile *p;
    enum profile *d;
    const char *ff;
    char *n;
    const char *default_profile;

    pa_assert(u);
    pa_assert(device);

    pa_card_new_data_init(&data);
    data.driver = __FILE__;
    data.module = u->module;

    n = pa_bluetooth_cleanup_name(device->name);
    pa_proplist_sets(data.proplist, PA_PROP_DEVICE_DESCRIPTION, n);
    pa_xfree(n);
    pa_proplist_sets(data.proplist, PA_PROP_DEVICE_STRING, device->address);
    pa_proplist_sets(data.proplist, PA_PROP_DEVICE_API, "bluez");
    pa_proplist_sets(data.proplist, PA_PROP_DEVICE_CLASS, "sound");
    pa_proplist_sets(data.proplist, PA_PROP_DEVICE_BUS, "bluetooth");
    if ((ff = pa_bluetooth_get_form_factor(device->class)))
        pa_proplist_sets(data.proplist, PA_PROP_DEVICE_FORM_FACTOR, ff);
    pa_proplist_sets(data.proplist, "bluez.path", device->path);
    pa_proplist_setf(data.proplist, "bluez.class", "0x%06x", (unsigned) device->class);
    pa_proplist_sets(data.proplist, "bluez.name", device->name);
    data.name = get_name("card", u->modargs, device->address, &b);
    data.namereg_fail = b;

    if (pa_modargs_get_proplist(u->modargs, "card_properties", data.proplist, PA_UPDATE_REPLACE) < 0) {
        pa_log("Invalid properties");
        pa_card_new_data_done(&data);
        return -1;
    }

    data.profiles = pa_hashmap_new(pa_idxset_string_hash_func, pa_idxset_string_compare_func);

    /* we base hsp/a2dp availability on UUIDs.
       Ideally, it would be based on "Connected" state, but
       we can't afford to wait for this information when
       we are loaded with profile="hsp", for instance */
    if (pa_bluetooth_uuid_has(device->uuids, A2DP_SINK_UUID)) {
        p = pa_card_profile_new("a2dp", _("High Fidelity Playback (A2DP)"), sizeof(enum profile));
        p->priority = 10;
        p->n_sinks = 1;
        p->n_sources = 0;
        p->max_sink_channels = 2;
        p->max_source_channels = 0;

        d = PA_CARD_PROFILE_DATA(p);
        *d = PROFILE_A2DP;

        pa_hashmap_put(data.profiles, p->name, p);

    }

    if (pa_bluetooth_uuid_has(device->uuids, A2DP_SOURCE_UUID)) {
        p = pa_card_profile_new("a2dp_source", _("High Fidelity Capture (A2DP)"), sizeof(enum profile));
        p->priority = 10;
        p->n_sinks = 0;
        p->n_sources = 1;
        p->max_sink_channels = 0;
        p->max_source_channels = 2;

        d = PA_CARD_PROFILE_DATA(p);
        *d = PROFILE_A2DP_SOURCE;

        pa_hashmap_put(data.profiles, p->name, p);
    }

    if (pa_bluetooth_uuid_has(device->uuids, HSP_HS_UUID) ||
        pa_bluetooth_uuid_has(device->uuids, HFP_HS_UUID)) {
        p = pa_card_profile_new("hsp", _("Telephony Duplex (HSP/HFP)"), sizeof(enum profile));
        p->priority = 20;
        p->n_sinks = 1;
        p->n_sources = 1;
        p->max_sink_channels = 1;
        p->max_source_channels = 1;

        d = PA_CARD_PROFILE_DATA(p);
        *d = PROFILE_HSP;

        pa_hashmap_put(data.profiles, p->name, p);
    }

    if (pa_bluetooth_uuid_has(device->uuids, HFP_AG_UUID)) {
        p = pa_card_profile_new("hfgw", _("Handsfree Gateway"), sizeof(enum profile));
        p->priority = 20;
        p->n_sinks = 1;
        p->n_sources = 1;
        p->max_sink_channels = 1;
        p->max_source_channels = 1;

        d = PA_CARD_PROFILE_DATA(p);
        *d = PROFILE_HFGW;

        pa_hashmap_put(data.profiles, p->name, p);
    }

    pa_assert(!pa_hashmap_isempty(data.profiles));

    p = pa_card_profile_new("off", _("Off"), sizeof(enum profile));
    d = PA_CARD_PROFILE_DATA(p);
    *d = PROFILE_OFF;
    pa_hashmap_put(data.profiles, p->name, p);

    if ((default_profile = pa_modargs_get_value(u->modargs, "profile", NULL))) {
        if (pa_hashmap_get(data.profiles, default_profile))
            pa_card_new_data_set_profile(&data, default_profile);
        else
            pa_log_warn("Profile '%s' not valid or not supported by device.", default_profile);
    }

    u->card = pa_card_new(u->core, &data);
    pa_card_new_data_done(&data);

    if (!u->card) {
        pa_log("Failed to allocate card.");
        return -1;
    }

    u->card->userdata = u;
    u->card->set_profile = card_set_profile;

    d = PA_CARD_PROFILE_DATA(u->card->active_profile);

    if ((device->headset_state < PA_BT_AUDIO_STATE_CONNECTED && *d == PROFILE_HSP) ||
        (device->audio_sink_state < PA_BT_AUDIO_STATE_CONNECTED && *d == PROFILE_A2DP) ||
        (device->hfgw_state < PA_BT_AUDIO_STATE_CONNECTED && *d == PROFILE_HFGW)) {
        pa_log_warn("Default profile not connected, selecting off profile");
        u->card->active_profile = pa_hashmap_get(u->card->profiles, "off");
        u->card->save_profile = FALSE;
    }

    d = PA_CARD_PROFILE_DATA(u->card->active_profile);
    u->profile = *d;

    if (USE_SCO_OVER_PCM(u))
        save_sco_volume_callbacks(u);

    return 0;
}

/* Run from main thread */
static const pa_bluetooth_device* find_device(struct userdata *u, const char *address, const char *path) {
    const pa_bluetooth_device *d = NULL;

    pa_assert(u);

    if (!address && !path) {
        pa_log_error("Failed to get device address/path from module arguments.");
        return NULL;
    }

    if (path) {
        if (!(d = pa_bluetooth_discovery_get_by_path(u->discovery, path))) {
            pa_log_error("%s is not a valid BlueZ audio device.", path);
            return NULL;
        }

        if (address && !(pa_streq(d->address, address))) {
            pa_log_error("Passed path %s address %s != %s don't match.", path, d->address, address);
            return NULL;
        }

    } else {
        if (!(d = pa_bluetooth_discovery_get_by_address(u->discovery, address))) {
            pa_log_error("%s is not known.", address);
            return NULL;
        }
    }

    if (d) {
        u->address = pa_xstrdup(d->address);
        u->path = pa_xstrdup(d->path);
    }

    return d;
}

/* Run from main thread */
static int setup_dbus(struct userdata *u) {
    DBusError err;

    dbus_error_init(&err);

    u->connection = pa_dbus_bus_get(u->core, DBUS_BUS_SYSTEM, &err);

    if (dbus_error_is_set(&err) || !u->connection) {
        pa_log("Failed to get D-Bus connection: %s", err.message);
        dbus_error_free(&err);
        return -1;
    }

    return 0;
}

int pa__init(pa_module* m) {
    pa_modargs *ma;
    uint32_t channels;
    struct userdata *u;
    const char *address, *path;
    DBusError err;
    char *mike, *speaker;
    const pa_bluetooth_device *device;

    pa_assert(m);

    dbus_error_init(&err);

    if (!(ma = pa_modargs_new(m->argument, valid_modargs))) {
        pa_log_error("Failed to parse module arguments");
        goto fail;
    }

    m->userdata = u = pa_xnew0(struct userdata, 1);
    u->module = m;
    u->core = m->core;
    u->service_fd = -1;
    u->stream_fd = -1;
    u->sample_spec = m->core->default_sample_spec;
    u->modargs = ma;

    if (pa_modargs_get_value(ma, "sco_sink", NULL) &&
        !(u->hsp.sco_sink = pa_namereg_get(m->core, pa_modargs_get_value(ma, "sco_sink", NULL), PA_NAMEREG_SINK))) {
        pa_log("SCO sink not found");
        goto fail;
    }

    if (pa_modargs_get_value(ma, "sco_source", NULL) &&
        !(u->hsp.sco_source = pa_namereg_get(m->core, pa_modargs_get_value(ma, "sco_source", NULL), PA_NAMEREG_SOURCE))) {
        pa_log("SCO source not found");
        goto fail;
    }

    if (pa_modargs_get_value_u32(ma, "rate", &u->sample_spec.rate) < 0 ||
        u->sample_spec.rate <= 0 || u->sample_spec.rate > PA_RATE_MAX) {
        pa_log_error("Failed to get rate from module arguments");
        goto fail;
    }

    u->auto_connect = TRUE;
    if (pa_modargs_get_value_boolean(ma, "auto_connect", &u->auto_connect)) {
        pa_log("Failed to parse auto_connect= argument");
        goto fail;
    }

    channels = u->sample_spec.channels;
    if (pa_modargs_get_value_u32(ma, "channels", &channels) < 0 ||
        channels <= 0 || channels > PA_CHANNELS_MAX) {
        pa_log_error("Failed to get channels from module arguments");
        goto fail;
    }
    u->sample_spec.channels = (uint8_t) channels;
    u->requested_sample_spec = u->sample_spec;

    address = pa_modargs_get_value(ma, "address", NULL);
    path = pa_modargs_get_value(ma, "path", NULL);

    if (setup_dbus(u) < 0)
        goto fail;

    if (!(u->discovery = pa_bluetooth_discovery_get(m->core)))
        goto fail;

    if (!(device = find_device(u, address, path)))
        goto fail;

    /* Add the card structure. This will also initialize the default profile */
    if (add_card(u, device) < 0)
        goto fail;

    if (!(u->msg = pa_msgobject_new(bluetooth_msg)))
        goto fail;

    u->msg->parent.process_msg = device_process_msg;
    u->msg->card = u->card;

    if (!dbus_connection_add_filter(pa_dbus_connection_get(u->connection), filter_cb, u, NULL)) {
        pa_log_error("Failed to add filter function");
        goto fail;
    }
    u->filter_added = TRUE;

    speaker = pa_sprintf_malloc("type='signal',sender='org.bluez',interface='org.bluez.Headset',member='SpeakerGainChanged',path='%s'", u->path);
    mike = pa_sprintf_malloc("type='signal',sender='org.bluez',interface='org.bluez.Headset',member='MicrophoneGainChanged',path='%s'", u->path);

    if (pa_dbus_add_matches(
                pa_dbus_connection_get(u->connection), &err,
                speaker,
                mike,
                "type='signal',sender='org.bluez',interface='org.bluez.MediaTransport',member='PropertyChanged'",
                "type='signal',sender='org.bluez',interface='org.bluez.HandsfreeGateway',member='PropertyChanged'",
                NULL) < 0) {

        pa_xfree(speaker);
        pa_xfree(mike);

        pa_log("Failed to add D-Bus matches: %s", err.message);
        goto fail;
    }

    pa_xfree(speaker);
    pa_xfree(mike);

    /* Connect to the BT service */
    init_bt(u);

    if (u->profile != PROFILE_OFF)
        if (init_profile(u) < 0)
            goto fail;

    if (u->sink || u->source)
        if (start_thread(u) < 0)
            goto fail;

    return 0;

fail:

    pa__done(m);

    dbus_error_free(&err);

    return -1;
}

int pa__get_n_used(pa_module *m) {
    struct userdata *u;

    pa_assert(m);
    pa_assert_se(u = m->userdata);

    return
        (u->sink ? pa_sink_linked_by(u->sink) : 0) +
        (u->source ? pa_source_linked_by(u->source) : 0);
}

void pa__done(pa_module *m) {
    struct userdata *u;

    pa_assert(m);

    if (!(u = m->userdata))
        return;

    if (u->sink && !USE_SCO_OVER_PCM(u))
        pa_sink_unlink(u->sink);

    if (u->source && !USE_SCO_OVER_PCM(u))
        pa_source_unlink(u->source);

    stop_thread(u);

    if (USE_SCO_OVER_PCM(u))
        restore_sco_volume_callbacks(u);

    if (u->connection) {

        if (u->path) {
            char *speaker, *mike;
            speaker = pa_sprintf_malloc("type='signal',sender='org.bluez',interface='org.bluez.Headset',member='SpeakerGainChanged',path='%s'", u->path);
            mike = pa_sprintf_malloc("type='signal',sender='org.bluez',interface='org.bluez.Headset',member='MicrophoneGainChanged',path='%s'", u->path);

            pa_dbus_remove_matches(pa_dbus_connection_get(u->connection), speaker, mike,
                "type='signal',sender='org.bluez',interface='org.bluez.MediaTransport',member='PropertyChanged'",
                "type='signal',sender='org.bluez',interface='org.bluez.HandsfreeGateway',member='PropertyChanged'",
                NULL);

            pa_xfree(speaker);
            pa_xfree(mike);
        }

        if (u->filter_added)
            dbus_connection_remove_filter(pa_dbus_connection_get(u->connection), filter_cb, u);

        pa_dbus_connection_unref(u->connection);
    }

    if (u->msg)
        pa_xfree(u->msg);

    if (u->card)
        pa_card_free(u->card);

    if (u->read_smoother)
        pa_smoother_free(u->read_smoother);

    shutdown_bt(u);

    if (u->a2dp.buffer)
        pa_xfree(u->a2dp.buffer);

    sbc_finish(&u->a2dp.sbc);

    if (u->modargs)
        pa_modargs_free(u->modargs);

    pa_xfree(u->address);
    pa_xfree(u->path);

    if (u->transport) {
        bt_transport_release(u);
        pa_xfree(u->transport);
    }

    if (u->discovery)
        pa_bluetooth_discovery_unref(u->discovery);

    pa_xfree(u);
}
