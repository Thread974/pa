/***
  This file is part of PulseAudio.

  Copyright 2011 Intel Corporation

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

PA_MODULE_AUTHOR("Frédéric Dalleau");
PA_MODULE_DESCRIPTION("Bluetooth policy module");
PA_MODULE_VERSION(PACKAGE_VERSION);
PA_MODULE_LOAD_ONCE(TRUE);

static const char* const valid_modargs[] = {
    NULL,
};

struct userdata {
    pa_hook_slot
        *sink_put_slot,
        *source_put_slot;
    pa_hashmap *hashmap;
};

struct module_info {
    char *args;
    uint32_t module;
};

/* When a sink is created, loopback default source (microphone) on it */
static pa_hook_result_t sink_put_hook_callback(pa_core *c, pa_sink *sink, void* userdata) {
    pa_source *defsource;
    pa_sink *defsink;
    struct userdata *u = userdata;
    const char *s;
    pa_module *m = NULL;
    char *args;
    struct module_info *mi;

    pa_assert(c);
    pa_assert(sink);

    pa_log_debug("Sink %s being created", sink->name);

    mi = pa_hashmap_get(u->hashmap, sink->name);
    if (mi) {
        pa_log_debug("Loopback already loaded for sink %s with args '%s'", sink->name, mi->args);
        return PA_HOOK_OK;
    }

    /* Don't want to run during startup or shutdown */
    if (c->state != PA_CORE_RUNNING)
        return PA_HOOK_OK;

    /* Don't switch to any internal devices */
    if ((s = pa_proplist_gets(sink->proplist, PA_PROP_DEVICE_BUS))) {
        if (pa_streq(s, "pci"))
            return PA_HOOK_OK;
        else if (pa_streq(s, "isa"))
            return PA_HOOK_OK;
    }

    /* Do not loopback default source over default sink */
    defsink = pa_namereg_get_default_sink(c);
    if (defsink == sink)
        return PA_HOOK_OK;

    /* Find suitable source to loopback */
    defsource = pa_namereg_get_default_source(c);
    if (!defsource) {
        defsource = defsink->monitor_source;
    }

    if (!defsource) {
        pa_log_debug("Cannot find suitable source for loopback to %s", sink->name);
        return PA_HOOK_OK;
    }

    /* Load module-loopback with default source */
    args = pa_sprintf_malloc("source=\"%s\" sink=\"%s\" source_dont_move=\"true\"", defsource->name, sink->name);
    m = pa_module_load(c, "module-loopback", args);

    if (m) {
        mi = pa_xnew(struct module_info, 1);
        mi->module = m->index;
        mi->args = args;

        pa_hashmap_put(u->hashmap, sink->name, mi);
    } else {
        pa_log_debug("Failed to loopback sink %s with args '%s'", sink->name, args);
        pa_xfree(args);
    }

    return PA_HOOK_OK;
}

static pa_hook_result_t source_put_hook_callback(pa_core *c, pa_source *source, void* userdata) {
    pa_source *defsource;
    pa_sink *defsink;
    struct userdata *u = userdata;
    const char *s;
    pa_module *m = NULL;
    char *args;
    struct module_info *mi;

    pa_assert(c);
    pa_assert(source);

    pa_log_debug("Source %s being created", source->name);

    mi = pa_hashmap_get(u->hashmap, source->name);
    if (mi) {
        pa_log_debug("Loopback already loaded for source  %s with args '%s'", source->name, mi->args);
        return PA_HOOK_OK;
    }

    /* Don't want to run during startup or shutdown */
    if (c->state != PA_CORE_RUNNING)
        return PA_HOOK_OK;

    /* Don't switch to any internal devices */
    if ((s = pa_proplist_gets(source->proplist, PA_PROP_DEVICE_BUS))) {
        if (pa_streq(s, "pci"))
            return PA_HOOK_OK;
        else if (pa_streq(s, "isa"))
            return PA_HOOK_OK;
    }

    /* Do not loopback default source over default sink */
    defsource = pa_namereg_get_default_source(c);
    if (defsource == source)
        return PA_HOOK_OK;

    defsink = pa_namereg_get_default_sink(c);
    if (!defsink) {
        pa_log_debug("Cannot find suitable sink for loopback from %s", source->name);
        return PA_HOOK_OK;
    }

    /* Load module-loopback with default source */
    args = pa_sprintf_malloc("source=\"%s\" sink=\"%s\" source_dont_move=\"true\"", source->name, defsink->name);
    m = pa_module_load(c, "module-loopback", args);

    if (m) {
        mi = pa_xnew(struct module_info, 1);
        mi->module = m->index;
        mi->args = args;

        pa_hashmap_put(u->hashmap, source->name, mi);
    } else {
        pa_log_debug("Failed to loopback source %s with args '%s'", source->name, args);
        pa_xfree(args);
    }

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

    m->userdata = u = pa_xnew(struct userdata, 1);

    /* A little bit later than module-rescue-streams... */
    u->sink_put_slot = pa_hook_connect(&m->core->hooks[PA_CORE_HOOK_SINK_PUT], PA_HOOK_LATE+30, (pa_hook_cb_t) sink_put_hook_callback, u);
    u->source_put_slot = pa_hook_connect(&m->core->hooks[PA_CORE_HOOK_SOURCE_PUT], PA_HOOK_LATE+20, (pa_hook_cb_t) source_put_hook_callback, u);
    u->card_profile_slot = pa_hook_connect(&m->core->hooks[PA_CORE_HOOK_SOURCE_PUT], PA_HOOK_LATE+20, (pa_hook_cb_t) source_put_hook_callback, u);
    u->hashmap = pa_hashmap_new(pa_idxset_string_hash_func, pa_idxset_string_compare_func);
    pa_modargs_free(ma);
    return 0;
}

void pa__done(pa_module*m) {
    struct userdata *u;

    pa_assert(m);

    if (!(u = m->userdata))
        return;

    if (u->sink_put_slot)
        pa_hook_slot_free(u->sink_put_slot);
    if (u->source_put_slot)
        pa_hook_slot_free(u->source_put_slot);

    if (u->hashmap) {
        struct module_info *mi;

        while ((mi = pa_hashmap_steal_first(u->hashmap))) {
            pa_xfree(mi->args);
            pa_xfree(mi);
        }

        pa_hashmap_free(u->hashmap, NULL, NULL);
    }

    pa_xfree(u);
}
