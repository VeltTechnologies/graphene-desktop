/*
 * This file is part of graphene-desktop, the desktop environment of VeltOS.
 * Copyright (C) 2016 Velt Technologies, Aidan Shafran <zelbrium@gmail.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * 
 *     http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "clock-applet.h"
#include <glib.h>

#define TIME_FORMAT_STRING_LENGTH 25

struct _GrapheneClockApplet
{
  GtkBox parent;
  
  GSettings *interfaceSettings;
  GSource *source;
  gchar *timeFormat;
  gchar *timeString;
};


static void graphene_clock_applet_finalize(GObject *self_);
static void on_interface_settings_changed(GrapheneClockApplet *self, gchar *key, GSettings *settings);
static gboolean update(GSource *source, GSourceFunc callback, gpointer userdata);


G_DEFINE_TYPE(GrapheneClockApplet, graphene_clock_applet, GTK_TYPE_LABEL)


GrapheneClockApplet* graphene_clock_applet_new(void)
{
  return GRAPHENE_CLOCK_APPLET(g_object_new(GRAPHENE_TYPE_CLOCK_APPLET, NULL));
}

static void graphene_clock_applet_class_init(GrapheneClockAppletClass *klass)
{
  GObjectClass *gobjectClass = G_OBJECT_CLASS(klass);
  gobjectClass->finalize = graphene_clock_applet_finalize;
}

static void graphene_clock_applet_init(GrapheneClockApplet *self)
{
  gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(self)), "graphene-clock-applet");

  self->timeString = NULL;
  self->timeFormat = g_new(gchar, TIME_FORMAT_STRING_LENGTH);
  self->timeFormat[0] = '\0'; // Empty string
  
  self->interfaceSettings = g_settings_new("org.gnome.desktop.interface");
  g_signal_connect_swapped(self->interfaceSettings, "changed", G_CALLBACK(on_interface_settings_changed), self);
  on_interface_settings_changed(self, "clock-", self->interfaceSettings);
  
  static GSourceFuncs funcs = { NULL, NULL, update, NULL };
  self->source = g_source_new(&funcs, sizeof(GSource));
  g_source_set_callback(self->source, NULL, self, NULL); // Sets the userdata passed to update - the callback itself is ignored
  g_source_set_ready_time(self->source, 0);
  g_source_attach(self->source, NULL);
}

static void graphene_clock_applet_finalize(GObject *self_)
{
  GrapheneClockApplet *self = GRAPHENE_CLOCK_APPLET(self);
  g_object_unref(self->interfaceSettings);
  g_source_destroy(self->source);
  self->source = NULL;
  g_free(self->timeFormat);
  g_free(self->timeString);
}

static void on_interface_settings_changed(GrapheneClockApplet *self, gchar *key, GSettings *settings)
{
  if(g_str_has_prefix(key, "clock-"))
  {
    int format = g_settings_get_enum(settings, "clock-format");
    gboolean showDate = g_settings_get_boolean(settings, "clock-show-date");
    gboolean showSeconds = g_settings_get_boolean(settings, "clock-show-seconds");
    
    self->timeFormat[0] = '\0';
    
    if(showDate)
      g_strlcat(self->timeFormat, "%a %b %e ", TIME_FORMAT_STRING_LENGTH); // Mon Jan 1
    if(format == 1) // 12 hour time
      g_strlcat(self->timeFormat, "%l", TIME_FORMAT_STRING_LENGTH); // 5
    else
      g_strlcat(self->timeFormat, "%H", TIME_FORMAT_STRING_LENGTH); // 17
    g_strlcat(self->timeFormat, ":%M", TIME_FORMAT_STRING_LENGTH); // :30
    if(showSeconds)
      g_strlcat(self->timeFormat, ":%S", TIME_FORMAT_STRING_LENGTH); // :55
    if(format == 1)
      g_strlcat(self->timeFormat, " %p", TIME_FORMAT_STRING_LENGTH); // PM
    
    if(self->source)
      g_source_set_ready_time(self->source, 0); // Update label now
  }
}

static gboolean update(GSource *source, GSourceFunc callback, gpointer userdata)
{
  GrapheneClockApplet *self = GRAPHENE_CLOCK_APPLET(userdata);
  
  // Get the time as a formatted string
  GDateTime *dt = g_date_time_new_now_local();
  gchar *formatted = g_date_time_format(dt, self->timeFormat);
  g_date_time_unref(dt);
  
  // Don't call set_text unless the string changed
  if(g_strcmp0(formatted, self->timeString) == 0)
  {
    g_free(formatted);
  }
  else
  {
    g_clear_pointer(&self->timeString, g_free);
    self->timeString = formatted;
    gtk_label_set_text(GTK_LABEL(self), self->timeString);
  }
  
  // Get monotonic time of the start of the next second
  // This keeps it from falling out of sync
  gint64 realNow = g_get_real_time(); // wall-clock time
  gint64 usUntilNextSecond = G_USEC_PER_SEC - (realNow - ((realNow / G_USEC_PER_SEC) * G_USEC_PER_SEC));
  usUntilNextSecond = CLAMP(usUntilNextSecond, 0, G_USEC_PER_SEC);
  gint64 updateTime = g_source_get_time(source) + usUntilNextSecond; // monotonic time

  // Set source to dispatch at the next second
  g_source_set_ready_time(source, updateTime);
  return G_SOURCE_CONTINUE;
}