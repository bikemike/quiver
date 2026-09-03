#include <gtk/gtk.h>
#include "PropertyView.h"

#include "Preferences.h"
#include "IPreferencesEventHandler.h"

#include <string>
#include <cstring>
#include <ctime>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

#include <exiv2/exiv2.hpp>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

#include "QuiverUtils.h"
#include "QuiverVideoOps.h"
#include "VideoDateEditTask.h"
#include "TaskManager.h"


/* prototypes */

static void property_value_cell_edited_callback (GtkCellRendererText *cell,
                                  gchar               *path_string,
                                  gchar               *new_text,
                                  gpointer             user_data);

static void property_video_value_cell_edited_callback (GtkCellRendererText *cell,
                                  gchar               *path_string,
                                  gchar               *new_text,
                                  gpointer             user_data);

static void property_value_editing_started_callback (GtkCellRenderer *renderer,
                                            GtkCellEditable *editable,
                                            gchar *path,
                                            gpointer user_data);

static void property_value_editing_canceled_callback (GtkCellRenderer *renderer,
                                            gpointer user_data);

static gboolean property_tree_event_popup_menu (GtkWidget *treeview, gpointer userdata);
static gboolean property_tree_event_button_press (GtkWidget *treeview, GdkEventButton *event, gpointer userdata);
static void property_tree_show_popup_menu (PropertyView::PropertyViewImpl *pImpl, GtkWidget *treeview, guint button, guint32 activate_time);

static gboolean property_date_format_is_valid(const char *date);

static void property_orientation_to_text (GtkTreeViewColumn *tree_column,
	                GtkCellRenderer   *cell,
                    GtkTreeModel      *tree_model,
	                GtkTreeIter       *iter,
                    gpointer           data);

static void property_view_map(GtkWidget *widget, gpointer user_data);
static gboolean property_view_idle_load(gpointer data);
static void property_populate_exif(PropertyView::PropertyViewImpl *pImpl);

static void property_tree_event_add_tag(GtkMenuItem *menuitem, gpointer user_data);
static void property_tree_event_remove_tag(GtkMenuItem *menuitem, gpointer user_data);

/* private implementation */
class PropertyView::PropertyViewImpl
{
public:
//	methods
	PropertyViewImpl();
	~PropertyViewImpl();

	void LoadProperties();
	void PopulateSummary();
	void PopulateXmp();
	void PopulateIptc();
	void PopulateVideo();
	void UpdateTabsForFile();

// variables
	QuiverFile    m_QuiverFile;
	std::shared_ptr<Exiv2::ExifData> m_ExifData;

	GtkWidget*    m_pNotebook;
	GtkWidget*    m_pPageWidgets[5];

	GtkWidget*    m_pSummaryTreeView;
	GtkWidget*    m_pSummaryPreview;
	GtkWidget*    m_pSummaryTitle;
	GtkWidget*    m_pExifTreeView;
	GtkWidget*    m_pXmpTreeView;
	GtkWidget*    m_pIptcTreeView;
	GtkWidget*    m_pVideoTreeView;

	guint         m_iIdleLoadID;

	gboolean      m_bLoaded;
	bool          m_bIsVideo;

	// whether the current file actually carries each metadata type;
	// tabs without data stay hidden
	bool          m_bHasExif;
	bool          m_bHasXmp;
	bool          m_bHasIptc;

// nested classes
	class PreferencesEventHandler : public IPreferencesEventHandler
	{
	public:
		PreferencesEventHandler(PropertyViewImpl* parent) {this->parent = parent;};
		virtual void HandlePreferenceChanged(PreferencesEventPtr event);
	private:
		PropertyViewImpl* parent;
	};

	IPreferencesEventHandlerPtr  m_PreferencesEventHandlerPtr;
};

enum
{
	PROP_TREE_COLUMN_KEY,
	PROP_TREE_COLUMN_NAME,
	PROP_TREE_COLUMN_VALUE_TEXT,
	PROP_TREE_COLUMN_VALUE_ORIENTATION,
	PROP_TREE_COLUMN_VALUE_PIXBUF,
	PROP_TREE_COLUMN_IS_VISIBLE_TEXT,
	PROP_TREE_COLUMN_IS_VISIBLE_PIXBUF,
	PROP_TREE_COLUMN_IS_VISIBLE_ORIENTATION,
	PROP_TREE_COLUMN_IS_GROUP,
	PROP_TREE_COLUMN_IS_EDITABLE,
	PROP_TREE_COLUMN_COUNT,
};

enum
{
	SUMMARY_COLUMN_LABEL,
	SUMMARY_COLUMN_VALUE,
	SUMMARY_COLUMN_COUNT,
};

enum
{
	KEYVALUE_COLUMN_KEY,
	KEYVALUE_COLUMN_VALUE,
	KEYVALUE_COLUMN_EDITABLE,
	KEYVALUE_COLUMN_GROUP,
	KEYVALUE_COLUMN_COUNT,
};


enum
{
  ORIENTATION_COLUMN_TEXT_VALUE,
  ORIENTATION_COLUMN_COUNT
};


const char *orientation_options[] = {
        "top - left",
        "top - right",
        "bottom - right",
        "bottom - left",
        "left - top",
        "right - top",
        "right - bottom",
        "left - bottom",
};

typedef struct _EditableTag
{
	const char* key;
	const char* title;
	int kind;
} EditableTag;

enum TagKind
{
	TAGKIND_STRING,
	TAGKIND_INT,
	TAGKIND_DATE,
	TAGKIND_ORIENTATION,
	TAGKIND_COMMENT,
};

static const EditableTag k_editableTags[] = {
	{ "Exif.Image.DateTime",             "Date and Time",        TAGKIND_DATE },
	{ "Exif.Image.Artist",               "Artist",               TAGKIND_STRING },
	{ "Exif.Image.ImageDescription",     "Image Description",    TAGKIND_STRING },
	{ "Exif.Image.ImageWidth",           "Image Width",          TAGKIND_INT },
	{ "Exif.Image.ImageLength",          "Image Length",         TAGKIND_INT },
	{ "Exif.Image.Orientation",          "Orientation",          TAGKIND_ORIENTATION },
	{ "Exif.Image.Software",             "Software",             TAGKIND_STRING },
	{ "Exif.Photo.DateTimeOriginal",     "Date Time Original",   TAGKIND_DATE },
	{ "Exif.Photo.DateTimeDigitized",    "Date Time Digitized",  TAGKIND_DATE },
	{ "Exif.Photo.PixelXDimension",      "Pixel X Dimension",    TAGKIND_INT },
	{ "Exif.Photo.PixelYDimension",      "Pixel Y Dimension",    TAGKIND_INT },
	{ "Exif.Photo.UserComment",          "User Comment",         TAGKIND_COMMENT },
	{ "Exif.Iop.RelatedImageWidth",      "Related Image Width",  TAGKIND_INT },
	{ "Exif.Iop.RelatedImageLength",     "Related Image Length", TAGKIND_INT },
};

typedef struct _KeyActionStruct
{
	PropertyView::PropertyViewImpl *pImpl;
	char* key;
} KeyActionStruct;

/* video info fetched with libavformat */
struct VideoInfo
{
	bool ok;
	std::string container;
	std::string codecs;
	double duration_seconds;
	int width, height;
	long long bit_rate;
	char creation_time[64];
	VideoInfo() : ok(false), duration_seconds(0.), width(0), height(0),
		bit_rate(0) { creation_time[0] = '\0'; }
};

static std::map<std::string, VideoInfo> s_mapVideoInfoCache;
static std::mutex s_avformatMutex;
static std::once_flag s_avformatInitFlag;

static double ParseRationalTriple(const std::string& str)
{
	double degrees = 0.;
	double factors[3] = {1., 60., 3600.};
	size_t pos = 0;
	for (int i = 0; i < 3 && std::string::npos != pos; i++)
	{
		size_t next = str.find(' ', pos);
		std::string token = (std::string::npos == next) ?
			str.substr(pos) : str.substr(pos, next - pos);
		pos = (std::string::npos == next) ? std::string::npos : next + 1;
		size_t slash = token.find('/');
		if (std::string::npos != slash)
		{
			double num = atof(token.substr(0, slash).c_str());
			double den = atof(token.substr(slash + 1).c_str());
			if (0. != den)
				degrees += (num / den) * factors[i];
		}
	}
	return degrees;
}

static VideoInfo ProbeVideoInfo(const gchar* szPath)
{
	VideoInfo info;

	if (NULL == szPath)
		return info;

	std::lock_guard<std::mutex> lock(s_avformatMutex);
	std::call_once(s_avformatInitFlag,
		[](){ av_log_set_level(AV_LOG_ERROR); });

	auto cached = s_mapVideoInfoCache.find(szPath);
	if (s_mapVideoInfoCache.end() != cached)
		return cached->second;

	AVFormatContext* pFmt = NULL;
	if (avformat_open_input(&pFmt, szPath, NULL, NULL) >= 0 && NULL != pFmt)
	{
		if (avformat_find_stream_info(pFmt, NULL) >= 0)
		{
			info.ok = true;
			if (NULL != pFmt->iformat && NULL != pFmt->iformat->name)
				info.container = pFmt->iformat->name;

			AVDictionaryEntry* e =
				av_dict_get(pFmt->metadata, "creation_time", NULL, 0);
			if (NULL == e)
				e = av_dict_get(pFmt->metadata, "date", NULL, 0);
			for (unsigned int i = 0; NULL == e && i < pFmt->nb_streams; i++)
			{
				// some muxers only tag the streams
				e = av_dict_get(pFmt->streams[i]->metadata,
					"creation_time", NULL, 0);
			}
			if (NULL != e)
			{
				g_strlcpy(info.creation_time, e->value,
					sizeof(info.creation_time));
			}

			if (pFmt->duration > 0)
				info.duration_seconds = pFmt->duration / (double)AV_TIME_BASE;
			if (pFmt->bit_rate > 0)
				info.bit_rate = pFmt->bit_rate;

			for (unsigned int i = 0; i < pFmt->nb_streams; i++)
			{
				AVStream* pStream = pFmt->streams[i];
				if (NULL == pStream || NULL == pStream->codecpar)
					continue;
				AVCodecParameters* pPar = pStream->codecpar;

				if (AVMEDIA_TYPE_VIDEO == pPar->codec_type)
				{
					info.width = pPar->width;
					info.height = pPar->height;
				}

				if (AV_CODEC_ID_NONE != pPar->codec_id && 0 != pPar->codec_id)
				{
					if (!info.codecs.empty())
						info.codecs += ", ";
					info.codecs += avcodec_get_name(pPar->codec_id);
				}
			}
		}
		avformat_close_input(&pFmt);
	}

	s_mapVideoInfoCache[szPath] = info;
	return info;
}


PropertyView::PropertyViewImpl::PropertyViewImpl() :
	m_PreferencesEventHandlerPtr ( new PreferencesEventHandler(this) )
{
	m_iIdleLoadID = 0;
	m_bLoaded     = FALSE;
	m_bIsVideo    = false;
	m_bHasExif    = false;
	m_bHasXmp     = false;
	m_bHasIptc    = false;
	m_pNotebook   = NULL;

	m_pSummaryPreview = NULL;
	m_pSummaryTitle   = NULL;

	for (int i = 0; i < 5; i++)
		m_pPageWidgets[i] = NULL;

	PreferencesPtr prefPtr = Preferences::GetInstance();
	prefPtr->AddEventHandler( m_PreferencesEventHandlerPtr );
}


PropertyView::PropertyViewImpl::~PropertyViewImpl()
{
	PreferencesPtr prefPtr = Preferences::GetInstance();
	prefPtr->RemoveEventHandler( m_PreferencesEventHandlerPtr );

	if (NULL != m_pNotebook)
	{
		g_object_unref(m_pNotebook);
	}
}

static void property_view_map(GtkWidget *widget, gpointer user_data)
{ (void)widget;
	PropertyView::PropertyViewImpl *pImpl = static_cast<PropertyView::PropertyViewImpl*>(user_data);
	if (!pImpl->m_bLoaded)
	{
		if (0 != pImpl->m_iIdleLoadID)
		{
			g_source_remove(pImpl->m_iIdleLoadID );
			pImpl->m_iIdleLoadID = 0;
		}

		pImpl->m_iIdleLoadID = g_timeout_add(10,property_view_idle_load,pImpl);
		pImpl->m_bLoaded = TRUE;
	}
}

PropertyView::PropertyView() : m_PropertyViewImplPtr ( new PropertyViewImpl() )
{
	GtkWidget *notebook = gtk_notebook_new();
	gtk_notebook_set_scrollable(GTK_NOTEBOOK(notebook), TRUE);
	gtk_notebook_popup_enable(GTK_NOTEBOOK(notebook));

	const char* labels[5] = { "Summary", "EXIF", "XMP", "IPTC", "Video" };
	GtkWidget* trees[5] = {};

	// editable values get a subtle tint so users can spot them
	GdkRGBA edit_color;
	gdk_rgba_parse(&edit_color, "#3584e4");

	for (int i = 0; i < 5; i++)
	{
		GtkWidget *treeview = gtk_tree_view_new();
		GtkWidget *scrolled_window = gtk_scrolled_window_new(NULL,NULL);
		gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window),
			GTK_POLICY_AUTOMATIC,GTK_POLICY_AUTOMATIC);
		gtk_widget_show(treeview);
		gtk_widget_show(scrolled_window);

		if (0 != i) // summary builds its own layout around the treeview
			gtk_container_add(GTK_CONTAINER(scrolled_window),treeview);

		// hold our own ref so pages can be pulled out of the notebook
		// and re-inserted later
		g_object_ref(scrolled_window);
		m_PropertyViewImplPtr->m_pPageWidgets[i] = scrolled_window;

		GtkTreeViewColumn *column;
		GtkCellRenderer *renderer;

		switch (i)
		{
			case 0: // summary: preview + filename above a plain field list
			{
				GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
				gtk_container_set_border_width(GTK_CONTAINER(vbox), 8);

				GtkWidget* preview = gtk_image_new();
				gtk_widget_set_halign(preview, GTK_ALIGN_CENTER);
				gtk_widget_set_valign(preview, GTK_ALIGN_START);
				gtk_widget_set_no_show_all(preview, TRUE);
				gtk_box_pack_start(GTK_BOX(vbox), preview, FALSE, FALSE, 0);
				m_PropertyViewImplPtr->m_pSummaryPreview = preview;

				GtkWidget* title = gtk_label_new(NULL);
				gtk_label_set_xalign(GTK_LABEL(title), 0.0);
				gtk_label_set_line_wrap(GTK_LABEL(title), TRUE);
				gtk_label_set_line_wrap_mode(GTK_LABEL(title),
					PANGO_WRAP_WORD_CHAR);
				gtk_box_pack_start(GTK_BOX(vbox), title, FALSE, FALSE, 0);
				m_PropertyViewImplPtr->m_pSummaryTitle = title;

				renderer = gtk_cell_renderer_text_new ();
				column = gtk_tree_view_column_new_with_attributes (
					"Property", renderer,
					"text", SUMMARY_COLUMN_LABEL,
					NULL);
				gtk_tree_view_column_set_resizable (column,TRUE);
				gtk_tree_view_append_column (GTK_TREE_VIEW (treeview), column);

				renderer = gtk_cell_renderer_text_new ();
				g_object_set (G_OBJECT (renderer), "wrap-width",300,  NULL);
				g_object_set (G_OBJECT (renderer),
					"wrap-mode",PANGO_WRAP_WORD_CHAR,  NULL);
				column = gtk_tree_view_column_new_with_attributes ("Value",
					renderer,
					"text", SUMMARY_COLUMN_VALUE,
					NULL);
				gtk_tree_view_column_set_resizable (column,TRUE);
				gtk_tree_view_append_column (GTK_TREE_VIEW (treeview), column);

				gtk_tree_view_set_headers_visible(
					GTK_TREE_VIEW(treeview), FALSE);

				gtk_container_add(GTK_CONTAINER(vbox), treeview);
				gtk_widget_show(vbox);
				gtk_container_add(GTK_CONTAINER(scrolled_window), vbox);
			}
			break;

			case 2: // xmp
			case 3: // iptc
			{
				renderer = gtk_cell_renderer_text_new ();
				column = gtk_tree_view_column_new_with_attributes (
					"Property", renderer,
					"text", KEYVALUE_COLUMN_KEY,
					"weight-set", KEYVALUE_COLUMN_GROUP,
					NULL);
				gtk_tree_view_column_set_resizable (column,TRUE);
				gtk_tree_view_append_column (GTK_TREE_VIEW (treeview), column);

				renderer = gtk_cell_renderer_text_new ();
				g_object_set (G_OBJECT (renderer), "wrap-width",300,  NULL);
				g_object_set (G_OBJECT (renderer),
					"wrap-mode",PANGO_WRAP_WORD_CHAR,  NULL);
				column = gtk_tree_view_column_new_with_attributes ("Value",
					renderer,
					"text", KEYVALUE_COLUMN_VALUE,
					NULL);
				gtk_tree_view_column_set_resizable (column,TRUE);
				gtk_tree_view_append_column (GTK_TREE_VIEW (treeview), column);
			}
			break;

			case 4: // video
			{
				renderer = gtk_cell_renderer_text_new ();
				column = gtk_tree_view_column_new_with_attributes ("Property",
					renderer,
					"text", KEYVALUE_COLUMN_KEY,
					NULL);
				gtk_tree_view_column_set_resizable (column,TRUE);
				gtk_tree_view_append_column (GTK_TREE_VIEW (treeview), column);

				renderer = gtk_cell_renderer_text_new ();
				g_object_set (G_OBJECT (renderer), "wrap-width",300,  NULL);
				g_object_set (G_OBJECT (renderer),
					"wrap-mode",PANGO_WRAP_WORD_CHAR,  NULL);
				g_object_set (G_OBJECT (renderer), "mode",GTK_CELL_RENDERER_MODE_EDITABLE,  NULL);
				g_object_set (G_OBJECT (renderer), "editable",TRUE,  NULL);
				g_object_set (G_OBJECT (renderer), "editable-set",TRUE,  NULL);
				g_object_set (G_OBJECT (renderer), "foreground-rgba", &edit_color,  NULL);
				column = gtk_tree_view_column_new_with_attributes ("Value",
					renderer,
					"text", KEYVALUE_COLUMN_VALUE,
					"editable-set", KEYVALUE_COLUMN_EDITABLE,
					"foreground-set", KEYVALUE_COLUMN_EDITABLE,
					NULL);
				gtk_tree_view_column_set_resizable (column,TRUE);
				gtk_tree_view_append_column (GTK_TREE_VIEW (treeview), column);

				g_signal_connect(renderer, "edited", (GCallback) property_video_value_cell_edited_callback, m_PropertyViewImplPtr.get());
				g_signal_connect(renderer, "editing-started", (GCallback) property_value_editing_started_callback, m_PropertyViewImplPtr.get());
				g_signal_connect(renderer, "editing-canceled", (GCallback) property_value_editing_canceled_callback, m_PropertyViewImplPtr.get());
			}
			break;

			case 1: // exif (editable tree)
			{
				// Property
				renderer = gtk_cell_renderer_text_new ();
				g_object_set (G_OBJECT (renderer),  "yalign", 0.0,  NULL);

				column = gtk_tree_view_column_new_with_attributes ("Property", renderer,
				  "text", PROP_TREE_COLUMN_NAME,
				  "weight-set",PROP_TREE_COLUMN_IS_GROUP,
				NULL);
				gtk_tree_view_column_set_resizable (column,TRUE);
				gtk_tree_view_append_column (GTK_TREE_VIEW (treeview), column);

				// Value
				renderer = gtk_cell_renderer_text_new ();
				g_signal_connect(renderer, "edited", (GCallback) property_value_cell_edited_callback, m_PropertyViewImplPtr.get());
				g_signal_connect(renderer, "editing-started", (GCallback) property_value_editing_started_callback, m_PropertyViewImplPtr.get());
				g_signal_connect(renderer, "editing-canceled", (GCallback) property_value_editing_canceled_callback, m_PropertyViewImplPtr.get());

				g_object_set (G_OBJECT (renderer),  "mode",GTK_CELL_RENDERER_MODE_EDITABLE,  NULL);
				g_object_set (G_OBJECT (renderer),  "foreground-rgba", &edit_color,  NULL);
				g_object_set (G_OBJECT (renderer),  "wrap-width",200,  NULL);
				g_object_set (G_OBJECT (renderer),  "wrap-mode",PANGO_WRAP_WORD_CHAR,  NULL);

				column = gtk_tree_view_column_new();
				gtk_tree_view_column_set_title(column,"Value");

				gtk_tree_view_column_pack_start (column,renderer,TRUE);
				gtk_tree_view_column_set_attributes(column,renderer,
				  "text", PROP_TREE_COLUMN_VALUE_TEXT,
				  "editable",PROP_TREE_COLUMN_IS_EDITABLE,
				  "visible",PROP_TREE_COLUMN_IS_VISIBLE_TEXT,
				  "foreground-set",PROP_TREE_COLUMN_IS_EDITABLE,
				NULL);

				renderer = gtk_cell_renderer_combo_new();

				gtk_tree_view_column_pack_end (column,renderer,FALSE);

				GtkListStore* numbers_model = gtk_list_store_new (ORIENTATION_COLUMN_COUNT, G_TYPE_STRING);
				GtkTreeIter oIter;
				for (int j = 0; j < 8; j++)
				{
					gtk_list_store_append (numbers_model, &oIter);
					gtk_list_store_set (numbers_model, &oIter,
									  ORIENTATION_COLUMN_TEXT_VALUE, orientation_options[j],
									  -1);
				}
				g_object_set (renderer,
							"model", numbers_model,
							"text-column", ORIENTATION_COLUMN_TEXT_VALUE,
							"has-entry", FALSE,
							"editable",TRUE,
							"foreground-rgba", &edit_color,
							"foreground-set",TRUE,
							NULL);
				g_object_unref(numbers_model);


				gtk_tree_view_column_add_attribute(column,renderer,"text",PROP_TREE_COLUMN_VALUE_ORIENTATION);
				gtk_tree_view_column_add_attribute(column,renderer,"visible",PROP_TREE_COLUMN_IS_VISIBLE_ORIENTATION);
				gtk_tree_view_column_set_cell_data_func(column,renderer,property_orientation_to_text,NULL,NULL);

				g_signal_connect(renderer, "edited", (GCallback) property_value_cell_edited_callback, m_PropertyViewImplPtr.get());
				g_signal_connect(renderer, "editing-started", (GCallback) property_value_editing_started_callback, m_PropertyViewImplPtr.get());
				g_signal_connect(renderer, "editing-canceled", (GCallback) property_value_editing_canceled_callback, m_PropertyViewImplPtr.get());

				renderer = gtk_cell_renderer_pixbuf_new();
				gtk_tree_view_column_pack_end (column,renderer,FALSE);

				gtk_tree_view_column_add_attribute(column,renderer,"pixbuf",PROP_TREE_COLUMN_VALUE_PIXBUF);
				gtk_tree_view_column_add_attribute(column,renderer,"visible",PROP_TREE_COLUMN_IS_VISIBLE_PIXBUF);

				gtk_tree_view_column_set_resizable (column,TRUE);
				gtk_tree_view_append_column (GTK_TREE_VIEW (treeview), column);

				g_signal_connect(treeview, "button-press-event", (GCallback) property_tree_event_button_press, m_PropertyViewImplPtr.get());
				g_signal_connect(treeview, "popup-menu", (GCallback) property_tree_event_popup_menu, m_PropertyViewImplPtr.get());
			}
			break;
		}

		gtk_notebook_append_page(GTK_NOTEBOOK(notebook), scrolled_window,
			gtk_label_new(labels[i]));

		trees[i] = treeview;
	}

	m_PropertyViewImplPtr->m_pSummaryTreeView = trees[0];
	m_PropertyViewImplPtr->m_pExifTreeView    = trees[1];
	m_PropertyViewImplPtr->m_pXmpTreeView     = trees[2];
	m_PropertyViewImplPtr->m_pIptcTreeView    = trees[3];
	m_PropertyViewImplPtr->m_pVideoTreeView   = trees[4];

	m_PropertyViewImplPtr->m_pNotebook = notebook;
	g_object_ref(m_PropertyViewImplPtr->m_pNotebook);

	gtk_widget_show(notebook);

	g_signal_connect(notebook, "map", (GCallback) property_view_map, m_PropertyViewImplPtr.get());
}

PropertyView::~PropertyView()
{
}

GtkWidget *
PropertyView::GetWidget()
{
	return m_PropertyViewImplPtr->m_pNotebook;
}

void
PropertyView::SetQuiverFile(QuiverFile quiverFile)
{
	m_PropertyViewImplPtr->m_QuiverFile = quiverFile;

	if (gtk_widget_get_mapped(m_PropertyViewImplPtr->m_pNotebook))
	{
		if (0 != m_PropertyViewImplPtr->m_iIdleLoadID)
		{
			g_source_remove(m_PropertyViewImplPtr->m_iIdleLoadID );
			m_PropertyViewImplPtr->m_iIdleLoadID = 0;
		}

		m_PropertyViewImplPtr->m_iIdleLoadID = g_timeout_add(300,property_view_idle_load,m_PropertyViewImplPtr.get());
		m_PropertyViewImplPtr->m_bLoaded = TRUE;
	}
	else
	{
		m_PropertyViewImplPtr->m_bLoaded = FALSE;
	}

}

/* misc helpers */

static GtkTreeStore* property_tree_store_create_exif(void)
{
	return gtk_tree_store_new (PROP_TREE_COLUMN_COUNT,
	 G_TYPE_STRING,
	 G_TYPE_STRING,
	 G_TYPE_STRING,
	 G_TYPE_INT,
	 GDK_TYPE_PIXBUF,
	 G_TYPE_BOOLEAN,
	 G_TYPE_BOOLEAN,
	 G_TYPE_BOOLEAN,
	 G_TYPE_BOOLEAN,
	 G_TYPE_BOOLEAN
	 );
}

static GtkTreeStore* property_tree_store_create_pair(void)
{
	return gtk_tree_store_new (KEYVALUE_COLUMN_COUNT,
	 G_TYPE_STRING,
	 G_TYPE_STRING,
	 G_TYPE_BOOLEAN,
	 G_TYPE_BOOLEAN);
}

static const EditableTag* FindEditableTag(const std::string& key)
{
	for (size_t i = 0; i < sizeof(k_editableTags)/sizeof(k_editableTags[0]); i++)
	{
		if (key == k_editableTags[i].key)
			return &k_editableTags[i];
	}
	return NULL;
}

static std::string FormatTimeT(time_t t)
{
	struct tm tv;
	localtime_r(&t, &tv);
	char buf[64];
	strftime(buf,sizeof(buf),"%Y-%m-%d %H:%M:%S",&tv);
	return buf;
}

static std::string GetGpsCoordinateString(std::shared_ptr<Exiv2::ExifData> pExifData,
	const char* coordKey, const char* refKey)
{
	try
	{
		auto itCoord = pExifData->findKey(Exiv2::ExifKey(coordKey));
		if (pExifData->end() == itCoord)
			return "";

		double degrees = ParseRationalTriple(itCoord->toString());

		std::string ref;
		if ('\0' != refKey[0])
		{
			auto itRef = pExifData->findKey(Exiv2::ExifKey(refKey));
			if (pExifData->end() != itRef)
				ref = itRef->toString();
		}

		char buf[64];
		if (!ref.empty())
			snprintf(buf,sizeof(buf),"%.6f (%s)",degrees,ref.c_str());
		else
			snprintf(buf,sizeof(buf),"%.6f",degrees);
		return buf;
	}
	catch (...)
	{
		return "";
	}
}

void PropertyView::PropertyViewImpl::PopulateSummary()
{
	GtkTreeStore* store = property_tree_store_create_pair();
	GtkTreeIter iter = {};

	auto add_row = [&](const char* label, const std::string& value)
	{
		if (value.empty())
			return;
		gtk_tree_store_append (store, &iter, NULL);
		gtk_tree_store_set (store, &iter,
			SUMMARY_COLUMN_LABEL, label,
			SUMMARY_COLUMN_VALUE, value.c_str(),
			-1);
	};

	if (NULL == m_QuiverFile.GetURI())
	{
		gtk_tree_view_set_model(GTK_TREE_VIEW(m_pSummaryTreeView),
			GTK_TREE_MODEL(store));
		gtk_image_clear(GTK_IMAGE(m_pSummaryPreview));
		gtk_widget_hide(m_pSummaryPreview);
		gtk_label_set_markup(GTK_LABEL(m_pSummaryTitle), "");
		g_object_unref(store);
		return;
	}

	// filename as the page heading
	gchar* szMarkup = g_markup_printf_escaped(
		"<big><b>%s</b></big>", m_QuiverFile.GetFileName().c_str());
	gtk_label_set_markup(GTK_LABEL(m_pSummaryTitle), szMarkup);
	g_free(szMarkup);

	// preview: embedded EXIF thumbnail for photos, poster frame for videos
	GdkPixbuf* pixbuf = NULL;
	if (m_bIsVideo)
	{
		pixbuf = QuiverVideoOps::LoadPixbuf(m_QuiverFile.GetURI());
	}
	else
	{
		pixbuf = m_QuiverFile.GetExifThumbnail();
	}

	if (NULL != pixbuf)
	{
		const int maxDim = 128;
		int w = gdk_pixbuf_get_width(pixbuf);
		int h = gdk_pixbuf_get_height(pixbuf);
		if (maxDim < w || maxDim < h)
		{
			double scale = ((double)w / (double)h > 1.0) ?
				((double)maxDim / (double)w) : ((double)maxDim / (double)h);
			GdkPixbuf* scaled = gdk_pixbuf_scale_simple(pixbuf,
				(w > 0) ? (int)(w * scale + 0.5) : 1,
				(h > 0) ? (int)(h * scale + 0.5) : 1,
				GDK_INTERP_BILINEAR);
			g_object_unref(pixbuf);
			pixbuf = scaled;
		}
	}

	if (NULL != pixbuf)
	{
		gtk_image_set_from_pixbuf(GTK_IMAGE(m_pSummaryPreview), pixbuf);
		gtk_widget_show(m_pSummaryPreview);
		g_object_unref(pixbuf);
	}
	else
	{
		gtk_widget_hide(m_pSummaryPreview);
	}

	add_row("Type", m_QuiverFile.GetMimeType());

	unsigned long long size = m_QuiverFile.GetFileSize();
	if (0 != size)
	{
		gchar* sz = g_format_size(size);
		add_row("File Size", sz);
		g_free(sz);
	}

	GFileInfo* pInfo = m_QuiverFile.GetFileInfo();
	if (NULL != pInfo)
	{
		GDateTime* pMod = g_file_info_get_modification_date_time(pInfo);
		if (NULL != pMod)
		{
			add_row("Last Modified", FormatTimeT(g_date_time_to_unix(pMod)));
			g_date_time_unref(pMod);
		}
		g_object_unref(pInfo);
	}

	if (m_bIsVideo)
	{
		gchar* szPath = g_filename_from_uri(m_QuiverFile.GetURI(), NULL, NULL);
		VideoInfo info = ProbeVideoInfo(szPath);
		g_free(szPath);

		// only claim a date when the container actually carries one;
		// GetTimeT's mtime fallback is for sorting, not display
		if ('\0' != info.creation_time[0])
			add_row("Date Taken", FormatTimeT(m_QuiverFile.GetTimeT(true)));

		if (info.ok)
		{
			int secs = (int)(info.duration_seconds + 0.5);
			gchar* dur = g_strdup_printf("%d:%02d:%02d", secs / 3600,
				(secs / 60) % 60, secs % 60);
			add_row("Duration", dur);
			g_free(dur);

			gchar* dims = g_strdup_printf("%d x %d", info.width, info.height);
			add_row("Dimensions", dims);
			g_free(dims);

			add_row("Codecs", info.codecs);
			add_row("Container", info.container);
		}
	}
	else
	{
		gchar* dims = g_strdup_printf("%d x %d",
			m_QuiverFile.GetWidth(), m_QuiverFile.GetHeight());
		add_row("Dimensions", dims);
		g_free(dims);

		add_row("Date Taken", FormatTimeT(m_QuiverFile.GetTimeT(true)));

		if (NULL != m_ExifData.get())
		{
			auto get_string = [&](const char* key) -> std::string
			{
				try
				{
					auto it = m_ExifData->findKey(Exiv2::ExifKey(key));
					if (m_ExifData->end() != it)
						return it->toString();
				}
				catch (...) {}
				return "";
			};

			add_row("Camera Make", get_string("Exif.Image.Make"));
			add_row("Camera Model", get_string("Exif.Image.Model"));
			add_row("Software", get_string("Exif.Image.Software"));
			add_row("Artist", get_string("Exif.Image.Artist"));
			add_row("GPS Latitude",
				GetGpsCoordinateString(m_ExifData,
					"Exif.GPSInfo.GPSLatitude","Exif.GPSInfo.GPSLatitudeRef"));
			add_row("GPS Longitude",
				GetGpsCoordinateString(m_ExifData,
					"Exif.GPSInfo.GPSLongitude","Exif.GPSInfo.GPSLongitudeRef"));
			add_row("GPS Altitude",
				GetGpsCoordinateString(m_ExifData,
					"Exif.GPSInfo.GPSAltitude",""));
		}
	}

	gtk_tree_view_set_model(GTK_TREE_VIEW(m_pSummaryTreeView),
		GTK_TREE_MODEL(store));
	gtk_tree_view_expand_all(GTK_TREE_VIEW(m_pSummaryTreeView));
	g_object_unref(store);
}

// insert a value at the nested path given by parts, creating group rows
// along the way (rows are matched by their KEY label)
static void property_xmp_insert(GtkTreeStore* store, GtkTreeIter* parent,
	std::vector<std::string>& parts, size_t idx, const std::string& value)
{
	GtkTreeIter iter = {};
	gboolean found = FALSE;

	if (gtk_tree_model_iter_children(GTK_TREE_MODEL(store), &iter, parent))
	{
		do
		{
			gchar* name = NULL;
			gtk_tree_model_get(GTK_TREE_MODEL(store), &iter,
				KEYVALUE_COLUMN_KEY, &name, -1);
			gboolean eq = (NULL != name && 0 == strcmp(name, parts[idx].c_str()));
			g_free(name);
			if (eq)
			{
				found = TRUE;
				break;
			}
		} while (gtk_tree_model_iter_next(GTK_TREE_MODEL(store), &iter));
	}

	if (!found)
	{
		gtk_tree_store_append(store, &iter, parent);
		gtk_tree_store_set(store, &iter,
			KEYVALUE_COLUMN_KEY, parts[idx].c_str(),
			KEYVALUE_COLUMN_VALUE, "",
			KEYVALUE_COLUMN_EDITABLE, FALSE,
			KEYVALUE_COLUMN_GROUP, (gboolean)(idx + 1 != parts.size()),
			-1);
	}

	if (idx + 1 == parts.size())
	{
		std::string display = value;
		if (display.size() > 200)
			display = display.substr(0, 197) + "...";
		gtk_tree_store_set(store, &iter,
			KEYVALUE_COLUMN_VALUE, display.c_str(), -1);
	}
	else
	{
		property_xmp_insert(store, &iter, parts, idx + 1, value);
	}
}

// one '/'-separated key segment may itself pack several levels:
// "Container.Directory[1]" -> "Container", "Directory", "[1]"
static void property_xmp_split_segment(const std::string& seg,
	std::vector<std::string>& out)
{
	size_t start = 0;
	while (true)
	{
		size_t dot = seg.find('.', start);
		std::string piece = seg.substr(start,
			(std::string::npos == dot) ?
				std::string::npos : dot - start);
		if (!piece.empty())
		{
			size_t bracket = piece.find('[');
			if (0 < bracket && std::string::npos != bracket)
			{
				out.push_back(piece.substr(0, bracket));
				out.push_back(piece.substr(bracket));
			}
			else
			{
				out.push_back(piece);
			}
		}
		if (std::string::npos == dot)
			break;
		start = dot + 1;
	}
}

static void property_populate_keyvalue_from_file(PropertyView::PropertyViewImpl* pImpl,
	GtkWidget* treeview, bool bXmp)
{
	GtkTreeStore* store = property_tree_store_create_pair();
	GtkTreeIter iter = {};

	gchar* szPath = g_filename_from_uri(
		pImpl->m_QuiverFile.GetURI(), NULL, NULL);
	if (NULL != szPath)
	{
		try
		{
			auto image = Exiv2::ImageFactory::open(szPath);
			image->readMetadata();

			if (bXmp)
			{
				Exiv2::XmpData data = image->xmpData();
				for (auto it = data.begin(); data.end() != it; ++it)
				{
					std::string key = it->key();
					const std::string szPrefix = "Xmp.";
					if (0 == key.compare(0, szPrefix.size(), szPrefix))
						key.erase(0, szPrefix.size());

					std::string value = it->toString();

					// structural placeholders ("type=Struct/Seq/Bag/Alt")
					// carry no data of their own; the children recreate
					// this hierarchy below, so skip them here
					bool bStructural = false;
					static const char* k_types[] = { "Struct", "Seq", "Bag", "Alt" };
					for (size_t i = 0; i < sizeof(k_types)/sizeof(k_types[0]); i++)
					{
						if (value == std::string("type=\"") + k_types[i] + "\"")
						{
							bStructural = true;
							break;
						}
					}
					if (bStructural)
						continue;

					// split into path levels: on '/' between struct fields,
					// then on '.' between schema/property parts, with
					// array indexes as their own level
					std::vector<std::string> expanded;
					size_t start = 0;
					while (true)
					{
						size_t slash = key.find('/', start);
						std::string seg = key.substr(start,
							(std::string::npos == slash) ?
								std::string::npos : slash - start);
						property_xmp_split_segment(seg, expanded);
						if (std::string::npos == slash)
							break;
						start = slash + 1;
					}

					// drop namespace prefixes for display ("Item:Mime" ->
					// "Mime"); the hierarchy keeps things unambiguous
					for (size_t i = 0; i < expanded.size(); i++)
					{
						size_t colon = expanded[i].find(':');
						if (0 < colon && colon + 1 < expanded[i].size())
							expanded[i].erase(0, colon + 1);
						if (!expanded[i].empty() && isalpha((unsigned char)expanded[i][0]))
							expanded[i][0] = toupper((unsigned char)expanded[i][0]);
					}

					if (!expanded.empty())
						property_xmp_insert(store, NULL, expanded, 0, value);
				}
			}
			else
			{
				Exiv2::IptcData data = image->iptcData();
				for (auto it = data.begin(); data.end() != it; ++it)
				{
					gtk_tree_store_append (store, &iter, NULL);
					gtk_tree_store_set (store, &iter,
						KEYVALUE_COLUMN_KEY, it->key().c_str(),
						KEYVALUE_COLUMN_VALUE, it->toString().c_str(),
						-1);
				}
			}
		}
		catch (...) {}
		g_free(szPath);
	}

	gtk_tree_view_set_model(GTK_TREE_VIEW(treeview), GTK_TREE_MODEL(store));
	gtk_tree_view_expand_all(GTK_TREE_VIEW(treeview));
	g_object_unref(store);
}

void PropertyView::PropertyViewImpl::PopulateXmp()
{
	property_populate_keyvalue_from_file(this,m_pXmpTreeView,true);
}

void PropertyView::PropertyViewImpl::PopulateIptc()
{
	property_populate_keyvalue_from_file(this,m_pIptcTreeView,false);
}

// poll struct: wait for the video-date task to finish, then refresh the view
struct VideoDatePoll
{
	PropertyView::PropertyViewImpl *pImpl;
	VideoDateEditTaskPtr            task;
	int                             ticks;
};

static gboolean property_video_date_poll(gpointer data)
{
	VideoDatePoll *poll = (VideoDatePoll*)data;

	if (NULL == poll->task.get())
	{
		delete poll;
		return FALSE;
	}

	if (!poll->task->IsFinished() && poll->ticks < 600) // cap ~5 min
	{
		poll->ticks++;
		return TRUE;
	}

	if (poll->task->IsFinished())
	{
		// refresh shared file state on the MAIN thread (Reload frees
		// m_szURI, which the GUI thread reads elsewhere), then repopulate
		poll->pImpl->m_QuiverFile.Reload();
		poll->pImpl->LoadProperties();
	}
	delete poll;
	return FALSE;
}

static void property_video_value_cell_edited_callback (GtkCellRendererText *cell,
                                  gchar               *path_string,
                                  gchar               *new_text,
                                  gpointer             user_data)
{ (void)cell; (void)path_string;
	PropertyView::PropertyViewImpl *pImpl = (PropertyView::PropertyViewImpl*)user_data;

	QuiverUtils::ConnectUnmodifiedAccelerators();

	if (NULL == pImpl->m_QuiverFile.GetURI())
		return;

	gchar* szPath = g_filename_from_uri(pImpl->m_QuiverFile.GetURI(), NULL, NULL);
	if (NULL == szPath)
		return;

	bool bOk = false;
	time_t new_epoch = 0;

	// accept "YYYY-MM-DD HH:MM:SS", "YYYY-MM-DDTHH:MM:SS" and
	// "YYYY:MM:DD HH:MM:SS"
	const char* sz = new_text;
	if (19 == strlen(sz))
	{
		char sep = sz[4];
		bool seps_ok = ((':' == sep || '-' == sep) &&
			sep == sz[7] &&
			(' ' == sz[10] || ('T' == sz[10] && '-' == sep)) &&
			':' == sz[13] && ':' == sz[16]);
		if (seps_ok)
		{
			struct tm tm_date = {};
			int year, month, day, hour, min, sec;
			if (6 == sscanf(sz, "%d%*c%d%*c%d%*c%d:%d:%d",
					&year, &month, &day, &hour, &min, &sec) &&
				1 <= month && month <= 12 && 1 <= day && day <= 31 &&
				0 <= hour && hour <= 23 && 0 <= min && min <= 59 &&
				0 <= sec && sec <= 60)
			{
				tm_date.tm_year = year - 1900;
				tm_date.tm_mon = month - 1;
				tm_date.tm_mday = day;
				tm_date.tm_hour = hour;
				tm_date.tm_min = min;
				tm_date.tm_sec = sec;
				tm_date.tm_isdst = -1;
				new_epoch = mktime(&tm_date);
				bOk = ((time_t)-1) != new_epoch;
			}
		}
	}

	if (bOk)
	{
		// drop the cached probe so the refreshed view re-reads from disk
		s_mapVideoInfoCache.erase(szPath);

		QuiverFile f = pImpl->m_QuiverFile;
		VideoDateEditTaskPtr taskPtr(new VideoDateEditTask(f, new_epoch));
		TaskManager::GetInstance()->AddTask(taskPtr);

		VideoDatePoll *poll = new VideoDatePoll;
		poll->pImpl = pImpl;
		poll->task = taskPtr;
		poll->ticks = 0;
		g_timeout_add(500, property_video_date_poll, poll);
	}

	g_free(szPath);
}

static void property_populate_exif(PropertyView::PropertyViewImpl *pImpl)
{
	GtkTreeStore* store = property_tree_store_create_exif();

	if (NULL != pImpl->m_ExifData.get())
	{
		GdkPixbuf *pixbuf = pImpl->m_QuiverFile.GetExifThumbnail();
		if (NULL != pixbuf)
		{
			GdkPixbuf *new_pixbuf = QuiverUtils::GdkPixbufExifReorientate(pixbuf,pImpl->m_QuiverFile.GetOrientation());
			if (NULL != new_pixbuf)
			{
				g_object_unref(pixbuf);
				pixbuf = new_pixbuf;
			}
		}

		if (NULL != pixbuf)
		{
			GtkTreeIter iter1 = {}, iter2 = {};
			gtk_tree_store_append (store, &iter1, NULL);
			gtk_tree_store_set (store, &iter1,
					PROP_TREE_COLUMN_NAME, "Exif Thumbnail",
					PROP_TREE_COLUMN_IS_GROUP, TRUE,
					PROP_TREE_COLUMN_IS_VISIBLE_TEXT, TRUE,
					-1);

			gtk_tree_store_append (store, &iter2, &iter1);
			gtk_tree_store_set (store, &iter2,
					PROP_TREE_COLUMN_NAME, "Thumbnail",
					PROP_TREE_COLUMN_VALUE_PIXBUF,pixbuf,
					PROP_TREE_COLUMN_IS_VISIBLE_PIXBUF, TRUE,
					-1);
			g_object_unref(pixbuf);
		}

		// group entries by exiv2 group name (Image, Photo, GPSInfo,
		// maker notes groups, ...)
		GtkTreeIter groupIter = {};
		std::string currentGroup;
		bool haveGroup = false;

		for (auto it = pImpl->m_ExifData->begin();
			pImpl->m_ExifData->end() != it; ++it)
		{
			const std::string& group = it->groupName();
			if (group != currentGroup || !haveGroup)
			{
				currentGroup = group;
				haveGroup = true;
				gtk_tree_store_append (store, &groupIter, NULL);
				gtk_tree_store_set (store, &groupIter,
						PROP_TREE_COLUMN_NAME, group.c_str(),
						PROP_TREE_COLUMN_IS_GROUP, TRUE,
						PROP_TREE_COLUMN_IS_VISIBLE_TEXT, TRUE,
						-1);
			}

			gboolean editable = (NULL != FindEditableTag(it->key()));

			GtkTreeIter child = {};
			gtk_tree_store_append (store, &child, &groupIter);

			if ("Exif.Image.Orientation" == it->key())
			{
				long val = 1;
				try { val = it->toInt64(); } catch (...) {}
				gtk_tree_store_set (store, &child,
					PROP_TREE_COLUMN_KEY,it->key().c_str(),
					PROP_TREE_COLUMN_NAME, it->tagName().c_str(),
					PROP_TREE_COLUMN_VALUE_ORIENTATION,(gint)val,
					PROP_TREE_COLUMN_IS_VISIBLE_ORIENTATION, TRUE,
					PROP_TREE_COLUMN_IS_EDITABLE, editable,
					-1);
			}
			else
			{
				std::string value;
				try { value = it->toString(); } catch (...) {}

				gtk_tree_store_set (store, &child,
					PROP_TREE_COLUMN_KEY,it->key().c_str(),
					PROP_TREE_COLUMN_NAME, it->tagName().c_str(),
					PROP_TREE_COLUMN_VALUE_TEXT,value.c_str(),
					PROP_TREE_COLUMN_IS_VISIBLE_TEXT, TRUE,
					PROP_TREE_COLUMN_IS_EDITABLE, editable,
					-1);
			}
		}
	}

	gtk_tree_view_set_model(GTK_TREE_VIEW(pImpl->m_pExifTreeView),
		GTK_TREE_MODEL (store));

	gtk_tree_view_expand_all (GTK_TREE_VIEW(pImpl->m_pExifTreeView));

	g_object_unref (G_OBJECT (store));
}

void PropertyView::PropertyViewImpl::UpdateTabsForFile()
{
	// built layout: Summary | EXIF | XMP | IPTC | Video
	// videos show Summary + Video; images show Summary plus whichever
	// of EXIF/XMP/IPTC the file actually carries
	GtkNotebook* nb = GTK_NOTEBOOK(m_pNotebook);

	auto find_page = [&](GtkWidget* tree) -> int
	{
		for (int i = 0; i < gtk_notebook_get_n_pages(nb); i++)
		{
			if (tree == gtk_notebook_get_nth_page(nb,i))
				return i;
		}
		return -1;
	};

	if (m_bIsVideo)
	{
		int page = find_page(m_pPageWidgets[1]); // EXIF
		if (-1 != page) gtk_notebook_remove_page(nb, page);
		page = find_page(m_pPageWidgets[2]);     // XMP
		if (-1 != page) gtk_notebook_remove_page(nb, page);
		page = find_page(m_pPageWidgets[3]);     // IPTC
		if (-1 != page) gtk_notebook_remove_page(nb, page);

		if (-1 == find_page(m_pPageWidgets[4]))  // Video
		{
			gtk_notebook_append_page(nb, m_pPageWidgets[4],
				gtk_label_new("Video"));
		}
	}
	else
	{
		int page = find_page(m_pPageWidgets[4]); // Video
		if (-1 != page) gtk_notebook_remove_page(nb, page);

		int pos = 1; // after Summary
		const int order[3] = { 1, 2, 3 };    // EXIF, XMP, IPTC
		const char* labels[3] = { "EXIF", "XMP", "IPTC" };
		const bool has[3] = { m_bHasExif, m_bHasXmp, m_bHasIptc };

		for (int i = 0; i < 3; i++)
		{
			bool present = (-1 != find_page(m_pPageWidgets[order[i]]));
			if (has[i])
			{
				if (!present)
					gtk_notebook_insert_page(nb, m_pPageWidgets[order[i]],
						gtk_label_new(labels[i]), pos);
				pos++;
			}
			else if (present)
			{
				gtk_notebook_remove_page(nb,
					find_page(m_pPageWidgets[order[i]]));
			}
		}
	}

	gtk_widget_show_all(m_pNotebook);
}

void PropertyView::PropertyViewImpl::PopulateVideo()
{
	GtkTreeStore* store = property_tree_store_create_pair();
	GtkTreeIter iter = {};

	gchar* szPath = g_filename_from_uri(m_QuiverFile.GetURI(), NULL, NULL);
	if (NULL != szPath)
	{
		VideoInfo info = ProbeVideoInfo(szPath);

		auto add = [&](const char* label, const char* value, gboolean editable)
		{
			gtk_tree_store_append(store, &iter, NULL);
			gtk_tree_store_set(store, &iter,
				KEYVALUE_COLUMN_KEY, label,
				KEYVALUE_COLUMN_VALUE, value,
				KEYVALUE_COLUMN_EDITABLE, editable,
				-1);
		};

		add("Name", m_QuiverFile.GetFileName().c_str(), FALSE);

		int secs = (int)(info.duration_seconds + 0.5);
		gchar* dur = g_strdup_printf("%d:%02d:%02d", secs / 3600,
			(secs / 60) % 60, secs % 60);
		add("Duration", dur, FALSE);
		g_free(dur);

		gchar* dims = g_strdup_printf("%d x %d", info.width, info.height);
		add("Dimensions", dims, FALSE);
		g_free(dims);

		add("Codecs", info.codecs.c_str(), FALSE);
		add("Container", info.container.c_str(), FALSE);

		if ('\0' != info.creation_time[0])
		{
			// show the raw ISO string so it can be edited back verbatim
			add("Created", info.creation_time, TRUE);
		}

		g_free(szPath);
	}

	gtk_tree_view_set_model(GTK_TREE_VIEW(m_pVideoTreeView), GTK_TREE_MODEL(store));
	gtk_tree_view_expand_all(GTK_TREE_VIEW(m_pVideoTreeView));
	g_object_unref(store);
}

void PropertyView::PropertyViewImpl::LoadProperties()
{
	m_bIsVideo = false;
	m_ExifData.reset();

	m_bHasExif = false;
	m_bHasXmp  = false;
	m_bHasIptc = false;

	if (NULL != m_QuiverFile.GetURI())
	{
		m_bIsVideo = m_QuiverFile.IsVideo();
		if (!m_bIsVideo)
		{
			m_ExifData = m_QuiverFile.GetExifData();
			m_bHasExif = (NULL != m_ExifData.get()) && !m_ExifData->empty();

			gchar* szPath = g_filename_from_uri(
				m_QuiverFile.GetURI(), NULL, NULL);
			if (NULL != szPath)
			{
				try
				{
					auto image = Exiv2::ImageFactory::open(szPath);
					image->readMetadata();
					m_bHasXmp  = !image->xmpData().empty();
					m_bHasIptc = !image->iptcData().empty();
				}
				catch (...) {}
				g_free(szPath);
			}
		}
	}

	UpdateTabsForFile();

	PopulateSummary();

	if (!m_bIsVideo && NULL != m_QuiverFile.GetURI())
	{
		property_populate_exif(this);
		PopulateXmp();
		PopulateIptc();
	}

	if (m_bIsVideo)
	{
		gchar* szPath = g_filename_from_uri(m_QuiverFile.GetURI(), NULL, NULL);
		if (NULL != szPath)
		{
			s_mapVideoInfoCache.erase(szPath);
			g_free(szPath);
		}
		PopulateVideo();
	}

	m_iIdleLoadID = 0;
}

static gboolean property_view_idle_load(gpointer data)
{
	PropertyView::PropertyViewImpl *pImpl =
		static_cast<PropertyView::PropertyViewImpl*>(data);
	pImpl->LoadProperties();
	return FALSE;
}

static void property_orientation_to_text (GtkTreeViewColumn *tree_column,
	                GtkCellRenderer   *cell,
                    GtkTreeModel      *tree_model,
	                GtkTreeIter       *iter,
                    gpointer           data)
{ (void)tree_column;  (void)data;
	gint value;

	/* Get the int value from the model. */
	gtk_tree_model_get (tree_model,iter,PROP_TREE_COLUMN_VALUE_ORIENTATION,&value,-1);
	/* Now we can format it ourselves. */
	if (value <= 8 && 0 < value)
		g_object_set (cell, "text", orientation_options[value-1], NULL);
}

static gboolean entry_focus_out ( GtkWidget *widget, GdkEventFocus *event, gpointer user_data)
{ (void)user_data;  (void)event;
	gtk_cell_editable_remove_widget(GTK_CELL_EDITABLE(widget));

	return FALSE; // false to propagate
}

static void property_value_editing_started_callback (GtkCellRenderer *renderer,
                                            GtkCellEditable *editable,
                                            gchar *path,
                                            gpointer user_data)
{ (void)path; (void)renderer;
	(void)user_data;

	QuiverUtils::DisconnectUnmodifiedAccelerators();

	g_signal_connect (G_OBJECT (editable), "focus-out-event",
    			G_CALLBACK (entry_focus_out), renderer);

	if (GTK_IS_COMBO_BOX(editable))
	{

		GtkCellLayout *layout = GTK_CELL_LAYOUT(editable);
		gtk_cell_layout_clear(layout);

		GtkCellRenderer *renderer = gtk_cell_renderer_text_new ();

		gtk_cell_layout_pack_start (layout,renderer,TRUE);
		gtk_cell_layout_set_attributes(layout,renderer,
		  "text", ORIENTATION_COLUMN_TEXT_VALUE,
		NULL);
	}

}

static void property_value_editing_canceled_callback (GtkCellRenderer *renderer,
                                            gpointer user_data)
{ (void)renderer;
	(void)user_data;

	QuiverUtils::ConnectUnmodifiedAccelerators();
}

/*
 * this routine parses a date in exif date format and checks that it is valid
 * format: YYYY:MM:DD HH:MM:SS
 */

static gboolean property_date_format_is_valid(const char *date)
{
	gboolean retval = FALSE;

	if (19 == strlen(date))
	{
		int year, month, day, hour, min, sec;
		sscanf(date,"%d:%d:%d %d:%d:%d",&year, &month, &day, &hour, &min, &sec);
		struct tm tm_date = {};
		tm_date.tm_sec = sec;
		tm_date.tm_min = min;
		tm_date.tm_hour = hour;
		tm_date.tm_mday = day;
		tm_date.tm_mon = month -1;
		tm_date.tm_year = year - 1900;
		tm_date.tm_isdst = -1;

		if ( tm_date.tm_sec == sec &&
			tm_date.tm_min == min &&
			tm_date.tm_hour == hour &&
			tm_date.tm_mday == day &&
			tm_date.tm_mon == month -1 &&
			tm_date.tm_year == year - 1900 )
		{
			retval = TRUE;
		}
	}

	return retval;
}

static void set_exif_value(std::shared_ptr<Exiv2::ExifData> pExifData,
	const char* key, const char* new_text, Exiv2::TypeId typeId)
{
	Exiv2::Exifdatum& datum = (*pExifData)[key];

	Exiv2::Value::UniquePtr value = Exiv2::Value::create(typeId);
	value->read(new_text);
	datum.setValue(value.get());
}

static void property_value_cell_edited_callback (GtkCellRendererText *cell,
                                  gchar               *path_string,
                                  gchar               *new_text,
                                  gpointer             user_data)
{ (void)cell;
	GtkTreeIter child = {};
	gboolean updated = FALSE;

	PropertyView::PropertyViewImpl *pImpl = (PropertyView::PropertyViewImpl*)user_data;

	QuiverUtils::ConnectUnmodifiedAccelerators();

	GtkTreeModel *pTreeModel = gtk_tree_view_get_model(GTK_TREE_VIEW(pImpl->m_pExifTreeView));

	std::shared_ptr<Exiv2::ExifData> pExifData = pImpl->m_ExifData;

	GtkTreePath* path = gtk_tree_path_new_from_string(path_string);
	gtk_tree_model_get_iter(pTreeModel,&child,path);
	gtk_tree_path_free(path);

	gchar* key = NULL;
	gtk_tree_model_get (pTreeModel,&child,PROP_TREE_COLUMN_KEY,&key,-1);

	if (NULL == key || NULL == pExifData.get())
	{
		if (NULL != key) g_free(key);
		return;
	}

	const EditableTag* tag = FindEditableTag(key);
	if (NULL == tag)
	{
		g_free(key);
		return;
	}

	switch (tag->kind)
	{
		case TAGKIND_ORIENTATION:
		{
			for (int i=0;i<8;i++)
			{
				if ( !strcmp(new_text,orientation_options[i]) )
				{
					gchar szVal[16];
					snprintf(szVal,sizeof(szVal),"%d",i+1);
					set_exif_value(pExifData, key, szVal, Exiv2::unsignedShort);
					updated = TRUE;
					break;
				}
			}
		}
		break;

		case TAGKIND_INT:
		{
			gchar* szv = g_strdup_printf("%ld", atol(new_text));
			set_exif_value(pExifData, key, szv, Exiv2::unsignedLong);
			g_free(szv);
			updated = TRUE;
		}
		break;

		case TAGKIND_DATE:
		{
			//"YYYY:MM:DD HH:MM:SS"
			if ( property_date_format_is_valid(new_text) )
			{
				set_exif_value(pExifData, key, new_text, Exiv2::asciiString);
				updated = TRUE;
			}
		}
		break;

		case TAGKIND_COMMENT:
		{
			set_exif_value(pExifData, key, new_text, Exiv2::comment);
			updated = TRUE;
		}
		break;

		case TAGKIND_STRING:
		default:
		{
			set_exif_value(pExifData, key, new_text, Exiv2::asciiString);
			updated = TRUE;
		}
		break;
	}

	if (updated)
	{
		pImpl->m_QuiverFile.SetExifData(pExifData);

		if (0 == strcmp(tag->key,"Exif.Image.Orientation"))
		{
			// thumbnails are cached per-orientation and the thumbnail
			// preview row changes; rebuild the whole tree
			property_populate_exif(pImpl);
		}
		else
		{
			// refresh just this row's displayed value
			std::string value;
			try
			{
				auto it = pExifData->findKey(Exiv2::ExifKey(key));
				if (pExifData->end() != it)
					value = it->toString();
			}
			catch (...) {}
			gtk_tree_store_set (GTK_TREE_STORE(pTreeModel), &child,
				PROP_TREE_COLUMN_VALUE_TEXT,value.c_str(), -1);
		}
	}
	g_free(key);
}

static gboolean property_tree_event_popup_menu (GtkWidget *treeview, gpointer userdata)
{
	PropertyView::PropertyViewImpl *pImpl = (PropertyView::PropertyViewImpl*)userdata;
	property_tree_show_popup_menu(pImpl,treeview, 0, gtk_get_current_event_time());
	return TRUE; /* we handled this */
}

static gboolean
property_tree_event_button_press (GtkWidget *treeview, GdkEventButton *event, gpointer userdata)
{
	PropertyView::PropertyViewImpl *pImpl = (PropertyView::PropertyViewImpl*)userdata;

	/* single click with the right mouse button? */
	if (event->type == GDK_BUTTON_PRESS  &&  event->button == 3)
	{

		GtkTreeSelection *selection;
		selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(treeview));

		if (NULL != selection)
		{
			if (gtk_tree_selection_count_selected_rows(selection)  <= 1)
			{
				GtkTreePath *path;
				/* Get tree path for row that was clicked */
				GtkTreeViewColumn *column;
				if (gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(treeview),
										 (gint) event->x,
										 (gint) event->y,
										 &path, &column, NULL, NULL))
				{
					gtk_tree_view_set_cursor(GTK_TREE_VIEW(treeview),path,NULL,FALSE);
					gtk_tree_selection_unselect_all(selection);
					gtk_tree_selection_select_path(selection, path);
					gtk_tree_path_free(path);
				}
			}
		}
		property_tree_show_popup_menu(pImpl,treeview, event->button, gdk_event_get_time((GdkEvent*)event));
		return TRUE;
	}
	return FALSE;
}

static void
property_tree_show_popup_menu (PropertyView::PropertyViewImpl *pImpl, GtkWidget *treeview, guint button, guint32 activate_time)
{ (void)activate_time;  (void)button;
	GtkWidget *menu=NULL, *submenu=NULL, *menuitem;

	GtkTreePath *path;
	GtkTreeViewColumn *column;
	GtkTreeIter iter;
	std::string selectedKey;
	gboolean is_group = TRUE;

	gtk_tree_view_get_cursor ( GTK_TREE_VIEW(treeview),&path,&column );
	GtkTreeModel *model = gtk_tree_view_get_model (GTK_TREE_VIEW(treeview));

	if (NULL != path)
	{
		menu = gtk_menu_new();
		gtk_tree_model_get_iter(GTK_TREE_MODEL(model),&iter,path);

		gchar* key = NULL;
		gtk_tree_model_get (GTK_TREE_MODEL(model),&iter,
			PROP_TREE_COLUMN_KEY,&key,
			PROP_TREE_COLUMN_IS_GROUP,&is_group,
			-1);
		if (NULL != key)
		{
			selectedKey = key;
			is_group = FALSE;
			g_free(key);
		}

		// determine the group this row belongs to
		std::string groupName;
		if (is_group)
		{
			gchar* name = NULL;
			gtk_tree_model_get (GTK_TREE_MODEL(model),&iter,
				PROP_TREE_COLUMN_NAME,&name,-1);
			if (NULL != name)
			{
				groupName = name;
				g_free(name);
			}
		}
		else
		{
			try
			{
				groupName = Exiv2::ExifKey(selectedKey).groupName();
			}
			catch (...) {}
		}

		if (!groupName.empty())
		{
			// offer adding any missing editable tag from this group
			bool bAddedHeader = false;
			for (size_t i = 0;
				i < sizeof(k_editableTags)/sizeof(k_editableTags[0]); i++)
			{
				const EditableTag* t = &k_editableTags[i];
				try
				{
					if (groupName != Exiv2::ExifKey(t->key).groupName())
						continue;
				}
				catch (...) { continue; }

				if (pImpl->m_ExifData->end() !=
					pImpl->m_ExifData->findKey(Exiv2::ExifKey(t->key)))
					continue;

				if (!bAddedHeader)
				{
					bAddedHeader = true;
					menuitem = gtk_menu_item_new_with_label("Add Tag");
					gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);
					submenu = gtk_menu_new();
					gtk_menu_item_set_submenu(GTK_MENU_ITEM(menuitem),submenu);
				}

				menuitem = gtk_menu_item_new_with_label(t->title);
				gtk_menu_shell_append(GTK_MENU_SHELL(submenu), menuitem);

				KeyActionStruct *data = g_new(KeyActionStruct,1);
				data->pImpl = pImpl;
				data->key = g_strdup(t->key);
				g_signal_connect(menuitem, "activate",
					 (GCallback) property_tree_event_add_tag, data);
			}

			if (!is_group)
			{
				menuitem = gtk_menu_item_new_with_label("Remove Tag");
				gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);

				KeyActionStruct *data = g_new(KeyActionStruct,1);
				data->pImpl = pImpl;
				data->key = g_strdup(selectedKey.c_str());
				g_signal_connect(menuitem, "activate",
								 (GCallback) property_tree_event_remove_tag, data);
			}
		}

		if (NULL != submenu || !selectedKey.empty())
		{
			gtk_widget_show_all(menu);
			gtk_menu_popup_at_pointer(GTK_MENU(menu), NULL);
		}
		else
		{
			gtk_widget_destroy(menu);
		}
	}
}

static void
property_tree_event_add_tag(GtkMenuItem *menuitem, gpointer user_data)
{ (void)menuitem;
	KeyActionStruct *data = (KeyActionStruct*)user_data;
	PropertyView::PropertyViewImpl *pImpl = data->pImpl;
	std::shared_ptr<Exiv2::ExifData> pExifData = pImpl->m_ExifData;

	if (NULL != pExifData.get())
	{
		const EditableTag* tag = FindEditableTag(data->key);
		if (NULL != tag)
		{
			// sensible defaults per kind
			switch (tag->kind)
			{
				case TAGKIND_DATE:
				{
					time_t now = time(NULL);
					struct tm tv;
					localtime_r(&now,&tv);
					gchar szDate[32];
					strftime(szDate,sizeof(szDate),"%Y:%m:%d %H:%M:%S",&tv);
					set_exif_value(pExifData,data->key,szDate,Exiv2::asciiString);
				}
				break;

				case TAGKIND_INT:
					set_exif_value(pExifData,data->key,"0",Exiv2::unsignedLong);
				break;

				case TAGKIND_ORIENTATION:
					set_exif_value(pExifData,data->key,"1",Exiv2::unsignedShort);
				break;

				case TAGKIND_COMMENT:
					set_exif_value(pExifData,data->key,"",Exiv2::comment);
				break;

				case TAGKIND_STRING:
				default:
					set_exif_value(pExifData,data->key,"",Exiv2::asciiString);
				break;
			}

			pImpl->m_QuiverFile.SetExifData(pExifData);

			property_populate_exif(pImpl);
		}
	}
	g_free(data->key);
	g_free(data);
}

static void
property_tree_event_remove_tag(GtkMenuItem *menuitem, gpointer user_data)
{ (void)menuitem;
	KeyActionStruct *data = (KeyActionStruct*)user_data;
	PropertyView::PropertyViewImpl *pImpl = data->pImpl;
	std::shared_ptr<Exiv2::ExifData> pExifData = pImpl->m_ExifData;

	if (NULL != pExifData.get())
	{
		try
		{
			auto it = pExifData->findKey(Exiv2::ExifKey(data->key));
			if (pExifData->end() != it)
			{
				pExifData->erase(it);
				pImpl->m_QuiverFile.SetExifData(pExifData);

				property_populate_exif(pImpl);
			}
		}
		catch (...) {}
	}
	g_free(data->key);
	g_free(data);
}

// nested class methods

void PropertyView::PropertyViewImpl::PreferencesEventHandler::HandlePreferenceChanged(PreferencesEventPtr event)
{ (void)event;

}
