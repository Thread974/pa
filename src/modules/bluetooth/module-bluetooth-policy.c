/***
  This file is part of PulseAudio.

  Copyright 2006 Lennart Poettering
  Copyright 2009 Canonical Ltd
  Copyright (C) 2012 Intel Corporation

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

#include <pulsecore/core.h>
#include <pulsecore/sink-input.h>
#include <pulsecore/source-output.h>
#include <pulsecore/source.h>
#include <pulsecore/modargs.h>
#include <pulsecore/log.h>
#include <pulsecore/namereg.h>
#include <pulsecore/core-util.h>

#include "module-bluetooth-policy-symdef.h"
#include "bluetooth-util.h"

PA_MODULE_AUTHOR("Frédéric Dalleau");
PA_MODULE_DESCRIPTION("When a sink/source is added, load module-loopback");
PA_MODULE_VERSION(PACKAGE_VERSION);
PA_MODULE_LOAD_ONCE(TRUE);

static const char* const valid_modargs[] = {
    NULL,
};

struct userdata {
    pa_core *core;
    pa_hook_slot
        *sink_put_slot,
        *source_put_slot,
        *sink_unlink_slot,
        *source_unlink_slot,
        *discovery_slot;
    pa_hashmap *hashmap;
    pa_bluetooth_discovery *discovery;
};

/* When a sink is created, loopback default source (microphone) on it */
static pa_hook_result_t sink_put_hook_callback(pa_core *c, pa_sink *sink, void* userdata) {
    pa_source *defsource;
    pa_sink *defsink;
    struct userdata *u = userdata;
    const char *s;
    pa_module *m = NULL;
    char *args;

    pa_assert(c);
    pa_assert(sink);

    /* Don't want to run during startup or shutdown */
    if (c->state != PA_CORE_RUNNING)
        return PA_HOOK_OK;

    pa_log_debug("Sink %s being created", sink->name);

    m = pa_hashmap_get(u->hashmap, sink->name);
    if (m) {
        pa_log_debug("Loopback already loaded for sink %s with args '%s'", sink->name, m->argument);
        return PA_HOOK_OK;
    }

    /* Only consider bluetooth sink and sources */
    if ((s = pa_proplist_gets(sink->proplist, PA_PROP_DEVICE_BUS))) {
        if (!pa_streq(s, "bluetooth"))
            return PA_HOOK_OK;
    }

    /* Restrict to A2DP sink role and HFP HF role */
    if ((s = sink->card->active_profile->name)) {
        if (!pa_streq(s, "hfgw") && !pa_streq(s, "a2dp_source"))
            return PA_HOOK_OK;
    }

    /* Prevent loopback default source over default sink */
    defsink = pa_namereg_get_default_sink(c);
    if (defsink == sink)
        return PA_HOOK_OK;

    /* Find suitable source to loopback from */
    defsource = pa_namereg_get_default_source(c);
    if (!defsource)
        defsource = defsink->monitor_source;

    if (!defsource) {
        pa_log_debug("Cannot find suitable source for loopback to %s", sink->name);
        return PA_HOOK_OK;
    }

    /* Load module-loopback with selected source */
    args = pa_sprintf_malloc("source=\"%s\" sink=\"%s\" source_dont_move=\"true\"", defsource->name, sink->name);
    m = pa_module_load(c, "module-loopback", args);

    if (m)
        pa_hashmap_put(u->hashmap, sink->name, m);
    else
        pa_log_debug("Failed to loopback sink %s with args '%s'", sink->name, args);

    pa_xfree(args);

    return PA_HOOK_OK;
}

/* When a source is created, loopback default the source to default sink */
static pa_hook_result_t source_put_hook_callback(pa_core *c, pa_source *source, void* userdata) {
    pa_source *defsource;
    pa_sink *defsink;
    struct userdata *u = userdata;
    const char *s;
    pa_module *m = NULL;
    char *args;

    pa_assert(c);
    pa_assert(source);

    /* Don't want to run during startup or shutdown */
    if (c->state != PA_CORE_RUNNING)
        return PA_HOOK_OK;

    pa_log_debug("Source %s being created", source->name);

    m = pa_hashmap_get(u->hashmap, source->name);
    if (m) {
        pa_log_debug("Loopback already loaded for source  %s with args '%s'", source->name, m->argument);
        return PA_HOOK_OK;
    }

    /* Only consider bluetooth sink and sources */
    if ((s = pa_proplist_gets(source->proplist, PA_PROP_DEVICE_BUS))) {
        if (!pa_streq(s, "bluetooth"))
            return PA_HOOK_OK;
    }

    /* Restrict to A2DP sink role and HFP HF role */
    if ((s = source->card->active_profile->name)) {
        if (!pa_streq(s, "hfgw") && !pa_streq(s, "a2dp_source"))
            return PA_HOOK_OK;
    }

    /* Prevent loopback default source over default sink */
    defsource = pa_namereg_get_default_source(c);
    if (defsource == source)
        return PA_HOOK_OK;

    /* Find suitable sink to loopback to */
    defsink = pa_namereg_get_default_sink(c);
    if (!defsink) {
        pa_log_debug("Cannot find suitable sink for loopback from %s", source->name);
        return PA_HOOK_OK;
    }

    /* Load module-loopback with selected sink */
    args = pa_sprintf_malloc("source=\"%s\" sink=\"%s\" source_dont_move=\"true\"", source->name, defsink->name);
    m = pa_module_load(c, "module-loopback", args);

    if (m)
        pa_hashmap_put(u->hashmap, source->name, m);
    else
        pa_log_debug("Failed to loopback source %s with args '%s'", source->name, args);

    pa_xfree(args);

    return PA_HOOK_OK;
}

/* When a sink is removed, unload any existing loopback on it */
static pa_hook_result_t sink_unlink_hook_callback(pa_core *c, pa_sink *sink, void* userdata) {
    struct userdata *u = userdata;
    pa_module *m = NULL;

    pa_assert(c);
    pa_assert(sink);

    pa_log_debug("Sink %s being removed", sink->name);

    m = pa_hashmap_get(u->hashmap, sink->name);
    if (!m) {
        pa_log_debug("No loopback attached to sink %s", sink->name);
        return PA_HOOK_OK;
    }

    pa_module_unload_request(m, TRUE);
    pa_hashmap_remove(u->hashmap, sink->name);

    return PA_HOOK_OK;
}

/* When a source is removed, unload any existing loopback on it */
static pa_hook_result_t source_unlink_hook_callback(pa_core *c, pa_source *source, void* userdata) {
    struct userdata *u = userdata;
    pa_module *m = NULL;

    pa_assert(c);
    pa_assert(source);

    pa_log_debug("Source %s being removed", source->name);

    m = pa_hashmap_get(u->hashmap, source->name);
    if (!m) {
        pa_log_debug("No loopback attached to source %s", source->name);
        return PA_HOOK_OK;
    }

    pa_module_unload_request(m, TRUE);
    pa_hashmap_remove(u->hashmap, source->name);

    return PA_HOOK_OK;
}

static pa_hook_result_t update_device_profile(pa_bluetooth_discovery *y, const pa_bluetooth_device *d, struct userdata *u) {
    char *name, *s;
    const char *profile;
    pa_card *c;

    pa_assert(u);
    pa_assert(d);

    pa_log_info("device %s: audio %d, hfp %d, hfgw %d, a2dp %d, a2dp_source %d", d->name, d->audio_state, d->headset_state, d->hfgw_state, d->audio_sink_state, d->audio_source_state);

    name = pa_sprintf_malloc("bluez_card.%s", d->address);

    for (s = name; *s; s++)
        if (*s == ':')
            *s = '_';

    pa_log_info("Card: %s", name);
    c = pa_namereg_get(u->core, name, PA_NAMEREG_CARD);

    if (!c) {
         pa_log("No card found with name %s", name);
         pa_xfree(name);
         return PA_HOOK_OK;
    }
    pa_xfree(name);

    if (d->hfgw_state > PA_BT_AUDIO_STATE_CONNECTED)
        profile = "hfgw";
    else if (d->audio_source_state > PA_BT_AUDIO_STATE_CONNECTED)
        profile = "a2dp_source";
    else if (d->headset_state > PA_BT_AUDIO_STATE_CONNECTED)
        profile = "hfp";
    else if (d->audio_sink_state > PA_BT_AUDIO_STATE_CONNECTED)
        profile = "a2dp";
    else
        profile = "off";

    if (!pa_card_set_profile(c, profile, FALSE))
        pa_log("Failed to set card %s to profile %s", c->name, profile);

    return PA_HOOK_OK;
}

int pa__init(pa_module*m) {
    pa_modargs *ma;
    struct userdata *u;

    pa_assert(m);

    if (!(ma = pa_modargs_new(m->argument, valid_modargs))) {
        pa_log("Failed to parse module arguments");
        return -1;
    }
    pa_modargs_free(ma);

    m->userdata = u = pa_xnew(struct userdata, 1);
    u->core = m->core;

    if (!(u->discovery = pa_bluetooth_discovery_get(m->core)))
        goto fail;

    /* A little bit later than module-rescue-streams... */
    u->sink_put_slot = pa_hook_connect(&m->core->hooks[PA_CORE_HOOK_SINK_PUT], PA_HOOK_LATE+30, (pa_hook_cb_t) sink_put_hook_callback, u);
    u->source_put_slot = pa_hook_connect(&m->core->hooks[PA_CORE_HOOK_SOURCE_PUT], PA_HOOK_LATE+20, (pa_hook_cb_t) source_put_hook_callback, u);
    u->sink_unlink_slot = pa_hook_connect(&m->core->hooks[PA_CORE_HOOK_SINK_UNLINK], PA_HOOK_LATE+30, (pa_hook_cb_t) sink_unlink_hook_callback, u);
    u->source_unlink_slot = pa_hook_connect(&m->core->hooks[PA_CORE_HOOK_SOURCE_UNLINK], PA_HOOK_LATE+20, (pa_hook_cb_t) source_unlink_hook_callback, u);
    u->discovery_slot = pa_hook_connect(pa_bluetooth_discovery_hook(u->discovery), PA_HOOK_NORMAL, (pa_hook_cb_t) update_device_profile, u);

    u->hashmap = pa_hashmap_new(pa_idxset_string_hash_func, pa_idxset_string_compare_func);

    return 0;

fail:
    pa__done(m);

    return -1;
}

void pa__done(pa_module*m) {
    struct userdata *u;

    pa_assert(m);

    if (!(u = m->userdata))
        return;

    if (u->discovery_slot)
        pa_hook_slot_free(u->discovery_slot);
    if (u->sink_put_slot)
        pa_hook_slot_free(u->sink_put_slot);
    if (u->source_put_slot)
        pa_hook_slot_free(u->source_put_slot);
    if (u->sink_unlink_slot)
        pa_hook_slot_free(u->sink_unlink_slot);
    if (u->source_unlink_slot)
        pa_hook_slot_free(u->source_unlink_slot);

    if (u->hashmap) {
        struct pa_module *mi;

        while ((mi = pa_hashmap_steal_first(u->hashmap))) {
            pa_module_unload_request(mi, TRUE);
        }

        pa_hashmap_free(u->hashmap, NULL, NULL);
    }

    if (u->discovery)
        pa_bluetooth_discovery_unref(u->discovery);

    pa_xfree(u);
}
