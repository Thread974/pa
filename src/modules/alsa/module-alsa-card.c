/***
  This file is part of PulseAudio.

  Copyright 2009 Lennart Poettering

  PulseAudio is free software; you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as published
  by the Free Software Foundation; either version 2.1 of the License,
  or (at your option) any later version.

  PulseAudio is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  General Public License for more details.

  You should have received a copy of the GNU Lesser General Public License
  along with PulseAudio; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
  USA.
***/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <pulse/xmalloc.h>

#include <pulsecore/core-util.h>
#include <pulsecore/i18n.h>
#include <pulsecore/modargs.h>
#include <pulsecore/queue.h>

#include <modules/reserve-wrap.h>

#ifdef HAVE_UDEV
#include <modules/udev-util.h>
#endif

#include "alsa-util.h"
#include "alsa-sink.h"
#include "alsa-source.h"
#include "module-alsa-card-symdef.h"

PA_MODULE_AUTHOR("Lennart Poettering");
PA_MODULE_DESCRIPTION("ALSA Card");
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
        "namereg_fail=<when false attempt to synthesise new names if they are already taken> "
        "device_id=<ALSA card index> "
        "format=<sample format> "
        "rate=<sample rate> "
        "fragments=<number of fragments> "
        "fragment_size=<fragment size> "
        "mmap=<enable memory mapping?> "
        "tsched=<enable system timer based scheduling mode?> "
        "tsched_buffer_size=<buffer size when using timer based scheduling> "
        "tsched_buffer_watermark=<lower fill watermark> "
        "profile=<profile name> "
        "ignore_dB=<ignore dB information from the device?> "
        "deferred_volume=<Synchronize software and hardware volume changes to avoid momentary jumps?> "
        "profile_set=<profile set configuration file> "
        "paths_dir=<directory containing the path configuration files> "
);

static const char* const valid_modargs[] = {
    "name",
    "card_name",
    "card_properties",
    "sink_name",
    "sink_properties",
    "source_name",
    "source_properties",
    "namereg_fail",
    "device_id",
    "format",
    "rate",
    "fragments",
    "fragment_size",
    "mmap",
    "tsched",
    "tsched_buffer_size",
    "tsched_buffer_watermark",
    "profile",
    "ignore_dB",
    "deferred_volume",
    "profile_set",
    "paths_dir",
    NULL
};

#define DEFAULT_DEVICE_ID "0"

struct userdata {
    pa_core *core;
    pa_module *module;

    char *device_id;

    pa_card *card;

    pa_modargs *modargs;

    pa_alsa_profile_set *profile_set;

    pa_alsa_ucm_config ucm;

    pa_hook_slot
        *jack_insert_new_hook_slot,
        *jack_remove_new_hook_slot;
};

struct profile_data {
    pa_alsa_profile *profile;
};

struct ucm_items {
    const char *id;
    const char *property;
};

struct ucm_info {
    const char *id;
    int priority;
    pa_alsa_direction_t direction;
    int channels;
};

static struct ucm_items item[] = {
    {"PlaybackPCM", PA_PROP_UCM_SINK},
    {"CapturePCM", PA_PROP_UCM_SOURCE},
    {"PlaybackVolume", PA_PROP_UCM_PLAYBACK_VOLUME},
    {"PlaybackSwitch", PA_PROP_UCM_PLAYBACK_SWITCH},
    {"CaptureVolume", PA_PROP_UCM_CAPTURE_VOLUME},
    {"CaptureSwitch", PA_PROP_UCM_CAPTURE_SWITCH},
    {"TQ", PA_PROP_UCM_QOS},
    {NULL, NULL},
};

/* UCM device info - this should eventually be part of policy manangement */
static struct ucm_info dev_info[] = {
    {SND_USE_CASE_DEV_SPEAKER, 100, PA_ALSA_DIRECTION_OUTPUT, 2},
    {SND_USE_CASE_DEV_LINE, 100, PA_ALSA_DIRECTION_ANY, 2},
    {SND_USE_CASE_DEV_HEADPHONES, 100, PA_ALSA_DIRECTION_OUTPUT, 2},
    {SND_USE_CASE_DEV_HEADSET, 300, PA_ALSA_DIRECTION_ANY, 2},
    {SND_USE_CASE_DEV_HANDSET, 200, PA_ALSA_DIRECTION_ANY, 2},
    {SND_USE_CASE_DEV_BLUETOOTH, 400, PA_ALSA_DIRECTION_ANY, 1},
    {SND_USE_CASE_DEV_EARPIECE, 100, PA_ALSA_DIRECTION_OUTPUT, 1},
    {SND_USE_CASE_DEV_SPDIF, 100, PA_ALSA_DIRECTION_ANY, 2},
    {SND_USE_CASE_DEV_HDMI, 100, PA_ALSA_DIRECTION_ANY, 8},
    {SND_USE_CASE_DEV_NONE, 100, PA_ALSA_DIRECTION_ANY, 2},
    {NULL, 0, PA_ALSA_DIRECTION_ANY, 0},
};

/* UCM profile properties - The verb data is store so it can be used to fill
 * the new profiles properties */

static int ucm_get_property(struct pa_alsa_ucm_verb *verb, snd_use_case_mgr_t *uc_mgr, const char *verb_name) {
    const char *value;
    int i = 0;

    do {
        int err;

        err = snd_use_case_get(uc_mgr, item[i].id, &value);
        if (err < 0 ) {
            pa_log_info("No %s for verb %s", item[i].id, verb_name);
            continue;
        }

        pa_log_info("Got %s for verb %s", item[i].id, verb_name);
        pa_proplist_sets(verb->proplist, item[i].property, value);
    }  while (item[++i].id);

    return 0;
};

/* Create a property list for this ucm device */
static int ucm_get_device_property(struct pa_alsa_ucm_device *device, snd_use_case_mgr_t *uc_mgr, const char *device_name) {
    const char *value;
    char *id;
    int i = 0;

    do {
        int err;

        id = pa_sprintf_malloc("%s/%s", item[i].id, device_name);

        err = snd_use_case_get(uc_mgr, id, &value);
        if (err < 0 ) {
            pa_log_info("No %s for device %s", id, device_name);
            pa_xfree(id);
            continue;
        }

        pa_log_info("Got %s for device %s", id, device_name);
        pa_xfree(id);
        pa_proplist_sets(device->proplist, item[i].property, value);
    }  while (item[++i].id);

    return 0;
};

/* Create a property list for this ucm modifier */
static int ucm_get_modifier_property(struct pa_alsa_ucm_modifier *modifier, snd_use_case_mgr_t *uc_mgr, const char *modifier_name) {
    const char *value;
    char *id;
    int i = 0;

    do {
        int err;

        id = pa_sprintf_malloc("%s/%s", item[i].id, modifier_name);

        err = snd_use_case_get(uc_mgr, id, &value);
        if (err < 0 ) {
            pa_log_info("No %s for modifier %s", id, modifier_name);
            pa_xfree(id);
            continue;
        }

        pa_log_info("Got %s for modifier %s", id, modifier_name);
        pa_xfree(id);
        pa_proplist_sets(modifier->proplist, item[i].property, value);
    }  while (item[++i].id);

    return 0;
};

/* Create a list of devices for this verb */
static int ucm_get_devices(struct pa_alsa_ucm_verb *verb, snd_use_case_mgr_t *uc_mgr) {
    const char **dev_list;
    int num_dev, i;

    num_dev = snd_use_case_get_list(uc_mgr, "_devices", &dev_list);
    if (num_dev < 0)
        return num_dev;

    for (i = 0; i < num_dev; i += 2) {
        pa_alsa_ucm_device *d;
        d = pa_xnew0(pa_alsa_ucm_device, 1);
        d->proplist = pa_proplist_new();
        pa_proplist_sets(d->proplist, PA_PROP_UCM_NAME, dev_list[i]);
        PA_LLIST_PREPEND(pa_alsa_ucm_device, verb->devices, d);
    }

    return 0;
};

static int ucm_get_modifiers(struct pa_alsa_ucm_verb *verb, snd_use_case_mgr_t *uc_mgr) {
    const char **mod_list;
    int num_mod, i;

    num_mod = snd_use_case_get_list(uc_mgr, "_modifiers", &mod_list);
    if (num_mod < 0)
        return num_mod;

    for (i = 0; i < num_mod; i += 2) {
        pa_alsa_ucm_modifier *m;
        m = pa_xnew0(pa_alsa_ucm_modifier, 1);
        m->proplist = pa_proplist_new();
        pa_proplist_sets(m->proplist, PA_PROP_UCM_NAME, mod_list[i]);
        PA_LLIST_PREPEND(pa_alsa_ucm_modifier, verb->modifiers, m);
    }

    return 0;
};

static int ucm_get_properties(struct pa_alsa_ucm_verb *verb, snd_use_case_mgr_t *uc_mgr, const char *verb_name) {
    struct pa_alsa_ucm_device *d;
    struct pa_alsa_ucm_modifier *mod;
    int err;

    err = snd_use_case_set(uc_mgr, "_verb", verb_name);
    if (err < 0)
        return err;

    err = ucm_get_devices(verb, uc_mgr);
    if (err < 0)
        pa_log("No UCM devices for verb %s", verb_name);

    err = ucm_get_modifiers(verb, uc_mgr);
    if (err < 0)
        pa_log("No UCM modifiers for verb %s", verb_name);

    /* Verb properties */
    ucm_get_property(verb, uc_mgr, verb_name);

    PA_LLIST_FOREACH(d, verb->devices) {
        const char *dev_name = pa_proplist_gets(d->proplist, PA_PROP_UCM_NAME);

        /* Devices properties */
        ucm_get_device_property(d, uc_mgr, dev_name);
    }

    PA_LLIST_FOREACH(mod, verb->modifiers) {
        const char *mod_name = pa_proplist_gets(mod->proplist, PA_PROP_UCM_NAME);

        /* Modifier properties */
        ucm_get_modifier_property(mod, uc_mgr, mod_name);
    }

    return 0;
}

static void add_profiles(struct userdata *u, pa_hashmap *h) {
    pa_alsa_profile *ap;
    void *state;

    pa_assert(u);
    pa_assert(h);

    PA_HASHMAP_FOREACH(ap, u->profile_set->profiles, state) {
        struct profile_data *d;
        pa_card_profile *cp;
        pa_alsa_mapping *m;
        uint32_t idx;

        cp = pa_card_profile_new(ap->name, ap->description, sizeof(struct profile_data));
        cp->priority = ap->priority;

        if (ap->output_mappings) {
            cp->n_sinks = pa_idxset_size(ap->output_mappings);

            PA_IDXSET_FOREACH(m, ap->output_mappings, idx)
                if (m->channel_map.channels > cp->max_sink_channels)
                    cp->max_sink_channels = m->channel_map.channels;
        }

        if (ap->input_mappings) {
            cp->n_sources = pa_idxset_size(ap->input_mappings);

            PA_IDXSET_FOREACH(m, ap->input_mappings, idx)
                if (m->channel_map.channels > cp->max_source_channels)
                    cp->max_source_channels = m->channel_map.channels;
        }

        d = PA_CARD_PROFILE_DATA(cp);
        d->profile = ap;

        pa_hashmap_put(h, cp->name, cp);
    }
}

static void add_disabled_profile(pa_hashmap *profiles) {
    pa_card_profile *p;
    struct profile_data *d;

    p = pa_card_profile_new("off", _("Off"), sizeof(struct profile_data));

    d = PA_CARD_PROFILE_DATA(p);
    d->profile = NULL;

    pa_hashmap_put(profiles, p->name, p);
}

/* Change UCM verb and device to match selected card profile */
static int ucm_set_profile(struct userdata *u, char *profile_name)
{
    struct pa_alsa_ucm_config *ucm = &u->ucm;
    struct profile_data *d;
    char *new_verb_name, *new_device_name, *old_verb_name, *old_device_name, *tmp;
    int ret = 0;

    /* current profile */
    d = PA_CARD_PROFILE_DATA(u->card->active_profile);

    new_device_name = strchr(profile_name, ':') + 2;
    if (!new_device_name) {
        pa_log("no new device found for %s", profile_name);
        return -1;
    }

    old_device_name = strchr(d->profile->name, ':') + 2;
    if (!old_device_name) {
        pa_log("no current device found for %s", d->profile->name);
        return -1;
    }

    new_verb_name = pa_xstrdup(profile_name);
    tmp = strchr(new_verb_name, ':');
    if (!tmp) {
        pa_log("no new verb found for %s", profile_name);
        pa_xfree(new_verb_name);
        return -1;
    }
    *tmp = 0;

    old_verb_name = pa_xstrdup(d->profile->name);
    tmp = strchr(old_verb_name, ':');
    if (!tmp) {
        pa_log("no new verb found for %s", d->profile->name);
        pa_xfree(new_verb_name);
        pa_xfree(old_verb_name);
        return -1;
    }
    *tmp = 0;

    pa_log("set ucm: old verb %s device %s", old_verb_name, old_device_name);
    pa_log("set ucm: new verb %s device %s", new_verb_name, new_device_name);

    /* do we need to change the verb */
    if (strcmp(new_verb_name, old_verb_name) == 0) {
        /* just change the device only */
        tmp = pa_sprintf_malloc("_swdev/%s", old_device_name);
        if ((snd_use_case_set(ucm->ucm_mgr, tmp, new_device_name)) < 0) {
            pa_log("failed to switch device %s %s", tmp, new_device_name);
            ret = -1;
        }
        pa_xfree(tmp);
    } else {
        /* change verb and device */
        if ((snd_use_case_set(ucm->ucm_mgr, "_verb", new_verb_name)) < 0) {
            pa_log("failed to set verb %s", new_verb_name);
            ret = -1;
        }
        if (snd_use_case_set(ucm->ucm_mgr, "_enadev", new_device_name) < 0) {
            pa_log("failed to set device %s", new_device_name);
            ret = -1;
        }
    }

    pa_xfree(new_verb_name);
    pa_xfree(old_verb_name);
    return ret;
}

static int card_set_profile(pa_card *c, pa_card_profile *new_profile) {
    struct userdata *u;
    struct profile_data *nd, *od;
    uint32_t idx;
    pa_alsa_mapping *am;
    pa_queue *sink_inputs = NULL, *source_outputs = NULL;

    pa_assert(c);
    pa_assert(new_profile);
    pa_assert_se(u = c->userdata);

    nd = PA_CARD_PROFILE_DATA(new_profile);
    od = PA_CARD_PROFILE_DATA(c->active_profile);

    if (od->profile && od->profile->output_mappings)
        PA_IDXSET_FOREACH(am, od->profile->output_mappings, idx) {
            if (!am->sink)
                continue;

            if (nd->profile &&
                nd->profile->output_mappings &&
                pa_idxset_get_by_data(nd->profile->output_mappings, am, NULL))
                continue;

            sink_inputs = pa_sink_move_all_start(am->sink, sink_inputs);
            pa_alsa_sink_free(am->sink);
            am->sink = NULL;
        }

    if (od->profile && od->profile->input_mappings)
        PA_IDXSET_FOREACH(am, od->profile->input_mappings, idx) {
            if (!am->source)
                continue;

            if (nd->profile &&
                nd->profile->input_mappings &&
                pa_idxset_get_by_data(nd->profile->input_mappings, am, NULL))
                continue;

            source_outputs = pa_source_move_all_start(am->source, source_outputs);
            pa_alsa_source_free(am->source);
            am->source = NULL;
        }

    /* if UCM is avalible for this card then update the verb */
    if (u->ucm.status == PA_ALSA_UCM_ENABLED) {
        if (ucm_set_profile(u, nd->profile->name) < 0)
            return -1;
    }

    if (nd->profile && nd->profile->output_mappings)
        PA_IDXSET_FOREACH(am, nd->profile->output_mappings, idx) {

            if (!am->sink)
                am->sink = pa_alsa_sink_new(c->module, u->modargs, __FILE__, c, am);

            if (sink_inputs && am->sink) {
                pa_sink_move_all_finish(am->sink, sink_inputs, FALSE);
                sink_inputs = NULL;
            }
        }

    if (nd->profile && nd->profile->input_mappings)
        PA_IDXSET_FOREACH(am, nd->profile->input_mappings, idx) {

            if (!am->source)
                am->source = pa_alsa_source_new(c->module, u->modargs, __FILE__, c, am);

            if (source_outputs && am->source) {
                pa_source_move_all_finish(am->source, source_outputs, FALSE);
                source_outputs = NULL;
            }
        }

    if (sink_inputs)
        pa_sink_move_all_fail(sink_inputs);

    if (source_outputs)
        pa_source_move_all_fail(source_outputs);

    return 0;
}

static int init_profile(struct userdata *u) {
    uint32_t idx;
    pa_alsa_mapping *am;
    struct profile_data *d;
    char *device_name, *verb_name, *tmp;
    struct pa_alsa_ucm_config *ucm = &u->ucm;

    pa_assert(u);

    d = PA_CARD_PROFILE_DATA(u->card->active_profile);

    if (u->ucm.status == PA_ALSA_UCM_ENABLED) {
        /* Set initial verb and device */
	verb_name = pa_xstrdup(d->profile->name);
	tmp = strchr(verb_name, ':');
        if (!tmp) {
            pa_log("no new verb found for %s", d->profile->name);
            pa_xfree(verb_name);
            return -1;
        }
        *tmp = 0;

        if ((snd_use_case_set(ucm->ucm_mgr, "_verb", verb_name)) < 0) {
            pa_log("failed to set verb %s", d->profile->name);
            return -1;
        }

        device_name = strchr(d->profile->name, ':') + 2;
        if (snd_use_case_set(ucm->ucm_mgr, "_enadev", device_name) < 0) {
            pa_log("failed to set device %s", device_name);
            return -1;
        }
    }

    if (d->profile && d->profile->output_mappings)
        PA_IDXSET_FOREACH(am, d->profile->output_mappings, idx)
            am->sink = pa_alsa_sink_new(u->module, u->modargs, __FILE__, u->card, am);

    if (d->profile && d->profile->input_mappings)
        PA_IDXSET_FOREACH(am, d->profile->input_mappings, idx)
            am->source = pa_alsa_source_new(u->module, u->modargs, __FILE__, u->card, am);

    return 0;
}

static void set_card_name(pa_card_new_data *data, pa_modargs *ma, const char *device_id) {
    char *t;
    const char *n;

    pa_assert(data);
    pa_assert(ma);
    pa_assert(device_id);

    if ((n = pa_modargs_get_value(ma, "card_name", NULL))) {
        pa_card_new_data_set_name(data, n);
        data->namereg_fail = TRUE;
        return;
    }

    if ((n = pa_modargs_get_value(ma, "name", NULL)))
        data->namereg_fail = TRUE;
    else {
        n = device_id;
        data->namereg_fail = FALSE;
    }

    t = pa_sprintf_malloc("alsa_card.%s", n);
    pa_card_new_data_set_name(data, t);
    pa_xfree(t);
}

static void ucm_add_mapping(pa_alsa_profile *p, pa_alsa_mapping *m)
{
    switch (m->direction) {
    case PA_ALSA_DIRECTION_ANY:
        pa_idxset_put(p->output_mappings, m, NULL);
        pa_idxset_put(p->input_mappings, m, NULL);
        break;
     case PA_ALSA_DIRECTION_OUTPUT:
        pa_idxset_put(p->output_mappings, m, NULL);
        break;
     case PA_ALSA_DIRECTION_INPUT:
        pa_idxset_put(p->input_mappings, m, NULL);
        break;
    }
    p->priority += m->priority * 100;
}

static pa_alsa_profile *ucm_new_profile(pa_alsa_profile_set *ps, const char *verb_name, const char *dev_name)
{
    pa_alsa_profile *p;
    char *profile_name;

    if (dev_name)
        profile_name = pa_sprintf_malloc("%s: %s", verb_name, dev_name);
    else
        profile_name = pa_sprintf_malloc("%s:", verb_name);

    if (pa_hashmap_get(ps->profiles, verb_name)) {
        pa_xfree(profile_name);
        return NULL;
    }

    p = pa_xnew0(pa_alsa_profile, 1);
    p->profile_set = ps;
    p->name = profile_name;

    p->output_mappings = pa_idxset_new(pa_idxset_trivial_hash_func, pa_idxset_trivial_compare_func);
    p->input_mappings = pa_idxset_new(pa_idxset_trivial_hash_func, pa_idxset_trivial_compare_func);

    ps->probed = TRUE;
    p->supported = 1;
    pa_hashmap_put(ps->profiles, p->name, p);
    return p;
}

static int ucm_create_mapping(pa_alsa_profile_set *ps, struct pa_alsa_profile *p, struct pa_alsa_ucm_device *device, const char *verb_name, const char *device_name, char *strings)
{
    pa_alsa_mapping *m;
    char *mapping_name;
    int i = 0;

    if (device_name)
        mapping_name = pa_sprintf_malloc("Mapping %s: %s", verb_name, device_name);
    else
        mapping_name = pa_sprintf_malloc("Mapping %s", verb_name);

    m = mapping_get(ps, mapping_name);
    if (!m) {
        pa_log("no mapping for %s", mapping_name);
        pa_xfree(mapping_name);
        return -1;
    }
    pa_log_info("ucm mapping: %s dev %s", mapping_name, strings);

    m->supported = TRUE;
    m->channel_map.map[0] = PA_CHANNEL_POSITION_LEFT;
    m->channel_map.map[1] = PA_CHANNEL_POSITION_RIGHT;
    m->device_strings = pa_split_spaces_strv(strings);
    pa_xfree(mapping_name);

    if (!device_name)
        goto not_found;
    do {
        if (strcmp(dev_info[i].id, device_name) == 0)
            goto found;
    } while (dev_info[++i].id);

not_found:
    /* use default values */
    m->priority = 100;
    m->direction = PA_ALSA_DIRECTION_ANY;
    m->channel_map.channels = 2;
    ucm_add_mapping(p, m);
    return 0;

found:
    m->priority = dev_info[i].priority;
    m->direction = dev_info[i].direction;
    m->channel_map.channels = dev_info[i].channels;
    ucm_add_mapping(p, m);
    return 0;
}

static int ucm_create_profile(pa_alsa_profile_set *ps, struct pa_alsa_ucm_verb *verb,
        const char *verb_name, const char *verb_sink, const char *verb_source) {

    struct pa_alsa_profile *p;
    struct pa_alsa_ucm_device *dev;
    char *dev_strings;
    int num_devices = 0;

    pa_assert(ps);

    /* Add a mapping for each verb modifier for this profile if the sink/source is different to the verb */
    PA_LLIST_FOREACH(dev, verb->devices) {
        const char *dev_name, *sink, *source;

        dev_name = pa_proplist_gets(dev->proplist, PA_PROP_UCM_NAME);

        /* if no default sink is set use hw:0 */
        sink = pa_proplist_gets(dev->proplist, PA_PROP_UCM_SINK);
        if (sink == NULL)
            sink = "hw:0";

        /* if no default sink is set use hw:0 */
        source = pa_proplist_gets(dev->proplist, PA_PROP_UCM_SOURCE);
        if (source == NULL)
            source = "hw:0";

		dev_strings = pa_sprintf_malloc("%s %s", sink, source);
        p = ucm_new_profile(ps, verb_name, dev_name);
        ucm_create_mapping(ps, p, dev, verb_name, dev_name, dev_strings);
        pa_xfree(dev_strings);
        pa_alsa_profile_dump(p);
        num_devices++;
    }

    if (num_devices)
		return 0;

    /* Create a default mapping for each verb/profile */
    dev_strings = pa_sprintf_malloc("%s %s", verb_sink, verb_source);
    p = ucm_new_profile(ps, verb_name, NULL);
    ucm_create_mapping(ps, p, dev, verb_name, NULL, dev_strings);
    pa_xfree(dev);

    return 0;
}

static pa_alsa_profile_set* add_ucm_profile_set(struct userdata *u) {
    struct pa_alsa_ucm_config *ucm = &u->ucm;
    struct pa_alsa_ucm_verb *verb;
    pa_alsa_profile_set *ps;
    pa_alsa_profile *p;
    pa_alsa_mapping *m;
    void *state;

    ps = pa_xnew0(pa_alsa_profile_set, 1);
    ps->mappings = pa_hashmap_new(pa_idxset_string_hash_func, pa_idxset_string_compare_func);
    ps->profiles = pa_hashmap_new(pa_idxset_string_hash_func, pa_idxset_string_compare_func);
    ps->decibel_fixes = pa_hashmap_new(pa_idxset_string_hash_func, pa_idxset_string_compare_func);

    /* create a profile for each verb */
    PA_LLIST_FOREACH(verb, ucm->verbs) {
        const char *sink, *source, *verb_name;
        char *dev;

        verb_name = pa_proplist_gets(verb->proplist, PA_PROP_UCM_NAME);
        if (verb_name == NULL) {
            pa_log("verb with no name");
            continue;
        }

        /* if no default sink is set use hw:0 */
        sink = pa_proplist_gets(verb->proplist, PA_PROP_UCM_SINK);
        if (sink == NULL)
            sink = "hw:0";

        /* if no default sink is set use hw:0 */
        source = pa_proplist_gets(verb->proplist, PA_PROP_UCM_SOURCE);
        if (source == NULL)
            source = "hw:0";

        dev = pa_sprintf_malloc("%s %s", sink, source);
	    ucm_create_profile(ps, verb, verb_name, sink, source);
        pa_xfree(dev);
    }

    PA_HASHMAP_FOREACH(m, ps->mappings, state)
        if (mapping_verify(m, &u->core->default_channel_map) < 0)
            goto fail;

    PA_HASHMAP_FOREACH(p, ps->profiles, state)
        if (profile_verify(p) < 0)
            goto fail;

    return ps;

fail:
    pa_log("failed to add UCM mappings");
    pa_alsa_profile_set_free(ps);
    return NULL;
}

static int card_query_ucm_profiles(struct userdata *u, int card_index)
{
    pa_module *m = u->module;
    char *card_name;
    const char **verb_list;
    int num_verbs, i, err;

    /* is UCM available for this card ? */
    snd_card_get_name(card_index, &card_name);
    err = snd_use_case_mgr_open(&u->ucm.ucm_mgr, card_name);
    if (err < 0) {
        pa_log("UCM not avaliable for card %s", card_name);
        u->ucm.status = PA_ALSA_UCM_DISABLED;
        return 0;
    }

    pa_log("UCM avaliable for card %s", card_name);

    /* get a list of all UCM verbs (profiles) for this card */
    num_verbs = snd_use_case_verb_list(u->ucm.ucm_mgr, &verb_list);
    if (num_verbs <= 0) {
        pa_log("UCM verb list not found for %s", card_name);
        return 0;
    }

    /* get the properties of each UCM verb */
    for (i = 0; i < num_verbs; i += 2) {
        struct pa_alsa_ucm_verb *verb;

        verb = pa_xnew0(pa_alsa_ucm_verb, 1);
        verb->proplist = pa_proplist_new();

        /* Get devices and modifiers for each verb */
        err = ucm_get_properties(verb, u->ucm.ucm_mgr, verb_list[i]);
        if (err < 0) {
            pa_log("Failed to set the verb %s", verb_list[i]);
            continue;
	}

        pa_proplist_sets(verb->proplist, PA_PROP_UCM_NAME, verb_list[i]);
        PA_LLIST_PREPEND(pa_alsa_ucm_verb, u->ucm.verbs, verb);
    }

    /* create the profile set for the UCM card */
    u->profile_set = add_ucm_profile_set(u);
    pa_alsa_profile_set_dump(u->profile_set);

    u->ucm.status = PA_ALSA_UCM_ENABLED;
    return 1;
}

static void free_ucm(struct userdata *u)
{
    struct pa_alsa_ucm_device *di, *dn;
    struct pa_alsa_ucm_modifier *mi, *mn;
    struct pa_alsa_ucm_verb *verb, *vi, *vn;

    verb = u->ucm.verbs;

    PA_LLIST_FOREACH_SAFE(di, dn, verb->devices) {
        PA_LLIST_REMOVE(pa_alsa_ucm_device, verb->devices, di);
        pa_proplist_free(di->proplist);
        pa_xfree(di);
    }

    PA_LLIST_FOREACH_SAFE(mi, mn, verb->modifiers) {
        PA_LLIST_REMOVE(pa_alsa_ucm_modifier, verb->modifiers, mi);
        pa_proplist_free(mi->proplist);
        pa_xfree(mi);
    }

    PA_LLIST_FOREACH_SAFE(vi, vn, u->ucm.verbs) {
        PA_LLIST_REMOVE(pa_alsa_ucm_verb, u->ucm.verbs, vi);
        pa_proplist_free(vi->proplist);
        pa_xfree(vi);
    }
}

static pa_hook_result_t jack_insert_new_hook_callback(pa_core *c, struct userdata *u) {
    struct pa_alsa_ucm_config *ucm = &u->ucm;
    struct profile_data *d;
    char *device_name;

    pa_assert(u);

    d = PA_CARD_PROFILE_DATA(u->card->active_profile);

    pa_log_debug("Jack insert new hook callback");

    device_name = strchr(d->profile->name, ':') + 2;
    if (!device_name) {
        pa_log("no device found for %s", d->profile->name);
        return PA_HOOK_CANCEL;
    }

    if (strcmp(device_name, "Headset.0") == 0)
        return PA_HOOK_OK;

    /* Set headset.0 device per default */
    if (snd_use_case_set(ucm->ucm_mgr, "_enadev", "Headset.0") < 0) {
        pa_log("failed to set device Headset.0");
        return PA_HOOK_CANCEL;
    }

    return PA_HOOK_OK;
}

static pa_hook_result_t jack_remove_new_hook_callback(pa_core *c, struct userdata *u) {
    struct profile_data *d;
    struct pa_alsa_ucm_config *ucm = &u->ucm;
    char *device_name;

    pa_assert(u);

    d = PA_CARD_PROFILE_DATA(u->card->active_profile);

    pa_log_debug("Jack removed new hook callback");

    device_name = strchr(d->profile->name, ':') + 2;
    if(!device_name) {
        pa_log("no device found for %s", d->profile->name);
        return PA_HOOK_CANCEL;
    }

    /* Set current profile device */
    if (snd_use_case_set(ucm->ucm_mgr, "_enadev", "Headset.0") < 0) {
        pa_log("failed to set device Headset.0");
        return PA_HOOK_CANCEL;
    }

    return PA_HOOK_OK;
}

int pa__init(pa_module *m) {
    pa_card_new_data data;
    pa_modargs *ma;
    int alsa_card_index;
    struct userdata *u;
    pa_reserve_wrapper *reserve = NULL;
    const char *description;
    const char *profile = NULL;
    char *fn = NULL;
    pa_bool_t namereg_fail = FALSE;

    pa_alsa_refcnt_inc();

    pa_assert(m);

    if (!(ma = pa_modargs_new(m->argument, valid_modargs))) {
        pa_log("Failed to parse module arguments");
        goto fail;
    }

    m->userdata = u = pa_xnew0(struct userdata, 1);
    u->core = m->core;
    u->module = m;
    u->device_id = pa_xstrdup(pa_modargs_get_value(ma, "device_id", DEFAULT_DEVICE_ID));
    u->modargs = ma;

    if ((alsa_card_index = snd_card_get_index(u->device_id)) < 0) {
        pa_log("Card '%s' doesn't exist: %s", u->device_id, pa_alsa_strerror(alsa_card_index));
        goto fail;
    }

    u->jack_insert_new_hook_slot = pa_hook_connect(&m->core->hooks[PA_CORE_HOOK_JACK_INSERT], PA_HOOK_NORMAL, (pa_hook_cb_t) jack_insert_new_hook_callback, u);
    u->jack_remove_new_hook_slot = pa_hook_connect(&m->core->hooks[PA_CORE_HOOK_JACK_REMOVE], PA_HOOK_NORMAL, (pa_hook_cb_t) jack_remove_new_hook_callback, u);

    if (!pa_in_system_mode()) {
        char *rname;

        if ((rname = pa_alsa_get_reserve_name(u->device_id))) {
            reserve = pa_reserve_wrapper_get(m->core, rname);
            pa_xfree(rname);

            if (!reserve)
                goto fail;
        }
    }

    if (card_query_ucm_profiles(u, alsa_card_index))
        pa_log_info("Found UCM profiles");
    else {
#ifdef HAVE_UDEV
        fn = pa_udev_get_property(alsa_card_index, "PULSE_PROFILE_SET");
#endif

        if (pa_modargs_get_value(ma, "profile_set", NULL)) {
            pa_xfree(fn);
            fn = pa_xstrdup(pa_modargs_get_value(ma, "profile_set", NULL));
        }

        u->profile_set = pa_alsa_profile_set_new(fn, &u->core->default_channel_map);
        pa_xfree(fn);
    }

    if (!u->profile_set)
        goto fail;

    pa_alsa_profile_set_probe(u->profile_set, u->device_id, &m->core->default_sample_spec, m->core->default_n_fragments, m->core->default_fragment_size_msec);
    pa_alsa_profile_set_dump(u->profile_set);

    pa_card_new_data_init(&data);
    data.driver = __FILE__;
    data.module = m;

    pa_alsa_init_proplist_card(m->core, data.proplist, alsa_card_index);

    pa_proplist_sets(data.proplist, PA_PROP_DEVICE_STRING, u->device_id);
    pa_alsa_init_description(data.proplist);
    set_card_name(&data, ma, u->device_id);

    /* We need to give pa_modargs_get_value_boolean() a pointer to a local
     * variable instead of using &data.namereg_fail directly, because
     * data.namereg_fail is a bitfield and taking the address of a bitfield
     * variable is impossible. */
    namereg_fail = data.namereg_fail;
    if (pa_modargs_get_value_boolean(ma, "namereg_fail", &namereg_fail) < 0) {
        pa_log("Failed to parse namereg_fail argument.");
        pa_card_new_data_done(&data);
        goto fail;
    }
    data.namereg_fail = namereg_fail;

    if (reserve)
        if ((description = pa_proplist_gets(data.proplist, PA_PROP_DEVICE_DESCRIPTION)))
            pa_reserve_wrapper_set_application_device_name(reserve, description);

    data.profiles = pa_hashmap_new(pa_idxset_string_hash_func, pa_idxset_string_compare_func);

    add_profiles(u, data.profiles);

    if (pa_hashmap_isempty(data.profiles)) {
        pa_log("Failed to find a working profile.");
        pa_card_new_data_done(&data);
        goto fail;
    }

    add_disabled_profile(data.profiles);

    if (pa_modargs_get_proplist(ma, "card_properties", data.proplist, PA_UPDATE_REPLACE) < 0) {
        pa_log("Invalid properties");
        pa_card_new_data_done(&data);
        goto fail;
    }

    if ((profile = pa_modargs_get_value(ma, "profile", NULL)))
        pa_card_new_data_set_profile(&data, profile);

    u->card = pa_card_new(m->core, &data);
    pa_card_new_data_done(&data);

    if (!u->card)
        goto fail;

    u->card->userdata = u;
    u->card->set_profile = card_set_profile;

    init_profile(u);

    if (reserve)
        pa_reserve_wrapper_unref(reserve);

    if (!pa_hashmap_isempty(u->profile_set->decibel_fixes))
        pa_log_warn("Card %s uses decibel fixes (i.e. overrides the decibel information for some alsa volume elements). "
                    "Please note that this feature is meant just as a help for figuring out the correct decibel values. "
                    "Pulseaudio is not the correct place to maintain the decibel mappings! The fixed decibel values "
                    "should be sent to ALSA developers so that they can fix the driver. If it turns out that this feature "
                    "is abused (i.e. fixes are not pushed to ALSA), the decibel fix feature may be removed in some future "
                    "Pulseaudio version.", u->card->name);

    return 0;

fail:
    if (reserve)
        pa_reserve_wrapper_unref(reserve);

    pa__done(m);

    return -1;
}

int pa__get_n_used(pa_module *m) {
    struct userdata *u;
    int n = 0;
    uint32_t idx;
    pa_sink *sink;
    pa_source *source;

    pa_assert(m);
    pa_assert_se(u = m->userdata);
    pa_assert(u->card);

    PA_IDXSET_FOREACH(sink, u->card->sinks, idx)
        n += pa_sink_linked_by(sink);

    PA_IDXSET_FOREACH(source, u->card->sources, idx)
        n += pa_source_linked_by(source);

    return n;
}

void pa__done(pa_module*m) {
    struct userdata *u;

    pa_assert(m);

    if (!(u = m->userdata))
        goto finish;

    if (u->jack_insert_new_hook_slot)
        pa_hook_slot_free(u->jack_insert_new_hook_slot);

    if (u->jack_remove_new_hook_slot)
        pa_hook_slot_free(u->jack_remove_new_hook_slot);

    if (u->card && u->card->sinks) {
        pa_sink *s;

        while ((s = pa_idxset_steal_first(u->card->sinks, NULL)))
            pa_alsa_sink_free(s);
    }

    if (u->card && u->card->sources) {
        pa_source *s;

        while ((s = pa_idxset_steal_first(u->card->sources, NULL)))
            pa_alsa_source_free(s);
    }

    if (u->ucm.status == PA_ALSA_UCM_ENABLED)
        free_ucm(u);

    if (u->card)
        pa_card_free(u->card);

    if (u->modargs)
        pa_modargs_free(u->modargs);

    if (u->profile_set)
        pa_alsa_profile_set_free(u->profile_set);

    pa_xfree(u->device_id);
    pa_xfree(u);

finish:
    pa_alsa_refcnt_dec();
}
