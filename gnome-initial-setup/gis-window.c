/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright (C) 2012 Red Hat
 * Copyright (C) 2014 Endless Mobile, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * Written by:
 *     Jasper St. Pierre <jstpierre@mecheye.net>
 *     Cosimo Cecchi <cosimo@endlessm.com>
 */

#include "config.h"

#include "gis-assistant.h"
#include "gis-window.h"

struct _GisWindowPrivate {
  GisAssistant *assistant;
};
typedef struct _GisWindowPrivate GisWindowPrivate;

G_DEFINE_TYPE_WITH_PRIVATE(GisWindow, gis_window, GTK_TYPE_APPLICATION_WINDOW)

static void
gis_window_realize (GtkWidget *widget)
{
  GdkWindow *window;

  GTK_WIDGET_CLASS (gis_window_parent_class)->realize (widget);

  window = gtk_widget_get_window (widget);
  /* disable WM functions except move */
  gdk_window_set_functions (window, GDK_FUNC_MOVE);
}

static void
gis_window_class_init (GisWindowClass *klass)
{
  GtkWidgetClass *wclass = GTK_WIDGET_CLASS (klass);

  wclass->realize = gis_window_realize;
}

static void
gis_window_init (GisWindow *window)
{
  GisWindowPrivate *priv = gis_window_get_instance_private (window);
  GdkGeometry size_hints;

  size_hints.min_width = 747;
  size_hints.min_height = 539;
  size_hints.max_width = 747;
  size_hints.max_height = 539;
  size_hints.win_gravity = GDK_GRAVITY_CENTER;

  gtk_window_set_geometry_hints (GTK_WINDOW (window),
                                 GTK_WIDGET (window),
                                 &size_hints,
                                 GDK_HINT_MIN_SIZE | GDK_HINT_MAX_SIZE | GDK_HINT_WIN_GRAVITY);

  priv->assistant = g_object_new (GIS_TYPE_ASSISTANT, NULL);
  gtk_container_add (GTK_CONTAINER (window), GTK_WIDGET (priv->assistant));
  gtk_widget_show (GTK_WIDGET (priv->assistant));
  gtk_window_set_titlebar (GTK_WINDOW (window), gis_assistant_get_titlebar (priv->assistant));
}

GisAssistant *
gis_window_get_assistant (GisWindow *window)
{
  GisWindowPrivate *priv;

  g_return_val_if_fail (GIS_IS_WINDOW (window), NULL);

  priv = gis_window_get_instance_private (window);
  return priv->assistant;
}

GtkWidget *
gis_window_new (GisDriver *driver)
{
  return g_object_new (GIS_TYPE_WINDOW,
		       "application", driver,
		       "type", GTK_WINDOW_TOPLEVEL,
		       "border-width", 12,
		       "icon-name", "preferences-system",
		       "resizable", TRUE,
		       "window-position", GTK_WIN_POS_CENTER_ALWAYS,
		       "deletable", FALSE,
		       NULL);
}
