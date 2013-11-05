/*-*- Mode: C; c-basic-offset: 8; indent-tabs-mode: nil -*-*/

/***
  This file is part of systemd.

  Copyright 2011 Lennart Poettering

  systemd is free software; you can redistribute it and/or modify it
  under the terms of the GNU Lesser General Public License as published by
  the Free Software Foundation; either version 2.1 of the License, or
  (at your option) any later version.

  systemd is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public License
  along with systemd; If not, see <http://www.gnu.org/licenses/>.
***/

#include <errno.h>
#include <string.h>

#include "strv.h"
#include "bus-util.h"

#include "logind.h"
#include "logind-user.h"

static int property_get_display(
                sd_bus *bus,
                const char *path,
                const char *interface,
                const char *property,
                sd_bus_message *reply,
                sd_bus_error *error,
                void *userdata) {

        _cleanup_free_ char *p = NULL;
        User *u = userdata;

        assert(bus);
        assert(reply);
        assert(u);

        p = u->display ? session_bus_path(u->display) : strdup("/");
        if (!p)
                return -ENOMEM;

        return sd_bus_message_append(reply, "(so)", u->display ? u->display->id : "", p);
}

static int property_get_state(
                sd_bus *bus,
                const char *path,
                const char *interface,
                const char *property,
                sd_bus_message *reply,
                sd_bus_error *error,
                void *userdata) {

        User *u = userdata;

        assert(bus);
        assert(reply);
        assert(u);

        return sd_bus_message_append(reply, "s", user_state_to_string(user_get_state(u)));
}

static int property_get_sessions(
                sd_bus *bus,
                const char *path,
                const char *interface,
                const char *property,
                sd_bus_message *reply,
                sd_bus_error *error,
                void *userdata) {

        User *u = userdata;
        Session *session;
        int r;

        assert(bus);
        assert(reply);
        assert(u);

        r = sd_bus_message_open_container(reply, 'a', "(so)");
        if (r < 0)
                return r;

        LIST_FOREACH(sessions_by_user, session, u->sessions) {
                _cleanup_free_ char *p = NULL;

                p = session_bus_path(session);
                if (!p)
                        return -ENOMEM;

                r = sd_bus_message_append(reply, "(so)", session->id, p);
                if (r < 0)
                        return r;

        }

        r = sd_bus_message_close_container(reply);
        if (r < 0)
                return r;

        return 1;
}

static int property_get_idle_hint(
                sd_bus *bus,
                const char *path,
                const char *interface,
                const char *property,
                sd_bus_message *reply,
                sd_bus_error *error,
                void *userdata) {

        User *u = userdata;

        assert(bus);
        assert(reply);
        assert(u);

        return sd_bus_message_append(reply, "b", user_get_idle_hint(u, NULL) > 0);
}

static int property_get_idle_since_hint(
                sd_bus *bus,
                const char *path,
                const char *interface,
                const char *property,
                sd_bus_message *reply,
                sd_bus_error *error,
                void *userdata) {

        User *u = userdata;
        dual_timestamp t;
        uint64_t k;

        assert(bus);
        assert(reply);
        assert(u);

        user_get_idle_hint(u, &t);
        k = streq(property, "IdleSinceHint") ? t.realtime : t.monotonic;

        return sd_bus_message_append(reply, "t", k);
}

static int method_terminate(sd_bus *bus, sd_bus_message *message, void *userdata) {
        User *u = userdata;
        int r;

        assert(bus);
        assert(message);
        assert(u);

        r = user_stop(u);
        if (r < 0)
                return sd_bus_reply_method_errno(bus, message, r, NULL);

        return sd_bus_reply_method_return(bus, message, NULL);
}

static int method_kill(sd_bus *bus, sd_bus_message *message, void *userdata) {
        User *u = userdata;
        int32_t signo;
        int r;

        assert(bus);
        assert(message);
        assert(u);

        r = sd_bus_message_read(message, "i", &signo);
        if (r < 0)
                return sd_bus_reply_method_errno(bus, message, r, NULL);

        if (signo <= 0 || signo >= _NSIG)
                return sd_bus_reply_method_errorf(bus, message, SD_BUS_ERROR_INVALID_ARGS, "Invalid signal %i", signo);

        r = user_kill(u, signo);
        if (r < 0)
                return sd_bus_reply_method_errno(bus, message, r, NULL);

        return sd_bus_reply_method_return(bus, message, NULL);
}

const sd_bus_vtable user_vtable[] = {
        SD_BUS_VTABLE_START(0),

        SD_BUS_PROPERTY("UID", "u", bus_property_get_uid, offsetof(User, uid), 0),
        SD_BUS_PROPERTY("GID", "u", bus_property_get_gid, offsetof(User, gid), 0),
        SD_BUS_PROPERTY("Name", "s", NULL, offsetof(User, name), 0),
        SD_BUS_PROPERTY("Timestamp", "t", NULL, offsetof(User, timestamp.realtime), 0),
        SD_BUS_PROPERTY("TimestampMonotonic", "t", NULL, offsetof(User, timestamp.monotonic), 0),
        SD_BUS_PROPERTY("RuntimePath", "s", NULL, offsetof(User, runtime_path), 0),
        SD_BUS_PROPERTY("Service", "s", NULL, offsetof(User, service), 0),
        SD_BUS_PROPERTY("Slice", "s", NULL, offsetof(User, slice), 0),
        SD_BUS_PROPERTY("Display", "(so)", property_get_display, 0, 0),
        SD_BUS_PROPERTY("State", "s", property_get_state, 0, 0),
        SD_BUS_PROPERTY("Sessions", "a(so)", property_get_sessions, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
        SD_BUS_PROPERTY("IdleHint", "b", property_get_idle_hint, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
        SD_BUS_PROPERTY("IdleSinceHint", "t", property_get_idle_since_hint, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
        SD_BUS_PROPERTY("IdleSinceHintMonotonic", "t", property_get_idle_since_hint, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),

        SD_BUS_METHOD("Terminate", NULL, NULL, method_terminate, 0),
        SD_BUS_METHOD("Kill", "i", NULL, method_kill, 0),

        SD_BUS_VTABLE_END
};

int user_object_find(sd_bus *bus, const char *path, const char *interface, void **found, void *userdata) {

        _cleanup_free_ char *e = NULL;
        Manager *m = userdata;
        unsigned long lu;
        const char *p;
        User *user;
        int r;

        assert(bus);
        assert(path);
        assert(interface);
        assert(found);
        assert(m);

        p = startswith(path, "/org/freedesktop/login1/user/_");
        if (!p)
                return 0;

        r = safe_atolu(p, &lu);
        if (r < 0)
                return 0;

        user = hashmap_get(m->users, ULONG_TO_PTR(lu));
        if (!user)
                return 0;

        *found = user;
        return 1;
}

char *user_bus_path(User *u) {
        char *s;

        assert(u);

        if (asprintf(&s, "/org/freedesktop/login1/user/_%llu", (unsigned long long) u->uid) < 0)
                return NULL;

        return s;
}

int user_node_enumerator(sd_bus *bus, const char *path, char ***nodes, void *userdata) {
        _cleanup_strv_free_ char **l = NULL;
        Manager *m = userdata;
        User *user;
        Iterator i;
        int r;

        assert(bus);
        assert(path);
        assert(nodes);

        HASHMAP_FOREACH(user, m->users, i) {
                char *p;

                p = user_bus_path(user);
                if (!p)
                        return -ENOMEM;

                r = strv_push(&l, p);
                if (r < 0) {
                        free(p);
                        return r;
                }
        }

        *nodes = l;
        l = NULL;

        return 1;
}

int user_send_signal(User *u, bool new_user) {
        _cleanup_free_ char *p = NULL;

        assert(u);

        p = user_bus_path(u);
        if (!p)
                return -ENOMEM;

        return sd_bus_emit_signal(
                        u->manager->bus,
                        "/org/freedesktop/login1",
                        "org.freedesktop.login1.Manager",
                        new_user ? "UserNew" : "UserRemoved",
                        "uo", (uint32_t) u->uid, p);
}

int user_send_changed(User *u, const char *properties, ...) {
        _cleanup_free_ char *p = NULL;
        char **l;

        assert(u);

        if (!u->started)
                return 0;

        p = user_bus_path(u);
        if (!p)
                return -ENOMEM;

        l = strv_from_stdarg_alloca(properties);

        return sd_bus_emit_properties_changed_strv(u->manager->bus, p, "org.freedesktop.login1.User", l);
}
