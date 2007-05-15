#include "gva-play-back.h"

#include <errno.h>
#include <langinfo.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>

#include "gva-game-db.h"
#include "gva-game-store.h"
#include "gva-time.h"
#include "gva-ui.h"
#include "gva-xmame.h"

static void
play_back_tree_view_row_activated_cb (GtkTreeView *view,
                                      GtkTreePath *path,
                                      GtkTreeViewColumn *column)
{
        gtk_action_activate (GVA_ACTION_PLAY_BACK);
}

static void
play_back_clicked_cb (GtkButton *button, GtkTreeView *view)
{
        gtk_action_activate (GVA_ACTION_PLAY_BACK);
}

static gint
play_back_confirm_deletion (void)
{
        GtkWidget *dialog;
        gint response;

        dialog = gtk_message_dialog_new_with_markup (
                GTK_WINDOW (GVA_WIDGET_PLAY_BACK_WINDOW),
                GTK_DIALOG_DESTROY_WITH_PARENT,
                GTK_MESSAGE_WARNING, GTK_BUTTONS_NONE,
                "<big><b>%s</b></big>",
                _("Delete selected game recordings?"));

        gtk_message_dialog_format_secondary_text (
                GTK_MESSAGE_DIALOG (dialog), "%s",
                _("This operation will permanently erase the recorded games "
                  "you have selected.  Are you sure you want to proceed?"));

        gtk_dialog_add_button (
                GTK_DIALOG (dialog), _("Do Not Delete"), GTK_RESPONSE_REJECT);

        gtk_dialog_add_button (
                GTK_DIALOG (dialog), GTK_STOCK_DELETE, GTK_RESPONSE_ACCEPT);

        response = gtk_dialog_run (GTK_DIALOG (dialog));

        gtk_widget_destroy (dialog);

        return response;
}

static void
play_back_delete_inpfile (GtkTreeRowReference *reference)
{
        GtkTreeModel *model;
        GtkTreePath *path;
        GtkTreeIter iter;
        gboolean iter_set;
        gchar *inpfile;

        model = gtk_tree_row_reference_get_model (reference);

        path = gtk_tree_row_reference_get_path (reference);
        iter_set = gtk_tree_model_get_iter (model, &iter, path);
        gtk_tree_path_free (path);
        g_assert (iter_set);

        gtk_tree_model_get (
                model, &iter, GVA_GAME_STORE_COLUMN_INPFILE, &inpfile, -1);
        g_assert (inpfile != NULL);

        errno = 0;
        if (g_unlink (inpfile) == 0)
                gtk_list_store_remove (GTK_LIST_STORE (model), &iter);
        else
                g_warning ("%s: %s", inpfile, g_strerror (errno));

        g_free (inpfile);
}

static void
play_back_delete_clicked_cb (GtkButton *button, GtkTreeView *view)
{
        GtkTreeModel *model;
        GList *list, *iter;

        if (play_back_confirm_deletion () != GTK_RESPONSE_ACCEPT)
                return;

        list = gtk_tree_selection_get_selected_rows (
                gtk_tree_view_get_selection (view), &model);
        for (iter = list; iter != NULL; iter = iter->next)
        {
                GtkTreePath *path = iter->data;

                iter->data = gtk_tree_row_reference_new (model, path);
                gtk_tree_path_free (path);
        }

        g_list_foreach (list, (GFunc) play_back_delete_inpfile, NULL);
        g_list_foreach (list, (GFunc) gtk_tree_row_reference_free, NULL);
        g_list_free (list);
}

static void
play_back_selection_changed_cb (GtkTreeSelection *tree_selection)
{
        gint selected_rows;

        selected_rows =
                gtk_tree_selection_count_selected_rows (tree_selection);
        gtk_widget_set_sensitive (
                GVA_WIDGET_PLAY_BACK_BUTTON, (selected_rows == 1));
        gtk_widget_set_sensitive (
                GVA_WIDGET_PLAY_BACK_DELETE_BUTTON, (selected_rows >= 1));
}

static void
play_back_render_time (GtkTreeViewColumn *column, GtkCellRenderer *renderer,
                       GtkTreeModel *model, GtkTreeIter *iter)
{
        GValue value;
        gchar text[256];

        memset (&value, 0, sizeof (GValue));
        gtk_tree_model_get_value (
                model, iter, GVA_GAME_STORE_COLUMN_TIME, &value);
        strftime (
                text, sizeof (text), nl_langinfo (D_T_FMT),
                localtime (g_value_get_boxed (&value)));
        g_object_set (renderer, "text", text, NULL);
        g_value_unset (&value);
}

static void
play_back_add_input_file (gchar *inpfile, gchar *romname, GtkTreeModel *model)
{
        GtkTreePath *path;
        GtkTreeIter iter;
        gboolean iter_set;
        gchar *title;
        struct stat statbuf;
        time_t *time;

        if (g_stat (inpfile, &statbuf) != 0)
        {
                g_warning ("%s: %s", inpfile, g_strerror (errno));
                return;
        }

        time = &statbuf.st_ctime;

        path = gva_game_db_lookup (romname);
        if (path == NULL)
        {
                g_warning ("%s: Game '%s' not found", inpfile, romname);
                return;
        }

        iter_set = gtk_tree_model_get_iter (
                gva_game_db_get_model (), &iter, path);
        g_assert (iter_set);

        gtk_tree_path_free (path);

        gtk_tree_model_get (
                gva_game_db_get_model (), &iter,
                GVA_GAME_STORE_COLUMN_TITLE, &title, -1);

        gtk_list_store_append (GTK_LIST_STORE (model), &iter);

        gtk_list_store_set (
                GTK_LIST_STORE (model), &iter,
                GVA_GAME_STORE_COLUMN_INPFILE, inpfile,
                GVA_GAME_STORE_COLUMN_ROMNAME, romname,
                GVA_GAME_STORE_COLUMN_TITLE, title,
                GVA_GAME_STORE_COLUMN_TIME, time,
                -1);

        g_free (title);
}

static void
play_back_refresh_list (GtkWindow *window, GtkTreeView *view)
{
        GHashTable *hash_table;
        GError *error = NULL;

        hash_table = gva_xmame_get_input_files (&error);
        if (hash_table != NULL)
        {
                GtkTreeModel *model;

                model = gtk_tree_view_get_model (view);
                gtk_list_store_clear (GTK_LIST_STORE (model));
                g_hash_table_foreach (
                        hash_table, (GHFunc) play_back_add_input_file, model);
                g_hash_table_destroy (hash_table);
        }
        else
        {
                g_assert (error != NULL);
                g_warning ("%s", error->message);
                g_error_free (error);
        }
}

void
gva_play_back_init (void)
{
        GtkWindow *window;
        GtkTreeView *view;
        GtkCellRenderer *renderer;
        GtkTreeViewColumn *column;

        window = GTK_WINDOW (GVA_WIDGET_PLAY_BACK_WINDOW);
        view = GTK_TREE_VIEW (GVA_WIDGET_PLAY_BACK_TREE_VIEW);

        gtk_tree_view_set_model (view, gva_game_store_new ());

        gtk_tree_selection_set_mode (
                gtk_tree_view_get_selection (view),
                GTK_SELECTION_MULTIPLE);

        /* Played On Column */
        renderer = gtk_cell_renderer_text_new ();
        column = gtk_tree_view_column_new ();
        gtk_tree_view_column_set_title (column, _("Played On"));
        gtk_tree_view_column_pack_start (column, renderer, TRUE);
        gtk_tree_view_column_set_cell_data_func (
                column, renderer, (GtkTreeCellDataFunc)
                play_back_render_time, NULL, NULL);
        gtk_tree_view_column_set_sort_column_id (
                column, GVA_GAME_STORE_COLUMN_TIME);
        gtk_tree_view_append_column (view, column);

        /* Title Column */
        renderer = gtk_cell_renderer_text_new ();
        column = gtk_tree_view_column_new_with_attributes (
                _("Title"), renderer, "text",
                GVA_GAME_STORE_COLUMN_TITLE, NULL);
        gtk_tree_view_column_set_sort_column_id (
                column, GVA_GAME_STORE_COLUMN_TITLE);
        gtk_tree_view_append_column (view, column);

        g_signal_connect (
                GVA_WIDGET_PLAY_BACK_WINDOW, "delete_event",
                G_CALLBACK (gtk_widget_hide_on_delete), NULL);
        g_signal_connect (
                GVA_WIDGET_PLAY_BACK_TREE_VIEW, "row-activated",
                G_CALLBACK (play_back_tree_view_row_activated_cb), NULL);
        g_signal_connect (
                GVA_WIDGET_PLAY_BACK_WINDOW, "show",
                G_CALLBACK (play_back_refresh_list), view);
        g_signal_connect (
                GVA_WIDGET_PLAY_BACK_BUTTON, "clicked",
                G_CALLBACK (play_back_clicked_cb), view);
        g_signal_connect (
                GVA_WIDGET_PLAY_BACK_DELETE_BUTTON, "clicked",
                G_CALLBACK (play_back_delete_clicked_cb), view);
        g_signal_connect_swapped (
                GVA_WIDGET_PLAY_BACK_CLOSE_BUTTON, "clicked",
                G_CALLBACK (gtk_widget_hide), window);
        g_signal_connect (
                gtk_tree_view_get_selection (view), "changed",
                G_CALLBACK (play_back_selection_changed_cb), NULL);

        play_back_selection_changed_cb (gtk_tree_view_get_selection (view));
}