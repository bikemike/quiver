#include "FileConflictCheck.h"

#include <map>
#include <set>
#include <string.h>

bool FileConflictCheck::Check(const std::vector<Mapping>& vectMappings,
		ResultList& vectResults,
		GCancellable* pCancellable,
		ProgressFn fnProgress,
		gpointer pUserData)
{
	vectResults.clear();

	const size_t n = vectMappings.size();
	if (0 == n)
	{
		return false;
	}

	ResultList results(n);
	std::map<std::string, size_t> mapFirstSeen;

	for (size_t i = 0 ; i < n ; i++)
	{
		if (NULL != pCancellable && g_cancellable_is_cancelled(pCancellable))
		{
			return false;
		}

		GFile* src = g_file_new_for_uri(vectMappings[i].strSrcURI.c_str());
		GFile* dst = g_file_new_for_uri(vectMappings[i].strDstURI.c_str());
		gchar* srcname = g_file_get_basename(src);
		gchar* dstname = g_file_get_basename(dst);

		results[i].strSrcName = (NULL != srcname) ? srcname : "";
		results[i].strDstName = (NULL != dstname) ? dstname : "";

		const std::string& mime = vectMappings[i].strContentType;
		if (!mime.empty())
		{
			gchar* szDesc = g_content_type_get_description(mime.c_str());
			gchar* szIcon = g_content_type_get_generic_icon_name(mime.c_str());
			results[i].strTypeDescription = (NULL != szDesc) ? szDesc : "";
			results[i].strIconName = (NULL != szIcon) ? szIcon : "";
			g_free(szIcon);
			g_free(szDesc);
		}

		bool bSelfMove =
			(0 == strcmp(vectMappings[i].strSrcURI.c_str(),
					vectMappings[i].strDstURI.c_str()));

		if (!bSelfMove)
		{
			std::map<std::string, size_t>::iterator itr =
				mapFirstSeen.find(vectMappings[i].strDstURI);
			if (mapFirstSeen.end() != itr)
			{
				size_t j = itr->second;
				if (!results[j].HasConflict())
				{
					results[j].strConflictWith =
						"generated for " + results[i].strSrcName;
				}
				results[i].strConflictWith =
					"generated for " + results[j].strSrcName;
			}
			else
			{
				mapFirstSeen.insert(std::make_pair(vectMappings[i].strDstURI, i));
			}
		}

		g_free(dstname);
		g_free(srcname);
		g_object_unref(dst);
		g_object_unref(src);

		if (NULL != fnProgress)
		{
			fnProgress((double)(i + 1) / (double)n, pUserData);
		}
	}

	// one listing per unique destination folder, instead of a stat()
	// per file: existing names are collected into sets
	std::map<std::string, std::set<std::string> > mapDirContents;

	for (size_t i = 0 ; i < n ; i++)
	{
		if (results[i].HasConflict() ||
			0 == strcmp(vectMappings[i].strSrcURI.c_str(),
				vectMappings[i].strDstURI.c_str()))
		{
			continue;
		}

		if (NULL != pCancellable && g_cancellable_is_cancelled(pCancellable))
		{
			return false;
		}

		GFile* dst = g_file_new_for_uri(vectMappings[i].strDstURI.c_str());
		GFile* parent = g_file_get_parent(dst);
		gchar* szBase = g_file_get_basename(dst);
		gchar* szDirURI = (NULL != parent) ? g_file_get_uri(parent) : NULL;

		bool bExists = false;
		if (NULL != szDirURI)
		{
			std::map<std::string, std::set<std::string> >::iterator itr =
				mapDirContents.find(szDirURI);
			if (mapDirContents.end() == itr)
			{
				std::set<std::string> setNames;
				GFileEnumerator* pEnum = g_file_enumerate_children(parent,
					G_FILE_ATTRIBUTE_STANDARD_NAME,
					G_FILE_QUERY_INFO_NONE, NULL, NULL);
				if (NULL != pEnum)
				{
					GFileInfo* pInfo = NULL;
					while (NULL != (pInfo =
							g_file_enumerator_next_file(pEnum, NULL, NULL)))
					{
						const char* szName = g_file_info_get_name(pInfo);
						if (NULL != szName)
						{
							setNames.insert(szName);
						}
						g_object_unref(pInfo);
					}
					g_object_unref(pEnum);
				}
				itr = mapDirContents.insert(
					std::make_pair(szDirURI, setNames)).first;
			}
			bExists = (itr->second.end() != itr->second.find(szBase));
		}

		if (bExists)
		{
			results[i].strConflictWith = "existing file";
		}

		g_free(szDirURI);
		g_free(szBase);
		g_object_unref(parent);
		g_object_unref(dst);

		if (NULL != fnProgress)
		{
			fnProgress((double)(i + 1) / (double)n, pUserData);
		}
	}

	vectResults.swap(results);

	for (size_t i = 0 ; i < vectResults.size() ; i++)
	{
		if (vectResults[i].HasConflict())
		{
			return true;
		}
	}
	return false;
}

static void conflict_list_cell_data(GtkTreeViewColumn* column,
	GtkCellRenderer* renderer, GtkTreeModel* model, GtkTreeIter* iter,
	gpointer user_data)
{ (void)column;  (void)model;  (void)iter;
	int iCol = GPOINTER_TO_INT(user_data);
	gchar* szVals[3] = { NULL, NULL, NULL };
	gtk_tree_model_get(model, iter, 0, &szVals[0], 1, &szVals[1], 2, &szVals[2], -1);
	gboolean bConflicted = (NULL != szVals[2] && '\0' != szVals[2][0]);
	const char* szText = szVals[iCol];
	if (2 == iCol && !bConflicted)
		szText = "—";
	if (bConflicted)
		g_object_set(renderer, "text", szText, "foreground", "#e01b24", NULL);
	else
		g_object_set(renderer, "text", szText, "foreground-set", FALSE, NULL);
	int k;
	for (k = 0 ; k < 3 ; k++)
		g_free(szVals[k]);
}

void FileConflictCheck::ShowResultsDialog(GtkWindow* pParent,
		const ResultList& vectResults)
{
	GtkWidget* dialog = gtk_dialog_new_with_buttons("Rename Conflicts",
		pParent, (GtkDialogFlags)(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
		"_Close", GTK_RESPONSE_CLOSE, NULL);
	gtk_window_set_default_size(GTK_WINDOW(dialog), 520, 340);

	GtkWidget* scrolled = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
		GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_widget_set_hexpand(scrolled, TRUE);
	gtk_widget_set_vexpand(scrolled, TRUE);

	GtkListStore* store = gtk_list_store_new(3,
		G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);

	for (size_t i = 0 ; i < vectResults.size() ; i++)
	{
		GtkTreeIter iter;
		gtk_list_store_append(store, &iter);
		gtk_list_store_set(store, &iter,
			0, vectResults[i].strSrcName.c_str(),
			1, vectResults[i].strDstName.c_str(),
			2, vectResults[i].strConflictWith.c_str(),
			-1);
	}

	GtkWidget* tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(tree), TRUE);
	g_object_unref(store);

	static const char* szTitles[] = { "Original Name", "New Name", "Conflicts With" };
	for (int c = 0 ; c < 3 ; c++)
	{
		GtkCellRenderer* renderer = gtk_cell_renderer_text_new();
		g_object_set(renderer, "ellipsize", PANGO_ELLIPSIZE_END, NULL);
		GtkTreeViewColumn* column = gtk_tree_view_column_new();
		gtk_tree_view_column_set_title(column, szTitles[c]);
		gtk_tree_view_column_pack_start(column, renderer, TRUE);
		gtk_tree_view_column_set_expand(column, TRUE);
		gtk_tree_view_column_set_cell_data_func(column, renderer,
			conflict_list_cell_data, GINT_TO_POINTER(c), NULL);
		gtk_tree_view_append_column(GTK_TREE_VIEW(tree), column);
	}

	GtkWidget* label = gtk_label_new(NULL);
	gtk_label_set_xalign(GTK_LABEL(label), 0.0);
	PangoAttrList* attrs = pango_attr_list_new();
	pango_attr_list_insert(attrs, pango_attr_scale_new(PANGO_SCALE_SMALL));
	gtk_label_set_attributes(GTK_LABEL(label), attrs);
	pango_attr_list_unref(attrs);
	std::string strSummary;
	size_t nConflicts = 0;
	for (size_t i = 0 ; i < vectResults.size() ; i++)
	{
		if (vectResults[i].HasConflict())
			nConflicts++;
	}
	strSummary = std::to_string(nConflicts) + " of "
		+ std::to_string(vectResults.size()) + " file(s) conflicted";
	gtk_label_set_text(GTK_LABEL(label), strSummary.c_str());

	GtkContainer* content = GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(dialog)));
	gtk_box_pack_start(GTK_BOX(content), scrolled, TRUE, TRUE, 0);
	gtk_container_add(GTK_CONTAINER(scrolled), tree);
	gtk_widget_show_all(GTK_WIDGET(content));
	gtk_box_pack_start(GTK_BOX(content), label, FALSE, FALSE, 4);
	gtk_widget_show(label);

	gtk_dialog_run(GTK_DIALOG(dialog));
	gtk_widget_destroy(dialog);
}
