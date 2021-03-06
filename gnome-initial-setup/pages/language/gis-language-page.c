/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright (C) 2012 Red Hat
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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * Written by:
 *     Jasper St. Pierre <jstpierre@mecheye.net>
 *     Michael Wood <michael.g.wood@intel.com>
 *
 * Based on gnome-control-center cc-region-panel.c
 */

/* Language page {{{1 */

#define PAGE_ID "language"

#define GNOME_SYSTEM_LOCALE_DIR "org.gnome.system.locale"
#define REGION_KEY "region"

#include "config.h"
#include "language-resources.h"
#include "gis-welcome-widget.h"
#include "cc-language-chooser.h"
#include "gis-page-util.h"
#include "gis-language-page.h"

#include <act/act-user-manager.h>
#include <polkit/polkit.h>
#include <locale.h>
#include <gtk/gtk.h>

struct _GisLanguagePagePrivate
{
  GtkWidget *logo;
  GtkWidget *welcome_widget;
  GtkWidget *language_chooser;

  GDBusProxy *localed;
  GPermission *permission;
  const gchar *new_locale_id;

  GCancellable *cancellable;

  GtkAccelGroup *accel_group;
};
typedef struct _GisLanguagePagePrivate GisLanguagePagePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (GisLanguagePage, gis_language_page, GIS_TYPE_PAGE);

static void
set_localed_locale (GisLanguagePage *self)
{
  GisLanguagePagePrivate *priv = gis_language_page_get_instance_private (self);
  GVariantBuilder *b;
  gchar *s;

  b = g_variant_builder_new (G_VARIANT_TYPE ("as"));
  s = g_strconcat ("LANG=", priv->new_locale_id, NULL);
  g_variant_builder_add (b, "s", s);
  g_free (s);

  g_dbus_proxy_call (priv->localed,
                     "SetLocale",
                     g_variant_new ("(asb)", b, TRUE),
                     G_DBUS_CALL_FLAGS_NONE,
                     -1, NULL, NULL, NULL);
  g_variant_builder_unref (b);
}

static void
change_locale_permission_acquired (GObject      *source,
                                   GAsyncResult *res,
                                   gpointer      data)
{
  GisLanguagePage *page = GIS_LANGUAGE_PAGE (data);
  GisLanguagePagePrivate *priv = gis_language_page_get_instance_private (page);
  GError *error = NULL;
  gboolean allowed;

  allowed = g_permission_acquire_finish (priv->permission, res, &error);
  if (error) {
      if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        g_warning ("Failed to acquire permission: %s\n", error->message);
      g_error_free (error);
      return;
  }

  if (allowed)
    set_localed_locale (page);
}

static void
user_loaded (GObject    *object,
             GParamSpec *pspec,
             gpointer    user_data)
{
  gchar *new_locale_id = user_data;

  act_user_set_language (ACT_USER (object), new_locale_id);

  g_free (new_locale_id);
}

static void
language_changed (CcLanguageChooser  *chooser,
                  GParamSpec         *pspec,
                  GisLanguagePage    *page)
{
  GisLanguagePagePrivate *priv = gis_language_page_get_instance_private (page);
  GisDriver *driver;
  GSettings *region_settings;
  ActUser *user;

  priv->new_locale_id = cc_language_chooser_get_language (chooser);
  driver = GIS_PAGE (page)->driver;

  setlocale (LC_MESSAGES, priv->new_locale_id);
  setlocale (LC_TIME, priv->new_locale_id);
  gtk_widget_set_default_direction (gtk_get_locale_direction ());

  /* gis spawns processes that also need to be localised */
  g_setenv ("LC_MESSAGES", priv->new_locale_id, TRUE);
  g_setenv ("LC_TIME", priv->new_locale_id, TRUE);

  if (gis_driver_get_mode (driver) == GIS_DRIVER_MODE_NEW_USER) {
      if (g_permission_get_allowed (priv->permission)) {
          set_localed_locale (page);
      }
      else if (g_permission_get_can_acquire (priv->permission)) {
          g_permission_acquire_async (priv->permission,
                                      NULL,
                                      change_locale_permission_acquired,
                                      page);
      }
  }

  /* Ensure we won't override the selected language for format strings */
  region_settings = g_settings_new (GNOME_SYSTEM_LOCALE_DIR);
  g_settings_reset (region_settings, REGION_KEY);
  g_object_unref (region_settings);

  user = act_user_manager_get_user (act_user_manager_get_default (),
                                    g_get_user_name ());
  if (act_user_is_loaded (user))
    act_user_set_language (user, priv->new_locale_id);
  else
    g_signal_connect (user,
                      "notify::is-loaded",
                      G_CALLBACK (user_loaded),
                      g_strdup (priv->new_locale_id));

  gis_driver_set_user_language (driver, priv->new_locale_id);

  gis_welcome_widget_show_locale (GIS_WELCOME_WIDGET (priv->welcome_widget),
                                  priv->new_locale_id);

  gis_driver_locale_changed (driver);
}

static void
localed_proxy_ready (GObject      *source,
                     GAsyncResult *res,
                     gpointer      data)
{
  GisLanguagePage *self = data;
  GisLanguagePagePrivate *priv = gis_language_page_get_instance_private (self);
  GDBusProxy *proxy;
  GError *error = NULL;

  proxy = g_dbus_proxy_new_finish (res, &error);

  if (!proxy) {
      if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        g_warning ("Failed to contact localed: %s\n", error->message);
      g_error_free (error);
      return;
  }

  priv->localed = proxy;
}

static char *
get_item (const char *buffer, const char *name)
{
  char *label, *start, *end, *result;
  char end_char;

  result = NULL;
  start = NULL;
  end = NULL;
  label = g_strconcat (name, "=", NULL);
  if ((start = strstr (buffer, label)) != NULL)
    {
      start += strlen (label);
      end_char = '\n';
      if (*start == '"')
        {
          start++;
          end_char = '"';
        }

      end = strchr (start, end_char);
    }

    if (start != NULL && end != NULL)
      {
        result = g_strndup (start, end - start);
      }

  g_free (label);

  return result;
}

static void
update_distro_logo (GisLanguagePage *page)
{
  GisLanguagePagePrivate *priv = gis_language_page_get_instance_private (page);
  char *buffer;
  char *id;

  id = NULL;

  if (g_file_get_contents ("/etc/os-release", &buffer, NULL, NULL))
    {
      id = get_item (buffer, "ID");
      g_free (buffer);
    }

  if (g_strcmp0 (id, "fedora") == 0)
    {
      g_object_set (priv->logo, "icon-name", "fedora-logo-icon", NULL);
    }
  else if (g_strcmp0 (id, "endless") == 0)
    {
      g_object_set (priv->logo,
                    "resource", "/org/gnome/initial-setup/EndlessLogo.svg",
                    NULL);
    }

  g_free (id);
}

static void
language_confirmed (CcLanguageChooser *chooser,
                    GisLanguagePage   *page)
{
  gis_assistant_next_page (gis_driver_get_assistant (GIS_PAGE (page)->driver));
}

static void
gis_language_page_constructed (GObject *object)
{
  GisLanguagePage *page = GIS_LANGUAGE_PAGE (object);
  GisLanguagePagePrivate *priv = gis_language_page_get_instance_private (page);
  GDBusConnection *bus;
  GClosure *closure;

  g_type_ensure (CC_TYPE_LANGUAGE_CHOOSER);

  G_OBJECT_CLASS (gis_language_page_parent_class)->constructed (object);

  update_distro_logo (page);

  g_signal_connect (priv->language_chooser, "notify::language",
                    G_CALLBACK (language_changed), page);
  g_signal_connect (priv->language_chooser, "confirm",
                    G_CALLBACK (language_confirmed), page);

  /* If we're in new user mode then we're manipulating system settings */
  if (gis_driver_get_mode (GIS_PAGE (page)->driver) == GIS_DRIVER_MODE_NEW_USER)
    {
      priv->permission = polkit_permission_new_sync ("org.freedesktop.locale1.set-locale", NULL, NULL, NULL);

      bus = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, NULL);
      g_dbus_proxy_new (bus,
                        G_DBUS_PROXY_FLAGS_GET_INVALIDATED_PROPERTIES,
                        NULL,
                        "org.freedesktop.locale1",
                        "/org/freedesktop/locale1",
                        "org.freedesktop.locale1",
                        priv->cancellable,
                        (GAsyncReadyCallback) localed_proxy_ready,
                        object);
      g_object_unref (bus);
  }

  /* Use ctrl+f to show factory dialog */
  priv->accel_group = gtk_accel_group_new ();
  closure = g_cclosure_new_swap (G_CALLBACK (gis_page_util_show_factory_dialog), page, NULL);
  gtk_accel_group_connect (priv->accel_group, GDK_KEY_f, GDK_CONTROL_MASK, 0, closure);
  g_closure_unref (closure);

  gis_page_set_complete (GIS_PAGE (page), TRUE);
  gtk_widget_show (GTK_WIDGET (page));
}

static GtkAccelGroup *
gis_language_page_get_accel_group (GisPage *page)
{
  GisLanguagePage *self = GIS_LANGUAGE_PAGE (page);
  GisLanguagePagePrivate *priv = gis_language_page_get_instance_private (self);

  return priv->accel_group;
}

static void
gis_language_page_locale_changed (GisPage *page)
{
  gis_page_set_title (GIS_PAGE (page), _("Welcome"));
}

static void
gis_language_page_dispose (GObject *object)
{
  GisLanguagePage *page = GIS_LANGUAGE_PAGE (object);
  GisLanguagePagePrivate *priv = gis_language_page_get_instance_private (page);

  g_clear_object (&priv->permission);
  g_clear_object (&priv->localed);
  g_clear_object (&priv->cancellable);
  g_clear_object (&priv->accel_group);

  G_OBJECT_CLASS (gis_language_page_parent_class)->dispose (object);
}

static void
gis_language_page_class_init (GisLanguagePageClass *klass)
{
  GisPageClass *page_class = GIS_PAGE_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  gtk_widget_class_set_template_from_resource (GTK_WIDGET_CLASS (klass), "/org/gnome/initial-setup/gis-language-page.ui");

  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisLanguagePage, welcome_widget);
  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisLanguagePage, language_chooser);
  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisLanguagePage, logo);

  page_class->page_id = PAGE_ID;
  page_class->locale_changed = gis_language_page_locale_changed;
  page_class->get_accel_group = gis_language_page_get_accel_group;
  object_class->constructed = gis_language_page_constructed;
  object_class->dispose = gis_language_page_dispose;
}

static void
gis_language_page_init (GisLanguagePage *page)
{
  g_resources_register (language_get_resource ());
  g_type_ensure (GIS_TYPE_WELCOME_WIDGET);
  g_type_ensure (CC_TYPE_LANGUAGE_CHOOSER);

  gtk_widget_init_template (GTK_WIDGET (page));
}

void
gis_prepare_language_page (GisDriver *driver)
{
  gis_driver_add_page (driver,
                       g_object_new (GIS_TYPE_LANGUAGE_PAGE,
                                     "driver", driver,
                                     NULL));
}
