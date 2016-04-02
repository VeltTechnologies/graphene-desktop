/*
 * graphene-desktop
 * Copyright (C) 2016 Velt Technologies, Aidan Shafran <zelbrium@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "wm.h"
#include "background.h"
#include <meta/meta-shadow-factory.h>

// VosWM class (private)
struct _VosWM {
  MetaPlugin parent;
  MetaBackgroundGroup *BackgroundGroup;
};

// Structs copied from meta-shadow-factory.c (commit a191554 on Jul 6, 2015)
typedef struct
{
  GObject parent_instance;
  GHashTable *shadows;
  GHashTable *shadow_classes;
} _MetaShadowFactory;
typedef struct
{
  const char *name;
  MetaShadowParams focused;
  MetaShadowParams unfocused;
} _MetaShadowClassInfo;



int main(int argc, char **argv)
{
  meta_plugin_manager_set_plugin_type(VOS_TYPE_WM);
  meta_set_wm_name("Mutter(VOS Desktop)");
  meta_set_gnome_wm_keybindings("Mutter,GNOME Shell");
  
  g_setenv("NO_GAIL", "1", TRUE);
  g_setenv("NO_AT_BRIDGE", "1", TRUE);
  meta_init();
  g_unsetenv("NO_AT_BRIDGE");
  g_unsetenv("NO_GAIL");
  
  return meta_run();
}

static void vos_wm_dispose(GObject *gobject); 
static void start(MetaPlugin *plugin);
static void on_monitors_changed(MetaScreen *screen, MetaPlugin *plugin);
static void minimize(MetaPlugin *wm, MetaWindowActor *actor);
static void unminimize(MetaPlugin *wm, MetaWindowActor *actor);
static void map(MetaPlugin *plugin, MetaWindowActor *actor);
static void destroy(MetaPlugin *plugin, MetaWindowActor *actor);
static const MetaPluginInfo * plugin_info(MetaPlugin *plugin);
// static void launch_rundialog(MetaDisplay *display, MetaScreen *screen,
//                      MetaWindow *window, ClutterKeyEvent *event,
//                      MetaKeyBinding *binding);
                     
G_DEFINE_TYPE (VosWM, vos_wm, META_TYPE_PLUGIN);

static void vos_wm_class_init(VosWMClass *klass)
{
  MetaPluginClass *object_class = META_PLUGIN_CLASS(klass);
  object_class->start = start;
  object_class->minimize = minimize;
  object_class->unminimize = unminimize;
  object_class->map = map;
  object_class->destroy = destroy;
  object_class->plugin_info = plugin_info;
  
  GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
  gobject_class->dispose = vos_wm_dispose;
}

VosWM* vos_wm_new(void)
{
  return VOS_WM(g_object_new(VOS_TYPE_WM, NULL));
}

static void vos_wm_init(VosWM *wm)
{
  
}

static void vos_wm_dispose(GObject *gobject)
{
  g_signal_handlers_disconnect_by_func(meta_plugin_get_screen(META_PLUGIN(gobject)), on_monitors_changed, VOS_WM(gobject));
  g_clear_object(&VOS_WM(gobject)->BackgroundGroup);
  G_OBJECT_CLASS(vos_wm_parent_class)->dispose(gobject);
}



static void start(MetaPlugin *plugin)
{
  MetaScreen *screen = meta_plugin_get_screen(plugin);
  ClutterActor *screenGroup = meta_get_window_group_for_screen(screen);
  ClutterActor *stage = meta_get_stage_for_screen(screen);
  
  ClutterActor *backgroundGroup = meta_background_group_new();
  VOS_WM(plugin)->BackgroundGroup = META_BACKGROUND_GROUP(backgroundGroup);
  clutter_actor_set_reactive(backgroundGroup, TRUE);
  clutter_actor_insert_child_below(screenGroup, backgroundGroup, NULL);
  
  g_signal_connect(screen, "monitors_changed", G_CALLBACK(on_monitors_changed), plugin);
  on_monitors_changed(screen, plugin);
  
  clutter_actor_show(backgroundGroup);
  clutter_actor_show(screenGroup);
  clutter_actor_show(stage);
  
  // meta_keybindings_set_custom_handler("panel-main-menu", launch_rundialog);
  // meta_keybindings_set_custom_handler("switch-windows", switch_windows);
  // meta_keybindings_set_custom_handler("switch-applications", switch_windows);
  
  
  
  /*
  The shadow factory has a bug which causes new shadow classes to not only not be created, but also
  corrupt the "normal" class. The only way I was able to fix this is by directly modifying the factory's hash
  table via private member variables. However, the private interface has remained the same for six years,
  so it's probably safe for a while...
  
  The bug is (I think):
    class_info->name = g_strdup (class_info->name);     on line 830 of meta-shadow-factory.c
  should be
    class_info->name = g_strdup (class_name);

  TODO: Maybe submit a bug report? Or something.
  */
  // Add a shadow class for the panel
  MetaShadowParams dockShadow = {3, -1, 0, 0, 200}; // radius, top_fade, x_offset, y_offset, opacity
  _MetaShadowFactory *factory = (_MetaShadowFactory*)meta_shadow_factory_get_default();
  _MetaShadowClassInfo *info = g_slice_new0(_MetaShadowClassInfo);
  info->name = "dock";
  info->focused = dockShadow;
  info->unfocused = dockShadow;
  g_hash_table_insert(factory->shadow_classes, "dock", info);
}

// static void launch_rundialog(MetaDisplay *display, MetaScreen *screen,
//                      MetaWindow *window, ClutterKeyEvent *event,
//                      MetaKeyBinding *binding)
// {
//   // printf("mainmenu!\n");
// }

static void on_monitors_changed(MetaScreen *screen, MetaPlugin *plugin)
{
  ClutterActor *backgroundGroup = CLUTTER_ACTOR(VOS_WM(plugin)->BackgroundGroup);
  clutter_actor_destroy_all_children(backgroundGroup);
  
  gint numMonitors = meta_screen_get_n_monitors(screen);
  for(int i=0;i<numMonitors;++i)
    clutter_actor_add_child(backgroundGroup, CLUTTER_ACTOR(vos_wm_background_new(screen, i)));
}

static void minimize_done(ClutterActor *actor, MetaPlugin *plugin)
{
  // End transition
  clutter_actor_remove_all_transitions(actor);
  g_signal_handlers_disconnect_by_func(actor, minimize_done, plugin);
  clutter_actor_set_scale(actor, 1, 1);
  clutter_actor_hide(actor); // Actually hide the window
  
  // Must call to complete the minimization
  meta_plugin_minimize_completed(plugin, META_WINDOW_ACTOR(actor));
}

static void minimize(MetaPlugin *plugin, MetaWindowActor *windowActor)
{
  ClutterActor *actor = CLUTTER_ACTOR(windowActor);
  
  // Get the minimized position
  MetaWindow *window = meta_window_actor_get_meta_window(windowActor);
  MetaRectangle rect = meta_rect(0,0,0,0);
  meta_window_get_icon_geometry(window, &rect); // This is set by the Launcher applet
  // printf("%i, %i, %i, %i\n", rect.x, rect.y, rect.width, rect.height);
  
  // Ease the window into its minimized position
  clutter_actor_set_pivot_point(actor, 0, 0);
  clutter_actor_save_easing_state(actor);
  clutter_actor_set_easing_mode(actor, CLUTTER_EASE_IN_SINE);
  clutter_actor_set_easing_duration(actor, 200);
  g_signal_connect(actor, "transitions_completed", G_CALLBACK(minimize_done), plugin);
  clutter_actor_set_x(actor, rect.x);
  clutter_actor_set_y(actor, rect.y);
  clutter_actor_set_scale(actor, rect.width/clutter_actor_get_width(actor), rect.height/clutter_actor_get_height(actor));
  clutter_actor_restore_easing_state(actor);
}

static void unminimize_done(ClutterActor *actor, MetaPlugin *plugin)
{
  clutter_actor_remove_all_transitions(actor);
  g_signal_handlers_disconnect_by_func(actor, unminimize_done, plugin);
  meta_plugin_unminimize_completed(plugin, META_WINDOW_ACTOR(actor));
}

static void unminimize(MetaPlugin *plugin, MetaWindowActor *windowActor)
{
  ClutterActor *actor = CLUTTER_ACTOR(windowActor);

  // Get the unminimized position
  gint x = clutter_actor_get_x(actor);
  gint y = clutter_actor_get_y(actor);
  
  // Move the window to it's minimized position and scale
  MetaWindow *window = meta_window_actor_get_meta_window(windowActor);
  MetaRectangle rect = meta_rect(0,0,0,0);
  meta_window_get_icon_geometry(window, &rect);
  clutter_actor_set_x(actor, rect.x);
  clutter_actor_set_y(actor, rect.y);
  clutter_actor_set_scale(actor, rect.width/clutter_actor_get_width(actor), rect.height/clutter_actor_get_height(actor));
  clutter_actor_show(actor);
  
  // Ease it into its unminimized position
  clutter_actor_set_pivot_point(actor, 0, 0);
  clutter_actor_save_easing_state(actor);
  clutter_actor_set_easing_mode(actor, CLUTTER_EASE_OUT_SINE);
  clutter_actor_set_easing_duration(actor, 200);
  g_signal_connect(actor, "transitions_completed", G_CALLBACK(unminimize_done), plugin);
  clutter_actor_set_x(actor, x);
  clutter_actor_set_y(actor, y);
  clutter_actor_set_scale(actor, 1, 1);
  clutter_actor_restore_easing_state(actor);
}

static void destroy_done(ClutterActor *actor, MetaPlugin *plugin)
{
  clutter_actor_remove_all_transitions(actor);
  g_signal_handlers_disconnect_by_func(actor, destroy_done, plugin);
  meta_plugin_destroy_completed(plugin, META_WINDOW_ACTOR(actor));
}

static void destroy(MetaPlugin *plugin, MetaWindowActor *windowActor)
{
  ClutterActor *actor = CLUTTER_ACTOR(windowActor);

  clutter_actor_remove_all_transitions(actor);
  MetaWindow *window = meta_window_actor_get_meta_window(windowActor);

  switch(meta_window_get_window_type(window))
  {
    case META_WINDOW_NORMAL:
      // clutter_actor_set_pivot_point(actor, 1, 0);
      // clutter_actor_save_easing_state(actor);
      // clutter_actor_set_easing_mode(actor, CLUTTER_LINEAR);
      // clutter_actor_set_easing_duration(actor, 200);
      // g_signal_connect(actor, "transitions_completed", G_CALLBACK(destroy_done), plugin);
      // clutter_actor_set_scale(actor, 0, 0);
      // clutter_actor_restore_easing_state(actor);
      // break;
      
    case META_WINDOW_NOTIFICATION:
    case META_WINDOW_DIALOG:
    case META_WINDOW_MODAL_DIALOG:
      clutter_actor_set_pivot_point(actor, 0.5, 0.5);
      clutter_actor_save_easing_state(actor);
      clutter_actor_set_easing_mode(actor, CLUTTER_EASE_OUT_QUAD);
      clutter_actor_set_easing_duration(actor, 200);
      g_signal_connect(actor, "transitions_completed", G_CALLBACK(destroy_done), plugin);
      clutter_actor_set_scale(actor, 0, 0);
      clutter_actor_restore_easing_state(actor);
      break;
      
    case META_WINDOW_MENU:
    case META_WINDOW_DOCK:
      // clutter_actor_set_pivot_point(actor, 0, 1);
      // clutter_actor_save_easing_state(actor);
      // clutter_actor_set_easing_mode(actor, CLUTTER_LINEAR);
      // clutter_actor_set_easing_duration(actor, 100);
      // g_signal_connect(actor, "transitions_completed", G_CALLBACK(destroy_done), plugin);
      // clutter_actor_set_scale(actor, 1, 0);
      // clutter_actor_restore_easing_state(actor);
      // break;
      // 
    default:
      meta_plugin_destroy_completed(plugin, META_WINDOW_ACTOR(actor));
  }
}

static void map_done(ClutterActor *actor, MetaPlugin *plugin)
{
  clutter_actor_remove_all_transitions(actor);
  g_signal_handlers_disconnect_by_func(actor, map_done, plugin);
  meta_plugin_map_completed(plugin, META_WINDOW_ACTOR(actor));
}

static void map(MetaPlugin *plugin, MetaWindowActor *windowActor)
{
  ClutterActor *actor = CLUTTER_ACTOR(windowActor);

  clutter_actor_remove_all_transitions(actor);
  MetaWindow *window = meta_window_actor_get_meta_window(windowActor);

  switch(meta_window_get_window_type(window))
  {
    case META_WINDOW_NORMAL:
      // clutter_actor_set_pivot_point(actor, 1, 0);
      // clutter_actor_save_easing_state(actor);
      // clutter_actor_set_easing_mode(actor, CLUTTER_LINEAR);
      // clutter_actor_set_easing_duration(actor, 200);
      // g_signal_connect(actor, "transitions_completed", G_CALLBACK(destroy_done), plugin);
      // clutter_actor_set_scale(actor, 0, 0);
      // clutter_actor_restore_easing_state(actor);
      // break;
      
    case META_WINDOW_NOTIFICATION:
    case META_WINDOW_DIALOG:
    case META_WINDOW_MODAL_DIALOG:
      clutter_actor_set_pivot_point(actor, 0.5, 0.5);
      clutter_actor_set_scale(actor, 0, 0);
      clutter_actor_show(actor);
      clutter_actor_save_easing_state(actor);
      clutter_actor_set_easing_mode(actor, CLUTTER_EASE_IN_QUAD);
      clutter_actor_set_easing_duration(actor, 200);
      g_signal_connect(actor, "transitions_completed", G_CALLBACK(map_done), plugin);
      clutter_actor_set_scale(actor, 1, 1);
      clutter_actor_restore_easing_state(actor);
      break;
      
    case META_WINDOW_MENU:
    case META_WINDOW_DOCK:
      // clutter_actor_set_pivot_point(actor, 0, 1);
      // clutter_actor_save_easing_state(actor);
      // clutter_actor_set_easing_mode(actor, CLUTTER_LINEAR);
      // clutter_actor_set_easing_duration(actor, 100);
      // g_signal_connect(actor, "transitions_completed", G_CALLBACK(destroy_done), plugin);
      // clutter_actor_set_scale(actor, 1, 0);
      // clutter_actor_restore_easing_state(actor);
      // break;

    default:
      meta_plugin_map_completed(plugin, META_WINDOW_ACTOR(actor));
  }
  
  if(g_strcmp0(meta_window_get_role(window), "GrapheneDock") == 0 || g_strcmp0(meta_window_get_role(window), "GraphenePopup") == 0)
  {
    g_object_set(windowActor, "shadow-mode", META_SHADOW_MODE_FORCED_ON, "shadow-class", "dock", NULL);
  }
}


static const MetaPluginInfo * plugin_info(MetaPlugin *plugin)
{
  static const MetaPluginInfo info = {
    .name = "Graphene Window Manager",
    .version = "1.0.0",
    .author = "Velt (Aidan Shafran)",
    .license = "GPLv3",
    .description = "Graphene Window Manager for VeltOS"
  };
  
  return &info;
}