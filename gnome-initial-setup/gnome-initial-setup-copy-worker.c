/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/* Copies settings installed from gnome-initial-setup and
 * sticks them in the user's profile */

#include <pwd.h>
#include <string.h>
#include <gio/gio.h>
#include <gnome-keyring.h>
#include <stdlib.h>

static char *
get_gnome_initial_setup_home_dir (void)
{
  struct passwd pw, *pwp;
  char buf[4096];

  getpwnam_r ("gnome-initial-setup", &pw, buf, sizeof (buf), &pwp);
  if (pwp != NULL)
    return g_strdup (pwp->pw_dir);
  else
    return NULL;
}

static gboolean
file_is_ours (GFile *file)
{
  GFileInfo *info;
  uid_t uid;

  info = g_file_query_info (file,
                            G_FILE_ATTRIBUTE_UNIX_UID,
                            G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                            NULL,
                            NULL);
  if (!info)
    return FALSE;

  uid = g_file_info_get_attribute_uint32 (info, G_FILE_ATTRIBUTE_UNIX_UID);
  g_object_unref (info);

  return uid == geteuid ();
}

static void
move_file_from_homedir (GFile       *src_base,
                        GFile       *dest_base,
                        const gchar *path)
{
  GFile *dest = g_file_get_child (dest_base, path);
  GFile *dest_parent = g_file_get_parent (dest);
  GFile *src = g_file_get_child (src_base, path);

  GError *error = NULL;

  g_file_make_directory_with_parents (dest_parent, NULL, NULL);

  if (!g_file_move (src, dest, G_FILE_COPY_NONE,
                    NULL, NULL, NULL, &error)) {
    if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND)) {
      g_warning ("Unable to move %s to %s: %s",
                 g_file_get_path (src),
                 g_file_get_path (dest),
                 error->message);
    }
  }
}

static void
unlock_keyring (const gchar *initial_setup_homedir)
{
  gchar *file;
  gchar *password = NULL;

  file = g_build_filename (initial_setup_homedir, ".config/password", NULL);
  if (g_file_get_contents (file, &password, NULL, NULL))
    gnome_keyring_unlock_sync ("login", password);
  else
    g_warning ("Unable to read user password file");

  g_remove (file);
  g_free (file);
  g_free (password);
}

int
main (int    argc,
      char **argv)
{
  GFile *src;
  GFile *dest;
  GError *error = NULL;
  char *initial_setup_homedir;
  gchar *gis_done_file_path;

  initial_setup_homedir = get_gnome_initial_setup_home_dir ();
  if (initial_setup_homedir == NULL)
    exit (EXIT_SUCCESS);

  src = g_file_new_for_path (initial_setup_homedir);

  if (!g_file_query_exists (src, NULL) ||
      !file_is_ours (src))
    exit (EXIT_SUCCESS);

  dest = g_file_new_for_path (g_get_home_dir ());

#define FILE(path) \
  move_file_from_homedir (src, dest, path);

  FILE (".config/run-welcome-tour");
  FILE (".config/dconf/user");
  FILE (".config/goa-1.0/accounts.conf");
  FILE (".config/monitors.xml");
  FILE (".local/share/keyrings/login.keyring");

  unlock_keyring (initial_setup_homedir);

  gis_done_file_path = g_build_filename (g_get_user_config_dir (),
                                         "gnome-initial-setup-done",
                                         NULL);

  if (!g_file_set_contents (gis_done_file_path, "yes", -1, &error))
    g_warning ("Unable to create %s: %s", gis_done_file_path, error->message);

  return EXIT_SUCCESS;
}
