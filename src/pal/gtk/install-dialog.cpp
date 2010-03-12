/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * install-dialog.cpp: 
 *
 * Contact:
 *   Moonlight List (moonlight-list@lists.ximian.com)
 *
 * Copyright 2010 Novell, Inc. (http://www.novell.com)
 *
 * See the LICENSE file included with the distribution for details.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include "install-dialog.h"
#include "application.h"
#include "runtime.h"
#include "utils.h"
#include "uri.h"

typedef struct {
	GdkPixbufLoader *loader;
	InstallDialog *dialog;
	int size;
} IconLoader;

typedef struct _InstallDialogPrivate {
	Application *application;
	Deployment *deployment;
	GPtrArray *loaders;
	GList *icon_list;
	
	char *install_dir;
	bool installed;
	
	GtkToggleButton *start_menu;
	GtkToggleButton *desktop;
	GtkLabel *primary_text;
	GtkImage *icon;
} InstallDialogPrivate;

static void install_dialog_class_init (InstallDialogClass *klass);
static void install_dialog_init (InstallDialog *dialog);
static void install_dialog_destroy (GtkObject *obj);
static void install_dialog_finalize (GObject *obj);


static GtkDialogClass *parent_class = NULL;


GType
install_dialog_get_type (void)
{
	static GType type = 0;
	
	if (!type) {
		static const GTypeInfo info = {
			sizeof (InstallDialogClass),
			NULL, /* base_class_init */
			NULL, /* base_class_finalize */
			(GClassInitFunc) install_dialog_class_init,
			NULL, /* class_finalize */
			NULL, /* class_data */
			sizeof (InstallDialog),
			0,    /* n_preallocs */
			(GInstanceInitFunc) install_dialog_init,
		};
		
		type = g_type_register_static (GTK_TYPE_DIALOG, "InstallDialog", &info, (GTypeFlags) 0);
	}
	
	return type;
}

static void
install_dialog_class_init (InstallDialogClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GtkObjectClass *gtk_object_class = GTK_OBJECT_CLASS (klass);
	
	parent_class = (GtkDialogClass *) g_type_class_ref (GTK_TYPE_DIALOG);
	
	object_class->finalize = install_dialog_finalize;
	gtk_object_class->destroy = install_dialog_destroy;
}

static void
install_dialog_init (InstallDialog *dialog)
{
	GtkWidget *checkboxes, *primary, *secondary, *container;
	GtkWidget *vbox, *hbox, *label;
	InstallDialogPrivate *priv;
	
	dialog->priv = priv = g_new0 (InstallDialogPrivate, 1);
	
	gtk_window_set_title ((GtkWindow *) dialog, "Install application");
	gtk_window_set_resizable ((GtkWindow *) dialog, false);
	gtk_window_set_modal ((GtkWindow *) dialog, true);
	
	hbox = gtk_hbox_new (false, 12);
	
	priv->icon = (GtkImage *) gtk_image_new ();
	gtk_widget_show ((GtkWidget *) priv->icon);
	
	gtk_box_pack_start ((GtkBox *) hbox, (GtkWidget *) priv->icon, false, false, 0);
	
	priv->primary_text = (GtkLabel *) gtk_label_new ("");
	gtk_label_set_line_wrap (priv->primary_text, true);
	gtk_widget_show ((GtkWidget *) priv->primary_text);
	
	label = gtk_label_new ("Please confirm the location for the shortcuts.");
	gtk_label_set_line_wrap ((GtkLabel *) label, true);
	gtk_widget_show (label);
	
	priv->start_menu = (GtkToggleButton *) gtk_check_button_new_with_label ("Start menu");
	gtk_toggle_button_set_active (priv->start_menu, true);
	gtk_widget_show ((GtkWidget *) priv->start_menu);
	
	priv->desktop = (GtkToggleButton *) gtk_check_button_new_with_label ("Desktop");
	gtk_toggle_button_set_active (priv->desktop, false);
	gtk_widget_show ((GtkWidget *) priv->desktop);
	
	vbox = gtk_vbox_new (false, 2);
	gtk_box_pack_start ((GtkBox *) vbox, (GtkWidget *) priv->start_menu, false, false, 0);
	gtk_box_pack_start ((GtkBox *) vbox, (GtkWidget *) priv->desktop, false, false, 0);
	gtk_widget_show (vbox);
	
	checkboxes = gtk_alignment_new (0.0, 0.0, 0.0, 0.0);
	gtk_alignment_set_padding ((GtkAlignment *) checkboxes, 0, 0, 8, 0);
	gtk_container_add ((GtkContainer *) checkboxes, vbox);
	gtk_widget_show (checkboxes);
	
	vbox = gtk_vbox_new (false, 6);
	gtk_box_pack_start ((GtkBox *) vbox, label, false, false, 0);
	gtk_box_pack_start ((GtkBox *) vbox, checkboxes, false, false, 0);
	gtk_widget_show (vbox);
	
	primary = gtk_alignment_new (0.0, 0.0, 0.0, 0.0);
	gtk_alignment_set_padding ((GtkAlignment *) primary, 0, 0, 0, 0);
	gtk_container_add ((GtkContainer *) primary, (GtkWidget *) priv->primary_text);
	gtk_widget_show (primary);
	
	secondary = gtk_alignment_new (0.0, 0.0, 0.0, 0.0);
	gtk_alignment_set_padding ((GtkAlignment *) secondary, 6, 0, 0, 0);
	gtk_container_add ((GtkContainer *) secondary, vbox);
	gtk_widget_show (secondary);
	
	vbox = gtk_vbox_new (false, 6);
	gtk_box_pack_start ((GtkBox *) vbox, primary, false, false, 0);
	gtk_box_pack_start ((GtkBox *) vbox, secondary, false, false, 0);
	gtk_widget_show (vbox);
	
	gtk_box_pack_start ((GtkBox *) hbox, vbox, false, false, 0);
	gtk_container_set_border_width ((GtkContainer *) hbox, 12);
	gtk_widget_show (hbox);
	
	//container = gtk_dialog_get_content_area ((GtkDialog *) dialog);
	container = ((GtkDialog *) dialog)->vbox;
	gtk_container_add ((GtkContainer *) container, hbox);
	
	/* Add OK and Cancel buttons */
	gtk_dialog_set_has_separator ((GtkDialog *) dialog, false);
	gtk_dialog_add_button ((GtkDialog *) dialog, GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);
	gtk_dialog_add_button ((GtkDialog *) dialog, GTK_STOCK_OK, GTK_RESPONSE_OK);
	gtk_dialog_set_default_response ((GtkDialog *) dialog, GTK_RESPONSE_CANCEL);
}

static void
install_dialog_finalize (GObject *obj)
{
	InstallDialog *dialog = (InstallDialog *) obj;
	InstallDialogPrivate *priv = dialog->priv;
	IconLoader *loader;
	guint i;
	
	if (!priv->installed)
		RemoveDir (priv->install_dir);
	
	g_free (priv->install_dir);
	
	if (priv->loaders) {
		for (i = 0; i < priv->loaders->len; i++) {
			loader = (IconLoader *) priv->loaders->pdata[i];
			if (loader->loader)
				g_object_unref (loader->loader);
			g_free (loader);
		}
		
		g_ptr_array_free (priv->loaders, true);
	}
	
	g_list_free (priv->icon_list);
	
	priv->application->unref ();
	priv->deployment->unref ();
	
	g_free (priv);
	
	G_OBJECT_CLASS (parent_class)->finalize (obj);
}

static void
install_dialog_destroy (GtkObject *obj)
{
	GTK_OBJECT_CLASS (parent_class)->destroy (obj);
}

static void
icon_loader_notify_cb (NotifyType type, gint64 args, gpointer user_data)
{
	IconLoader *loader = (IconLoader *) user_data;
	InstallDialogPrivate *priv = loader->dialog->priv;
	GdkPixbuf *pixbuf;
	
	switch (type) {
	case NotifyCompleted:
		if (loader->loader) {
			if (gdk_pixbuf_loader_close (loader->loader, NULL)) {
				/* get the pixbuf and add it to our icon_list */
				pixbuf = gdk_pixbuf_loader_get_pixbuf (loader->loader);
				priv->icon_list = g_list_prepend (priv->icon_list, pixbuf);
				
				if (loader->size == 128) {
					/* set the 128x128 pixbuf as the icon in our dialog */
					gtk_image_set_from_pixbuf (priv->icon, pixbuf);
				}
				
				gtk_window_set_icon_list ((GtkWindow *) loader->dialog, priv->icon_list);
				break;
			}
			
			/* fall through as if we got a NotifyFailed */
		}
	case NotifyFailed:
		if (loader->loader) {
			/* load default icon and destroy the loader */
			gtk_image_set_from_icon_name (priv->icon, "gnome-remote-desktop", GTK_ICON_SIZE_DIALOG);
			g_object_unref (loader->loader);
			loader->loader = NULL;
		}
		break;
	default:
		break;
	}
}

static void
icon_loader_write_cb (void *buffer, gint32 offset, gint32 n, gpointer user_data)
{
	IconLoader *loader = (IconLoader *) user_data;
	
	if (loader->loader && !gdk_pixbuf_loader_write (loader->loader, (const guchar *) buffer, n, NULL)) {
		/* loading failed, destroy the loader */
		g_object_unref (loader->loader);
		loader->loader = NULL;
	}
}

GtkDialog *
install_dialog_new (GtkWindow *parent, Deployment *deployment)
{
	InstallDialog *dialog = (InstallDialog *) g_object_new (INSTALL_DIALOG_TYPE, NULL);
	OutOfBrowserSettings *settings = deployment->GetOutOfBrowserSettings ();
	Application *application = deployment->GetCurrentApplication ();
	IconCollection *icons = settings->GetIcons ();
	InstallDialogPrivate *priv = dialog->priv;
	char *markup, *location;
	IconLoader *loader;
	int count, i;
	
	if (parent) {
		gtk_window_set_transient_for ((GtkWindow *) dialog, parent);
		gtk_window_set_destroy_with_parent ((GtkWindow *) dialog, true);
	}
	
	priv->application = application;
	application->ref ();
	
	priv->deployment = deployment;
	deployment->ref ();
	
	if (g_ascii_strncasecmp (deployment->GetXapLocation (), "file:", 5) != 0) {
		location = g_path_get_dirname (deployment->GetXapLocation ());
	} else {
		location = g_strdup ("file://");
	}
	
	markup = g_markup_printf_escaped ("You are installing <b>%s</b> from <b>%s</b>",
					  settings->GetShortName (), location);
	gtk_label_set_markup (priv->primary_text, markup);
	g_free (location);
	g_free (markup);
	
	priv->install_dir = install_utils_get_install_dir (settings);
	
	/* load the icons */
	if (icons && (count = icons->GetCount ()) > 0) {
		priv->loaders = g_ptr_array_sized_new (count);
		
		for (i = 0; i < count; i++) {
			Value *value = icons->GetValueAt (i);
			Icon *icon = value->AsIcon ();
			Size *size = icon->GetSize ();
			Uri *uri = icon->GetSource ();
			
			loader = g_new (IconLoader, 1);
			loader->size = MAX ((int) size->width, (int) size->height);
			loader->loader = gdk_pixbuf_loader_new ();
			loader->dialog = dialog;
			
			g_ptr_array_add (priv->loaders, loader);
			
			application->GetResource (NULL, uri, icon_loader_notify_cb, icon_loader_write_cb, MediaPolicy, NULL, loader);
		}
	}
	
	if (!priv->loaders)
		gtk_image_set_from_icon_name (priv->icon, "gnome-remote-desktop", GTK_ICON_SIZE_DIALOG);
	
	return (GtkDialog *) dialog;
}

bool
install_dialog_get_install_to_start_menu (InstallDialog *dialog)
{
	g_return_val_if_fail (IS_INSTALL_DIALOG (dialog), false);
	
	return gtk_toggle_button_get_active (dialog->priv->start_menu);
}

bool
install_dialog_get_install_to_desktop (InstallDialog *dialog)
{
	g_return_val_if_fail (IS_INSTALL_DIALOG (dialog), false);
	
	return gtk_toggle_button_get_active (dialog->priv->desktop);
}

static bool
install_xap (Deployment *deployment, const char *path)
{
	Surface *surface = deployment->GetSurface ();
	int fd;
	
	if ((fd = open (path, O_CREAT | O_EXCL | O_WRONLY, 0666)) == -1)
		return false;
	
	if (CopyFileTo (surface->GetSourceLocation (), fd) == -1)
		return false;
	
	return true;
}

static bool
install_html (OutOfBrowserSettings *settings, const char *filename)
{
	WindowSettings *window = settings->GetWindowSettings ();
	char *title;
	FILE *fp;
	
	if (!(fp = fopen (filename, "wt")))
		return false;
	
	if (window && window->GetTitle ())
		title = g_markup_escape_text (window->GetTitle (), -1);
	else
		title = g_markup_escape_text (settings->GetShortName (), -1);
	
	fprintf (fp, "<html xmlns=\"http://www.w3.org/1999/xhtml\">\n");
	fprintf (fp, "  <head>\n");
	fprintf (fp, "    <title>%s</title>\n", title);
	fprintf (fp, "  </head>\n");
	fprintf (fp, "  <body>\n");
	fprintf (fp, "    <div id=\"MoonlightControl\">\n");
	fprintf (fp, "      <object data=\"data:application/x-silverlight-2,\" type=\"application/x-silverlight-2\" width=\"100%%\" height=\"100%%\">\n");
	fprintf (fp, "        <param name=\"source\" value=\"Application.xap\"/>\n");
	fprintf (fp, "        <param name=\"isOutOfBrowser\" value=\"true\"/>\n");
	fprintf (fp, "        <param name=\"background\" value=\"white\"/>\n");
	fprintf (fp, "      </object>\n");
	fprintf (fp, "    </div>\n");
	fprintf (fp, "  </body>\n");
	fprintf (fp, "</html>\n");
	
	g_free (title);
	fclose (fp);
	
	return true;
}

static void
notify_cb (NotifyType type, gint64 args, gpointer user_data)
{
	FILE *fp = (FILE *) user_data;
	
	switch (type) {
	case NotifyCompleted:
	case NotifyFailed:
		fclose (fp);
		break;
	default:
		break;
	}
}

static void
write_cb (void *buffer, gint32 offset, gint32 n, gpointer user_data)
{
	FILE *fp = (FILE *) user_data;
	
	fwrite (buffer, 1, n, fp);
}

static void
install_icons (Application *application, OutOfBrowserSettings *settings, const char *install_dir)
{
	IconCollection *icons = settings->GetIcons ();
	char *filename, *icons_dir, name[64];
	int i, count;
	
	if (icons && (count = icons->GetCount ()) > 0) {
		icons_dir = g_build_filename (install_dir, "icons", NULL);
		
		for (i = 0; i < count; i++) {
			Value *value = icons->GetValueAt (i);
			Icon *icon = value->AsIcon ();
			Size *size = icon->GetSize ();
			
			if ((int) size->width == 48 || (int) size->height == 48) {
				/* we only need to extract the 48x48 icon for the .desktop files */
				Uri *uri = icon->GetSource ();
				FILE *fp;
				
				g_mkdir_with_parents (icons_dir, 0777);
				
				snprintf (name, sizeof (name), "%dx%d.png", (int) size->width, (int) size->height);
				filename = g_build_filename (icons_dir, name, NULL);
				
				if ((fp = fopen (filename, "wb")))
					application->GetResource (NULL, uri, notify_cb, write_cb, MediaPolicy, NULL, fp);
				
				g_free (filename);
			}
		}
		
		g_free (icons_dir);
	}
}

static bool
install_gnome_desktop (OutOfBrowserSettings *settings, const char *app_dir, const char *filename)
{
	char *dirname, *icon_name;
	struct stat st;
	FILE *fp;
	
	dirname = g_path_get_dirname (filename);
	g_mkdir_with_parents (dirname, 0777);
	g_free (dirname);
	
	if (!(fp = fopen (filename, "wt")))
		return false;
	
	fprintf (fp, "[Desktop Entry]\n");
	fprintf (fp, "Type=Application\n");
	fprintf (fp, "Encoding=UTF-8\n");
	fprintf (fp, "Version=1.0\n");
	fprintf (fp, "Terminal=false\n");
	fprintf (fp, "Categories=Application;Utility;\n");
	fprintf (fp, "Name=%s\n", settings->GetShortName ());
	
	if (settings->GetBlurb ())
		fprintf (fp, "Comment=%s\n", settings->GetBlurb ());
	
	icon_name = g_build_filename (app_dir, "icons", "48x48.png", NULL);
	if (stat (icon_name, &st) != -1 && S_ISREG (st.st_mode))
		fprintf (fp, "Icon=%s\n", icon_name);
	g_free (icon_name);
	
	fprintf (fp, "Exec=gnome-open \"file:%s/index.html\"\n", app_dir);
	
	fclose (fp);
	
	return true;
}

bool
install_dialog_install (InstallDialog *dialog)
{
	InstallDialogPrivate *priv = dialog->priv;
	OutOfBrowserSettings *settings;
	char *filename;
	
	g_return_val_if_fail (IS_INSTALL_DIALOG (dialog), false);
	
	if (priv->installed)
		return true;
	
	settings = priv->deployment->GetOutOfBrowserSettings ();
	
	if (g_mkdir_with_parents (priv->install_dir, 0777) == -1)
		return false;
	
	/* install the XAP */
	filename = g_build_filename (priv->install_dir, "Application.xap", NULL);
	if (!install_xap (priv->deployment, filename)) {
		g_free (filename);
		return false;
	}
	
	g_free (filename);
	
	/* install the HTML page */
	filename = g_build_filename (priv->install_dir, "index.html", NULL);
	if (!install_html (settings, filename)) {
		g_free (filename);
		return false;
	}
	
	g_free (filename);
	
	priv->installed = true;
	
	/* install the icon(s) */
	install_icons (priv->application, settings, priv->install_dir);
	
	/* conditionally install start menu shortcut */
	if (install_dialog_get_install_to_start_menu (dialog)) {
		filename = install_utils_get_start_menu_shortcut (settings);
		install_gnome_desktop (settings, priv->install_dir, filename);
		g_free (filename);
	}
	
	/* conditionally install desktop shortcut */
	if (install_dialog_get_install_to_desktop (dialog)) {
		filename = install_utils_get_desktop_shortcut (settings);
		install_gnome_desktop (settings, priv->install_dir, filename);
		g_free (filename);
	}
	
	return true;
}


char *
install_utils_get_app_name (OutOfBrowserSettings *settings)
{
	// FIXME: not safe to use the short name like this... but it'll do for now.
	return g_strdup (settings->GetShortName ());
}

char *
install_utils_get_install_dir (OutOfBrowserSettings *settings)
{
	char *app_name, *install_dir;
	
	app_name = install_utils_get_app_name (settings);
	install_dir = g_build_filename (g_get_home_dir (), ".local", "share", "moonlight", "applications", app_name, NULL);
	g_free (app_name);
	
	return install_dir;
}

char *
install_utils_get_desktop_shortcut (OutOfBrowserSettings *settings)
{
	char *shortcut, *path;
	
	shortcut = g_strdup_printf ("%s.desktop", settings->GetShortName ());
	path = g_build_filename (g_get_home_dir (), "Desktop", shortcut, NULL);
	g_free (shortcut);
	
	return path;
}

char *
install_utils_get_start_menu_shortcut (OutOfBrowserSettings *settings)
{
	char *shortcut, *path;
	
	shortcut = g_strdup_printf ("%s.desktop", settings->GetShortName ());
	path = g_build_filename (g_get_home_dir (), ".local", "share", "applications", shortcut, NULL);
	g_free (shortcut);
	
	return path;
}