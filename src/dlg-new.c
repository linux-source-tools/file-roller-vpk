/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 *  File-Roller
 *
 *  Copyright (C) 2008 Free Software Foundation, Inc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Street #330, Boston, MA 02111-1307, USA.
 */

#include <config.h>
#include <string.h>
#include <math.h>
#include <unistd.h>

#include <gio/gio.h>
#include "dlg-new.h"
#include "file-utils.h"
#include "fr-stock.h"
#include "gconf-utils.h"
#include "main.h"
#include "preferences.h"


#define GLADE_FILE "new.glade"
#define DEFAULT_EXTENSION ".tar.gz"
#define BAD_CHARS "/\\*"
#define MEGABYTE (1024 * 1024)


/* called when the main dialog is closed. */
static void
destroy_cb (GtkWidget  *widget,
	    DlgNewData *data)
{
	g_object_unref (data->gui);
	g_free (data);
}


static void
update_sensitivity_for_ext (DlgNewData *data,
			    const char *ext)
{
	const char *mime_type;
	int         i;

	mime_type = get_mime_type_from_extension (ext);

	if (mime_type == NULL) {
		gtk_widget_set_sensitive (data->n_password_entry, FALSE);
		gtk_widget_set_sensitive (data->n_password_label, FALSE);
		gtk_widget_set_sensitive (data->n_encrypt_header_checkbutton, FALSE);
		gtk_widget_set_sensitive (data->n_volume_box, FALSE);
		return;
	}

	for (i = 0; mime_type_desc[i].mime_type != NULL; i++) {
		if (strcmp (mime_type_desc[i].mime_type, mime_type) == 0) {
			gboolean sensitive;

			sensitive = mime_type_desc[i].capabilities & FR_COMMAND_CAN_ENCRYPT;
			gtk_widget_set_sensitive (data->n_password_entry, sensitive);
			gtk_widget_set_sensitive (data->n_password_label, sensitive);

			sensitive = mime_type_desc[i].capabilities & FR_COMMAND_CAN_ENCRYPT_HEADER;
			gtk_widget_set_sensitive (data->n_encrypt_header_checkbutton, sensitive);

			sensitive = mime_type_desc[i].capabilities & FR_COMMAND_CAN_CREATE_VOLUMES;
			gtk_widget_set_sensitive (data->n_volume_box, sensitive);

			break;
		}
	}
}


static int
get_archive_type (DlgNewData *data)
{
	const char *uri;
	const char *ext;

	uri = gtk_file_chooser_get_uri (GTK_FILE_CHOOSER (data->dialog));
	if (uri == NULL)
		return -1;

	ext = get_archive_filename_extension (uri);
	if (ext == NULL) {
		int idx;

		idx = gtk_combo_box_get_active (GTK_COMBO_BOX (data->n_archive_type_combo_box)) - 1;
		if (idx >= 0)
			return data->supported_types[idx];

		ext = DEFAULT_EXTENSION;
	}

	return get_mime_type_index (get_mime_type_from_extension (ext));
}


static void
archive_type_combo_box_changed_cb (GtkComboBox *combo_box,
				   DlgNewData  *data)
{
	int         idx;
	const char *uri, *basename;
	const char *ext, *new_ext;
	char       *basename_noext;
	char       *new_basename;
	char       *new_basename_uft8;

	uri = gtk_file_chooser_get_uri (GTK_FILE_CHOOSER (data->dialog));
	if (uri == NULL)
		return;

	ext = get_archive_filename_extension (uri);
	if (ext == NULL)
		ext = "";

	idx = gtk_combo_box_get_active (GTK_COMBO_BOX (data->n_archive_type_combo_box)) - 1;
	if (idx < 0) {
		update_sensitivity_for_ext (data, ext);
		return;
	}

	new_ext = mime_type_desc[data->supported_types[idx]].default_ext;
	basename = file_name_from_path (uri);
	basename_noext = g_strndup (basename, strlen (basename) - strlen (ext));
	new_basename = g_strconcat (basename_noext, new_ext, NULL);
	new_basename_uft8 = g_uri_unescape_string (new_basename, NULL);

	gtk_file_chooser_set_current_name (GTK_FILE_CHOOSER (data->dialog), new_basename_uft8);
	update_sensitivity_for_ext (data, new_ext);

	g_free (new_basename_uft8);
	g_free (new_basename);
	g_free (basename_noext);
}


static void
update_sensitivity (DlgNewData *data)
{
	const char *password;
	gboolean    void_password;

	password = gtk_entry_get_text (GTK_ENTRY (data->n_password_entry));
	void_password = (password == NULL) || (strcmp (password, "") == 0);
	gtk_toggle_button_set_inconsistent (GTK_TOGGLE_BUTTON (data->n_encrypt_header_checkbutton), void_password);
	/*gtk_widget_set_sensitive (GTK_WIDGET (data->n_encrypt_header_checkbutton), ! void_password);*/
	gtk_widget_set_sensitive (data->n_volume_spinbutton, gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (data->n_volume_checkbutton)));
}


static void
password_entry_changed_cb (GtkEditable *editable,
			   gpointer     user_data)
{
	update_sensitivity ((DlgNewData *) user_data);
}


static void
volume_toggled_cb (GtkToggleButton *toggle_button,
		   gpointer         user_data)
{
	update_sensitivity ((DlgNewData *) user_data);
}


static DlgNewData *
dlg_new_archive (FrWindow  *window,
		int        *supported_types,
		const char *default_name)
{
	DlgNewData    *data;
	GtkWidget     *n_archive_type_box;
	GtkWidget     *n_new_button;
	GtkSizeGroup  *size_group;
	GtkFileFilter *filter;
	/*char          *default_ext;*/
	int            i;

	data = g_new0 (DlgNewData, 1);

	data->gui = glade_xml_new (GLADEDIR "/" GLADE_FILE , NULL, NULL);
	if (data->gui == NULL) {
		g_warning ("Could not find " GLADE_FILE "\n");
		return NULL;
	}

	data->window = window;
	data->supported_types = supported_types;

	/* Get the widgets. */

	data->dialog = glade_xml_get_widget (data->gui, "filechooserdialog");

	data->n_password_entry = glade_xml_get_widget (data->gui, "n_password_entry");
	data->n_password_label = glade_xml_get_widget (data->gui, "n_password_label");
	data->n_other_options_expander = glade_xml_get_widget (data->gui, "n_other_options_expander");
	data->n_encrypt_header_checkbutton = glade_xml_get_widget (data->gui, "n_encrypt_header_checkbutton");

	data->n_volume_checkbutton = glade_xml_get_widget (data->gui, "n_volume_checkbutton");
	data->n_volume_spinbutton = glade_xml_get_widget (data->gui, "n_volume_spinbutton");
	data->n_volume_box = glade_xml_get_widget (data->gui, "n_volume_box");

	n_archive_type_box = glade_xml_get_widget (data->gui, "n_archive_type_box");
	n_new_button = glade_xml_get_widget (data->gui, "n_new_button");

	/* Set widgets data. */

	gtk_dialog_set_default_response (GTK_DIALOG (data->dialog), GTK_RESPONSE_OK);
	gtk_file_chooser_set_current_folder_uri (GTK_FILE_CHOOSER (data->dialog), fr_window_get_open_default_dir (window));

	if (default_name != NULL) {
		gtk_file_chooser_set_current_name (GTK_FILE_CHOOSER (data->dialog), default_name);
		/*char *ext, *name_ext;

		ext = eel_gconf_get_string (PREF_BATCH_ADD_DEFAULT_EXTENSION, ".tgz");
		name_ext = g_strconcat (default_name, ext, NULL);
		gtk_file_chooser_set_current_name (GTK_FILE_CHOOSER (data->dialog), name_ext);
		g_free (name_ext);
		g_free (ext);*/
	}

	filter = gtk_file_filter_new ();
	gtk_file_filter_set_name (filter, _("All archives"));
	for (i = 0; data->supported_types[i] != -1; i++)
		gtk_file_filter_add_mime_type (filter, mime_type_desc[data->supported_types[i]].mime_type);
	gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (data->dialog), filter);
	gtk_file_chooser_set_filter (GTK_FILE_CHOOSER (data->dialog), filter);

	filter = gtk_file_filter_new ();
	gtk_file_filter_set_name (filter, _("All files"));
	gtk_file_filter_add_pattern (filter, "*");
	gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (data->dialog), filter);

	/**/

	size_group = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);
	gtk_size_group_add_widget (size_group, glade_xml_get_widget (data->gui, "n_archive_type_label"));
	gtk_size_group_add_widget (size_group, glade_xml_get_widget (data->gui, "n_password_label"));

	gtk_button_set_use_stock (GTK_BUTTON (n_new_button), TRUE);
	gtk_button_set_label (GTK_BUTTON (n_new_button), FR_STOCK_CREATE_ARCHIVE);
	gtk_expander_set_expanded (GTK_EXPANDER (data->n_other_options_expander), eel_gconf_get_boolean (PREF_BATCH_OTHER_OPTIONS, FALSE));
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (data->n_encrypt_header_checkbutton), eel_gconf_get_boolean (PREF_ENCRYPT_HEADER, FALSE));
	gtk_spin_button_set_value (GTK_SPIN_BUTTON (data->n_volume_spinbutton), (double) eel_gconf_get_integer (PREF_BATCH_VOLUME_SIZE, 0) / MEGABYTE);

	/* archive type combobox */

	data->n_archive_type_combo_box = gtk_combo_box_new_text ();
	gtk_combo_box_append_text (GTK_COMBO_BOX (data->n_archive_type_combo_box), _("Automatic"));
	for (i = 0; data->supported_types[i] != -1; i++) {
		int idx = data->supported_types[i];
		gtk_combo_box_append_text (GTK_COMBO_BOX (data->n_archive_type_combo_box),
					   _(mime_type_desc[idx].name));
	}
	gtk_combo_box_set_active (GTK_COMBO_BOX (data->n_archive_type_combo_box), 0);
	gtk_box_pack_start (GTK_BOX (n_archive_type_box), data->n_archive_type_combo_box, TRUE, TRUE, 0);
	gtk_widget_show_all (n_archive_type_box);

	/* Set the signals handlers. */

	/*g_signal_connect (G_OBJECT (data->dialog),
			  "response",
			  G_CALLBACK (new_file_response_cb),
			  data);*/

	g_signal_connect (G_OBJECT (data->dialog),
			  "destroy",
			  G_CALLBACK (destroy_cb),
			  data);

	/*
	g_signal_connect_swapped (G_OBJECT (cancel_button),
				  "clicked",
				  G_CALLBACK (gtk_widget_destroy),
				  G_OBJECT (data->dialog));
	g_signal_connect (G_OBJECT (add_button),
			  "clicked",
			  G_CALLBACK (add_clicked_cb),
			  data);*/

	g_signal_connect (G_OBJECT (data->n_archive_type_combo_box),
			  "changed",
			  G_CALLBACK (archive_type_combo_box_changed_cb),
			  data);
	g_signal_connect (G_OBJECT (data->n_password_entry),
			  "changed",
			  G_CALLBACK (password_entry_changed_cb),
			  data);
	g_signal_connect (G_OBJECT (data->n_volume_checkbutton),
			  "toggled",
			  G_CALLBACK (volume_toggled_cb),
			  data);

	/* Run dialog. */

/*	default_ext = eel_gconf_get_string (PREF_BATCH_ADD_DEFAULT_EXTENSION, DEFAULT_EXTENSION);
	update_archive_type_combo_box_from_ext (data, default_ext);
	g_free (default_ext);*/

	update_sensitivity (data);

	gtk_window_set_modal (GTK_WINDOW (data->dialog), TRUE);
	gtk_window_set_transient_for (GTK_WINDOW (data->dialog), GTK_WINDOW (data->window));
	/*gtk_window_present (GTK_WINDOW (data->dialog));*/

	return data;
}


DlgNewData *
dlg_new (FrWindow *window)
{
	DlgNewData *data;

	data = dlg_new_archive (window, create_type, NULL);
	gtk_window_set_title (GTK_WINDOW (data->dialog), _("New"));

	return data;
}


DlgNewData *
dlg_save_as (FrWindow   *window,
	     const char *default_name)
{
	DlgNewData *data;

	data = dlg_new_archive (window, save_type, default_name);
	gtk_window_set_title (GTK_WINDOW (data->dialog), _("Save"));

	return data;
}


const char *
dlg_new_data_get_password (DlgNewData *data)
{
	const char *password = NULL;
	int         idx;

	idx = get_archive_type (data);
	if (idx < 0)
		return NULL;

	if (mime_type_desc[idx].capabilities & FR_COMMAND_CAN_ENCRYPT)
		password = (char*) gtk_entry_get_text (GTK_ENTRY (data->n_password_entry));

	return password;
}


gboolean
dlg_new_data_get_encrypt_header (DlgNewData *data)
{
	gboolean encrypt_header = FALSE;
	int      idx;

	idx = get_archive_type (data);
	if (idx < 0)
		return FALSE;

	if (mime_type_desc[idx].capabilities & FR_COMMAND_CAN_ENCRYPT) {
		char *password = (char*) gtk_entry_get_text (GTK_ENTRY (data->n_password_entry));
		if (password != NULL) {
			password = g_strstrip (password);
			if (strcmp (password, "") != 0) {
				if (mime_type_desc[idx].capabilities & FR_COMMAND_CAN_ENCRYPT_HEADER)
					encrypt_header = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (data->n_encrypt_header_checkbutton));
			}
		}
	}

	return encrypt_header;
}


int
dlg_new_data_get_volume_size (DlgNewData *data)
{
	guint volume_size = 0;
	int   idx;

	idx = get_archive_type (data);
	if (idx < 0)
		return 0;

	if ((mime_type_desc[idx].capabilities & FR_COMMAND_CAN_CREATE_VOLUMES)
	    && gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (data->n_volume_checkbutton)))
	{
		double value;

		value = gtk_spin_button_get_value (GTK_SPIN_BUTTON (data->n_volume_spinbutton));
		volume_size = floor (value * MEGABYTE);

	}

	return volume_size;
}