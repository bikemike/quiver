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

// row item type for the conflicts column view
typedef struct {
	GObject  parent_instance;
	gchar*   src_name;
	gchar*   dst_name;
	gchar*   conflict_with;
} ConflictItem;

typedef struct {
	GObjectClass parent_class;
} ConflictItemClass;

#define CONFLICT_ITEM_TYPE (conflict_item_get_type())
#define CONFLICT_ITEM(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST((obj), CONFLICT_ITEM_TYPE, ConflictItem))

G_DEFINE_TYPE(ConflictItem, conflict_item, G_TYPE_OBJECT)

static void conflict_item_finalize (GObject* object)
{
	ConflictItem* item = CONFLICT_ITEM(object);
	g_free(item->src_name);
	g_free(item->dst_name);
	g_free(item->conflict_with);
	G_OBJECT_CLASS(g_type_class_peek_parent(
		G_OBJECT_GET_CLASS(object)))->finalize(object);
}

static void conflict_item_class_init (ConflictItemClass* klass)
{
	G_OBJECT_CLASS(klass)->finalize = conflict_item_finalize;
}

static void conflict_item_init (ConflictItem* item)
{
	item->src_name = NULL;
	item->dst_name = NULL;
	item->conflict_with = NULL;
}

static ConflictItem* conflict_item_new (const gchar* src, const gchar* dst,
	const gchar* conflict)
{
	ConflictItem* item = static_cast<ConflictItem*>(
		g_object_new(CONFLICT_ITEM_TYPE, NULL));
	item->src_name = g_strdup(src);
	item->dst_name = g_strdup(dst);
	item->conflict_with = g_strdup(conflict);
	return item;
}

static void conflict_row_setup (GtkListItem* list_item, gpointer user_data)
{ (void)user_data;
	GtkWidget* label = gtk_label_new(NULL);
	gtk_label_set_xalign(GTK_LABEL(label), 0.0);
	gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
	gtk_widget_set_hexpand(label, TRUE);
	gtk_list_item_set_child(list_item, label);
}

static void conflict_row_bind (GtkListItem* list_item, gpointer user_data)
{
	int iCol = GPOINTER_TO_INT(user_data);
	ConflictItem* item = CONFLICT_ITEM(gtk_list_item_get_item(list_item));
	GtkWidget* label = gtk_list_item_get_child(list_item);
	gboolean bConflicted = (NULL != item->conflict_with && '\0' != item->conflict_with[0]);
	const char* szText = NULL;
	switch (iCol)
	{
		case 0: szText = item->src_name; break;
		case 1: szText = item->dst_name; break;
		default: szText = bConflicted ? item->conflict_with : "—"; break;
	}
	gtk_label_set_text(GTK_LABEL(label), szText);
	gtk_widget_set_margin_end(label, 6);
	gtk_widget_remove_css_class(GTK_WIDGET(label), "dim-label");
	if (bConflicted)
	{
		gchar* esc = g_markup_escape_text(szText, -1);
		gchar* markup = g_strdup_printf("<span foreground=\"#e01b24\">%s</span>", esc);
		gtk_label_set_markup(GTK_LABEL(label), markup);
		g_free(markup);
		g_free(esc);
	}
	else
		gtk_widget_add_css_class(GTK_WIDGET(label), "dim-label");
}

static GtkListItemFactory* conflict_column_factory (int iCol)
{
	GtkListItemFactory* factory = gtk_signal_list_item_factory_new();
	g_signal_connect(factory, "setup", G_CALLBACK(conflict_row_setup), NULL);
	g_signal_connect(factory, "bind", G_CALLBACK(conflict_row_bind),
		GINT_TO_POINTER(iCol));
	return factory;
}

void FileConflictCheck::ShowResultsDialog(GtkWindow* pParent,
		const ResultList& vectResults)
{
	GtkWidget* dialog = gtk_window_new();
	gtk_window_set_title(GTK_WINDOW(dialog), "Rename Conflicts");
	gtk_window_set_default_size(GTK_WINDOW(dialog), 520, 340);
	if (pParent)
		gtk_window_set_transient_for(GTK_WINDOW(dialog), pParent);

	GtkWidget* content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
	gtk_window_set_child(GTK_WINDOW(dialog), content);

	GtkWidget* scrolled = gtk_scrolled_window_new();
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
		GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_widget_set_halign(scrolled, GTK_ALIGN_FILL);
	gtk_widget_set_valign(scrolled, GTK_ALIGN_FILL);
	gtk_widget_set_hexpand(scrolled, TRUE);
	gtk_widget_set_vexpand(scrolled, TRUE);

	GListStore* store = g_list_store_new(CONFLICT_ITEM_TYPE);

	for (size_t i = 0 ; i < vectResults.size() ; i++)
	{
		ConflictItem* item = conflict_item_new(
			vectResults[i].strSrcName.c_str(),
			vectResults[i].strDstName.c_str(),
			vectResults[i].strConflictWith.c_str());
		g_list_store_append(store, item);
		g_object_unref(item);
	}

	GtkSingleSelection* sel = gtk_single_selection_new(G_LIST_MODEL(store));
	g_object_unref(store);

	GtkWidget* view = gtk_column_view_new(GTK_SELECTION_MODEL(sel));
	g_object_unref(sel);

	static const char* szTitles[] = { "Original Name", "New Name", "Conflicts With" };
	for (int c = 0 ; c < 3 ; c++)
	{
		GtkColumnViewColumn* column =
			gtk_column_view_column_new(szTitles[c], conflict_column_factory(c));
		gtk_column_view_column_set_expand(column, TRUE);
		gtk_column_view_append_column(GTK_COLUMN_VIEW(view), column);
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

	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), view);
	gtk_box_append(GTK_BOX(content), scrolled);
	gtk_box_append(GTK_BOX(content), label);

	GtkWidget* btnClose = gtk_button_new_with_mnemonic("_Close");
	g_signal_connect_swapped(btnClose, "clicked",
		G_CALLBACK(gtk_window_destroy), dialog);
	GtkHeaderBar* hbar = GTK_HEADER_BAR(gtk_header_bar_new());
	gtk_header_bar_set_show_title_buttons(GTK_HEADER_BAR(hbar), TRUE);
	gtk_header_bar_pack_end(hbar, btnClose);
	gtk_window_set_titlebar(GTK_WINDOW(dialog), GTK_WIDGET(hbar));

	gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
	gtk_widget_set_visible(GTK_WIDGET(dialog), TRUE);
}
