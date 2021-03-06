/*
 * snappy - 1.0
 *
 * Copyright (C) 2011-2014 Collabora Ltd.
 * Luis de Bethencourt <luis@debethencourt.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301
 * USA
 */

#define VERSION "1.0"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "user_interface.h"

#ifdef ENABLE_DBUS
#include "dlna.h"
#endif

#include "gst_engine.h"
#include "utils.h"


/*               Close snappy down               */
void
close_down (UserInterface * ui, GstEngine * engine)
{
  g_print ("closing snappy\n");

  /* Save position if file isn't finished playing */
  add_uri_unfinished (engine);

  /* Close gstreamer gracefully */
  change_state (engine, "Null");

  /* Re-enable screensaver */
  screensaver_enable (ui->screensaver, TRUE);
  screensaver_free (ui->screensaver);

  gst_object_unref (G_OBJECT (engine->player));
}


/*           Process command arguments           */
GList *
process_args (int argc, char *argv[],
    gboolean * blind, gboolean * fullscreen, gboolean * hide, gboolean * loop,
    gboolean * secret, gchar ** suburi, gboolean * tags,
    GOptionContext * context)
{
  gboolean recent = FALSE, version = FALSE;
  guint c, index, pos = 0;
  GList *uri_list = NULL;

  GOptionEntry entries[] = {
    {"blind", 'b', G_OPTION_FLAG_IN_MAIN, G_OPTION_ARG_NONE, blind,
        "Blind mode", NULL},
    {"fullscreen", 'f', G_OPTION_FLAG_IN_MAIN, G_OPTION_ARG_NONE, fullscreen,
        "Fullscreen mode", NULL},
    {"hide-controls", 'h', G_OPTION_FLAG_IN_MAIN, G_OPTION_ARG_NONE, hide,
        "Hide on screen controls", NULL},
    {"loop", 'l', G_OPTION_FLAG_IN_MAIN, G_OPTION_ARG_NONE, loop,
        "Looping mode", NULL},
    {"media-info", 'i', G_OPTION_FLAG_IN_MAIN, G_OPTION_ARG_NONE, tags,
        "Print media information", NULL},
    {"recent", 'r', G_OPTION_FLAG_IN_MAIN, G_OPTION_ARG_NONE, &recent,
        "Show recently viewed", NULL},
    {"secret", 's', G_OPTION_FLAG_IN_MAIN, G_OPTION_ARG_NONE, secret,
        "Views not saved in recently viewed history", NULL},
    {"subtitles", 't', G_OPTION_FLAG_IN_MAIN, G_OPTION_ARG_FILENAME,
        suburi, "Use this subtitle file", NULL},
    {"version", 'v', G_OPTION_FLAG_IN_MAIN, G_OPTION_ARG_NONE, &version,
        "Shows snappy's version", NULL},
    {NULL}
  };
  GError *err = NULL;

  g_option_context_add_main_entries (context, entries, NULL);
  g_option_context_add_group (context, gst_init_get_option_group ());
  g_option_context_add_group (context, clutter_get_option_group ());

  /* Check command arguments and update entry variables */
  if (!g_option_context_parse (context, &argc, &argv, &err)) {
    g_print ("Error initializing: %s\n", err->message);
    g_error_free (err);
    return NULL;
  }

  /* Recently viewed uris */
  if (recent) {
    gchar **recent = NULL;
    recent = get_recently_viewed ();

    if (recent) {
      g_print ("These are the recently viewed URIs: \n\n");

      for (c = 0; recent[c] != NULL; c++) {
        if (c < 9)
          g_print ("0%d: %s \n", c + 1, recent[c]);
        else
          g_print ("%d: %s \n", c + 1, recent[c]);
      }
    } else {
      g_print ("ERROR: Can't find history of recently viewed URIs\n");
    }

    return NULL;
  }

  /* Show snappy's version */
  if (version) {
    g_print ("snappy version %s\n", VERSION);
    return NULL;
  }

  /* Check that at least one URI has been introduced */
  if (argc > 1) {
    /* Save uris in the file glist */
    for (index = 1; index < argc; index++) {
      g_print ("Adding file: %s\n", argv[index]);
      uri_list = g_list_append (uri_list, clean_uri (argv[index]));
      pos++;
    }
  } else {
    /* If no files passed by user display help */
    g_print ("Opening snappy without content.\n\n");
    g_print ("%s", g_option_context_get_help (context, TRUE, NULL));
  }

  return uri_list;
}


/*            snappy's main function             */
int
main (int argc, char *argv[])
{
  UserInterface *ui = NULL;
  GstEngine *engine = NULL;
  ClutterActor *video_texture;
  ClutterGstVideoSink *sink;

  gboolean ok, blind = FALSE, fullscreen = FALSE, hide = FALSE, loop = FALSE;
  gboolean secret = FALSE, tags = FALSE;
  gint ret = 0;
  gchar *uri = NULL;
  gchar *suburi = NULL;
  GList *uri_list;
  GOptionContext *context;
  gchar *data_dir;

  ClutterInitError ci_err;

#ifdef ENABLE_DBUS
  SnappyMP *mp_obj = NULL;
#endif

  context = g_option_context_new ("<media file> - Play movie files");

  clutter_set_windowing_backend (CLUTTER_WINDOWING_X11);
  ci_err = gtk_clutter_init (&argc, &argv);
  if (ci_err != CLUTTER_INIT_SUCCESS)
    goto quit;

  /* Try to find the path for our resources in case snappy was relocated */
  data_dir = g_strdup (SNAPPY_DATA_DIR);
  if (!g_file_test (data_dir, G_FILE_TEST_EXISTS | G_FILE_TEST_IS_DIR)) {
    gchar *root_dir;

#ifdef G_OS_WIN32
    root_dir = g_win32_get_package_installation_directory_of_module (NULL);
#elif !defined(G_OS_UNIX)
    gchar *exec_path;
    gchar *bin_dir;

    exec_path = g_file_read_link ("/proc/self/exe", NULL);
    bin_dir = g_path_get_dirname (exec_path);
    root_dir = g_build_filename (bin_dir, "..", NULL);
    g_free (exec_path);
    g_free (bin_dir);
#else
    root_dir = NULL;
#endif
    if (root_dir != NULL) {
      data_dir = g_build_filename (root_dir, "share", "snappy", NULL);
      g_free (root_dir);
    }
  }

  /* Process command arguments */
  uri_list = process_args (argc, argv, &blind, &fullscreen, &hide,
      &loop, &secret, &suburi, &tags, context);

  gst_init (&argc, &argv);
  clutter_gst_init (NULL, NULL);

  /* User Interface */
  ui = g_new (UserInterface, 1);
  ui->uri_list = uri_list;
  ui->blind = blind;
  ui->fullscreen = fullscreen;
  ui->hide = hide;
  ui->tags = tags;
  ui->data_dir = data_dir;
  interface_init (ui);

  /* Gstreamer engine */
  engine = g_new (GstEngine, 1);
  sink = clutter_gst_video_sink_new ();
  if (sink == NULL) {
    g_print ("ERROR: Failed to create clutter-gst sink element\n");
    return FALSE;
  }
  video_texture = g_object_new (CLUTTER_TYPE_ACTOR, "content",
      g_object_new (CLUTTER_GST_TYPE_CONTENT, "sink", sink, NULL),
      "name", "texture", NULL);

  ok = engine_init (engine, sink);
  if (!ok)
    goto quit;

  engine->secret = secret;
  engine->loop = loop;

  ui->engine = engine;
  ui->texture = video_texture;

  gst_bus_add_watch (engine->bus, bus_call, ui);
  gst_object_unref (engine->bus);

  /* Get uri to load */
  if (uri_list) {
    uri = g_list_first (uri_list)->data;
    /* based on video filename we can guess subtitle file (.srt files only) */
    if (NULL == suburi) {
      gchar suburi_path_guessing[1024]; //buffer
      gchar *uri_no_extension = strip_filename_extension (uri);

      sprintf (suburi_path_guessing, "%s.srt", uri_no_extension);
      /* subtitle file exists, defaults for it */
      if (g_file_test (g_filename_from_uri (suburi_path_guessing, NULL, NULL),
              G_FILE_TEST_EXISTS))
        suburi = suburi_path_guessing;
    }
  }

  /* Load engine and start interface */
  engine_load_uri (engine, uri);
  interface_start (ui, uri);

  /* Load subtitle file if available */
  if (suburi != NULL) {
    suburi = clean_uri (suburi);
    set_subtitle_uri (engine, suburi);
  }

  /* Start playing if we have a URI to play */
  if (uri) {
    change_state (engine, "Paused");
    change_state (engine, "Playing");
  }
#ifdef ENABLE_DBUS
  /* Start MPRIS Dbus object */
  mp_obj = g_new (SnappyMP, 1);
  mp_obj->engine = engine;
  mp_obj->ui = ui;
  load_dlna (mp_obj);
#endif

  /* Main loop */
  gtk_main ();

  /* Close snappy */
  close_down (ui, engine);
#ifdef ENABLE_DBUS
  close_dlna (mp_obj);
#endif

quit:
  g_list_free (uri_list);
  g_option_context_free (context);

  return ret;
}
