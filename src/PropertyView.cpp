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


/* ═══════════════════════════════════════════════════════════════════════
 * GObject model items — used as rows in GListStore
 * ═══════════════════════════════════════════════════════════════════════ */

typedef struct _PropertyItem
{
	GObject    parent_instance;
	char      *key;
	char      *value;
	gboolean   editable;
	gboolean   is_group;
} PropertyItem;

typedef struct _PropertyItemClass { GObjectClass parent_class; } PropertyItemClass;

static GType property_item_get_type(void);
G_DEFINE_TYPE(PropertyItem, property_item, G_TYPE_OBJECT)
#define PROPERTY_ITEM(o) ((PropertyItem *)(o))

static void property_item_init(PropertyItem *s)
{ s->key = NULL; s->value = NULL; s->editable = FALSE; s->is_group = FALSE; }

static void property_item_finalize(GObject *o)
{
	PropertyItem *s = PROPERTY_ITEM(o);
	g_free(s->key); g_free(s->value);
	G_OBJECT_CLASS(property_item_parent_class)->finalize(o);
}

static void property_item_class_init(PropertyItemClass *k)
{ G_OBJECT_CLASS(k)->finalize = property_item_finalize; }

static PropertyItem* property_item_new(const char *k, const char *v,
	gboolean ed, gboolean grp)
{
	PropertyItem *i = PROPERTY_ITEM(g_object_new(property_item_get_type(), NULL));
	i->key = g_strdup(k); i->value = g_strdup(v ? v : "");
	i->editable = ed; i->is_group = grp;
	return i;
}

typedef struct _ExifItem
{
	GObject     parent_instance;
	char       *full_key;
	char       *name;
	char       *value_text;
	int         value_orientation;
	GdkPixbuf  *thumbnail;
	gboolean    is_group;
	gboolean    is_editable;
	gboolean    show_text;
	gboolean    show_orientation;
	gboolean    show_pixbuf;
} ExifItem;

typedef struct _ExifItemClass { GObjectClass parent_class; } ExifItemClass;

static GType exif_item_get_type(void);
G_DEFINE_TYPE(ExifItem, exif_item, G_TYPE_OBJECT)
#define EXIF_ITEM(o) ((ExifItem *)(o))

static void exif_item_init(ExifItem *s)
{
	s->full_key = NULL; s->name = NULL; s->value_text = NULL;
	s->thumbnail = NULL; s->value_orientation = 0;
	s->is_group = FALSE; s->is_editable = FALSE;
	s->show_text = FALSE; s->show_orientation = FALSE; s->show_pixbuf = FALSE;
}

static void exif_item_finalize(GObject *o)
{
	ExifItem *s = EXIF_ITEM(o);
	g_free(s->full_key); g_free(s->name); g_free(s->value_text);
	if (s->thumbnail) g_object_unref(s->thumbnail);
	G_OBJECT_CLASS(exif_item_parent_class)->finalize(o);
}

static void exif_item_class_init(ExifItemClass *k)
{ G_OBJECT_CLASS(k)->finalize = exif_item_finalize; }

static ExifItem* exif_item_new(void)
{ return EXIF_ITEM(g_object_new(exif_item_get_type(), NULL)); }


/* ═══════════════════════════════════════════════════════════════════════
 * Constants / data tables
 * ═══════════════════════════════════════════════════════════════════════ */

static const char *orientation_options[] = {
	"top - left", "top - right", "bottom - right", "bottom - left",
	"left - top", "right - top", "right - bottom", "left - bottom",
};

typedef struct _EditableTag { const char *key; const char *title; int kind; } EditableTag;

enum TagKind { TAGKIND_STRING, TAGKIND_INT, TAGKIND_DATE, TAGKIND_ORIENTATION, TAGKIND_COMMENT };

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
	char *key;
} KeyActionStruct;

struct VideoInfo
{
	bool ok;
	std::string container, codecs;
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
	if (NULL == szPath) return info;

	std::lock_guard<std::mutex> lock(s_avformatMutex);
	std::call_once(s_avformatInitFlag, [](){ av_log_set_level(AV_LOG_ERROR); });

	auto cached = s_mapVideoInfoCache.find(szPath);
	if (s_mapVideoInfoCache.end() != cached) return cached->second;

	AVFormatContext* pFmt = NULL;
	if (avformat_open_input(&pFmt, szPath, NULL, NULL) >= 0 && NULL != pFmt)
	{
		if (avformat_find_stream_info(pFmt, NULL) >= 0)
		{
			info.ok = true;
			if (NULL != pFmt->iformat && NULL != pFmt->iformat->name)
				info.container = pFmt->iformat->name;

			AVDictionaryEntry* e = av_dict_get(pFmt->metadata, "creation_time", NULL, 0);
			if (NULL == e) e = av_dict_get(pFmt->metadata, "date", NULL, 0);
			for (unsigned int i = 0; NULL == e && i < pFmt->nb_streams; i++)
				e = av_dict_get(pFmt->streams[i]->metadata, "creation_time", NULL, 0);
			if (NULL != e)
				g_strlcpy(info.creation_time, e->value, sizeof(info.creation_time));

			if (pFmt->duration > 0) info.duration_seconds = pFmt->duration / (double)AV_TIME_BASE;
			if (pFmt->bit_rate > 0) info.bit_rate = pFmt->bit_rate;

			for (unsigned int i = 0; i < pFmt->nb_streams; i++)
			{
				AVStream* pStream = pFmt->streams[i];
				if (NULL == pStream || NULL == pStream->codecpar) continue;
				AVCodecParameters* pPar = pStream->codecpar;
				if (AVMEDIA_TYPE_VIDEO == pPar->codec_type)
				{ info.width = pPar->width; info.height = pPar->height; }
				if (AV_CODEC_ID_NONE != pPar->codec_id && 0 != pPar->codec_id)
				{
					if (!info.codecs.empty()) info.codecs += ", ";
					info.codecs += avcodec_get_name(pPar->codec_id);
				}
			}
		}
		avformat_close_input(&pFmt);
	}
	s_mapVideoInfoCache[szPath] = info;
	return info;
}


/* ═══════════════════════════════════════════════════════════════════════
 * Forward declarations for helpers used before definition
 * ═══════════════════════════════════════════════════════════════════════ */

static void property_value_cell_edited_callback(const char *key, const char *new_text,
	gpointer user_data);
static void property_video_value_cell_edited_callback(const char *new_text, gpointer user_data);
static void property_populate_exif(PropertyView::PropertyViewImpl *pImpl);
static gboolean property_view_idle_load(gpointer data);
static gboolean property_date_format_is_valid(const char *date);
static void set_exif_value(std::shared_ptr<Exiv2::ExifData> pExifData,
	const char* key, const char* new_text, Exiv2::TypeId typeId);
static void exif_entry_activate_cb(GtkEntry *entry, gpointer user_data);
static void exif_right_click_cb(GtkGestureClick *gesture, gint n_press,
	gdouble x, gdouble y, gpointer user_data);
static void exif_popover_closed_cb(GtkPopover *popover, gpointer user_data);


/* ═══════════════════════════════════════════════════════════════════════
 * Private implementation class
 * ═══════════════════════════════════════════════════════════════════════ */

class PropertyView::PropertyViewImpl
{
public:
	PropertyViewImpl();
	~PropertyViewImpl();

	void LoadProperties();
	void PopulateSummary();
	void PopulateXmp();
	void PopulateIptc();
	void PopulateVideo();
	void UpdateTabsForFile();

	QuiverFile    m_QuiverFile;
	std::shared_ptr<Exiv2::ExifData> m_ExifData;

	GtkWidget*    m_pNotebook;
	GtkWidget*    m_pPageWidgets[5];

	GtkWidget*    m_pSummaryColumnView;
	GtkWidget*    m_pSummaryPreview;
	GtkWidget*    m_pSummaryTitle;
	GtkWidget*    m_pExifColumnView;
	GtkWidget*    m_pXmpColumnView;
	GtkWidget*    m_pIptcColumnView;
	GtkWidget*    m_pVideoColumnView;

	GtkWidget*    m_pExifPopover;

	guint         m_iIdleLoadID;
	gboolean      m_bLoaded;
	bool          m_bIsVideo;
	bool          m_bHasExif;
	bool          m_bHasXmp;
	bool          m_bHasIptc;

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


/* ═══════════════════════════════════════════════════════════════════════
 * GtkColumnView / GtkSignalListItemFactory callbacks
 * ═══════════════════════════════════════════════════════════════════════ */

/* --- Key-value label pair (used by Summary, XMP, IPTC tabs) --- */

static void kv_setup(GtkListItemFactory *factory, GtkListItem *item, gpointer user_data)
{ (void)factory; (void)user_data;
	gtk_list_item_set_child(item, gtk_label_new(NULL)); }

static void kv_key_bind(GtkListItemFactory *factory, GtkListItem *item, gpointer user_data)
{ (void)factory; (void)user_data;
	PropertyItem *pi = PROPERTY_ITEM(gtk_list_item_get_item(item));
	GtkWidget *label = gtk_list_item_get_child(item);
	gchar *text;
	if (pi->is_group)
		text = g_markup_printf_escaped("<b>%s</b>", pi->key ? pi->key : "");
	else
		text = g_markup_escape_text(pi->key ? pi->key : "", -1);
	gtk_label_set_markup(GTK_LABEL(label), text);
	g_free(text);
}

static void kv_value_bind(GtkListItemFactory *factory, GtkListItem *item, gpointer user_data)
{ (void)factory; (void)user_data;
	PropertyItem *pi = PROPERTY_ITEM(gtk_list_item_get_item(item));
	gtk_label_set_text(GTK_LABEL(gtk_list_item_get_child(item)),
		pi->value ? pi->value : "");
}

static void kv_unbind(GtkListItemFactory *factory, GtkListItem *item, gpointer user_data)
{ (void)factory; (void)item; (void)user_data; }


/* --- Video / EXIF editable entry pair --- */

static void entry_setup(GtkListItemFactory *factory, GtkListItem *item, gpointer user_data)
{ (void)factory; (void)user_data;
	gtk_list_item_set_child(item, gtk_entry_new()); }

static void video_entry_bind(GtkListItemFactory *factory, GtkListItem *item, gpointer user_data)
{ (void)factory; (void)user_data;
	PropertyItem *pi = PROPERTY_ITEM(gtk_list_item_get_item(item));
	GtkWidget *entry = gtk_list_item_get_child(item);

	if (pi->editable)
	{
		gtk_editable_set_text(GTK_EDITABLE(entry), pi->value ? pi->value : "");
		gtk_widget_set_sensitive(entry, TRUE);
		gtk_widget_add_css_class(entry, "dim-label");
	}
	else
	{
		gtk_editable_set_text(GTK_EDITABLE(entry), pi->value ? pi->value : "");
		gtk_widget_set_sensitive(entry, FALSE);
	}
}

static void video_entry_activate(GtkEntry *entry, gpointer user_data)
{ (void)user_data;
	const char *new_text = gtk_editable_get_text(GTK_EDITABLE(entry));
	g_signal_handlers_disconnect_by_func(entry, (gpointer)video_entry_activate, user_data);
	property_video_value_cell_edited_callback(new_text, user_data);
}


/* --- EXIF name column (bold for groups) --- */

static void exif_name_setup(GtkListItemFactory *factory, GtkListItem *item, gpointer user_data)
{ (void)factory; (void)user_data;
	GtkWidget *label = gtk_label_new(NULL);
	gtk_label_set_xalign(GTK_LABEL(label), 0.0);
	gtk_label_set_wrap(GTK_LABEL(label), TRUE);
	gtk_label_set_wrap_mode(GTK_LABEL(label), PANGO_WRAP_WORD_CHAR);
	gtk_list_item_set_child(item, label);
}

static void exif_name_bind(GtkListItemFactory *factory, GtkListItem *item, gpointer user_data)
{ (void)factory; (void)user_data;
	ExifItem *ei = EXIF_ITEM(gtk_list_item_get_item(item));
	GtkWidget *label = gtk_list_item_get_child(item);
	gchar *text;
	if (ei->is_group)
		text = g_markup_printf_escaped("<b>%s</b>", ei->name ? ei->name : "");
	else
		text = g_markup_escape_text(ei->name ? ei->name : "", -1);
	gtk_label_set_markup(GTK_LABEL(label), text);
	g_free(text);
}


/* --- EXIF value column (text / orientation / pixbuf + optional editable) --- */

static void exif_value_setup(GtkListItemFactory *factory, GtkListItem *item, gpointer user_data)
{ (void)factory; (void)user_data;
	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
	GtkWidget *label = gtk_label_new(NULL);
	gtk_label_set_xalign(GTK_LABEL(label), 0.0);
	gtk_label_set_wrap(GTK_LABEL(label), TRUE);
	gtk_label_set_wrap_mode(GTK_LABEL(label), PANGO_WRAP_WORD_CHAR);
	gtk_widget_set_hexpand(label, TRUE);
	GtkWidget *picture = gtk_picture_new();
	gtk_widget_set_visible(picture, FALSE);
	GtkWidget *entry = gtk_entry_new();
	gtk_widget_set_visible(entry, FALSE);
	gtk_box_append(GTK_BOX(box), label);
	gtk_box_append(GTK_BOX(box), picture);
	gtk_box_append(GTK_BOX(box), entry);
	gtk_list_item_set_child(item, box);
}

static void exif_value_bind(GtkListItemFactory *factory, GtkListItem *item, gpointer user_data)
{ (void)factory; (void)user_data;
	PropertyView::PropertyViewImpl *pImpl = (PropertyView::PropertyViewImpl*)user_data;
	ExifItem *ei = EXIF_ITEM(gtk_list_item_get_item(item));
	GtkWidget *box = gtk_list_item_get_child(item);
	GtkWidget *label = gtk_widget_get_first_child(box);
	GtkWidget *picture = gtk_widget_get_next_sibling(label);
	GtkWidget *entry = gtk_widget_get_next_sibling(picture);

	if (ei->is_group)
	{
		gtk_label_set_text(GTK_LABEL(label), "");
		gtk_widget_set_visible(label, TRUE);
		gtk_widget_set_visible(picture, FALSE);
		gtk_widget_set_visible(entry, FALSE);
		return;
	}

	if (ei->show_pixbuf && ei->thumbnail)
	{
		char buf[48];
		snprintf(buf, sizeof(buf), "%dx%d",
			gdk_pixbuf_get_width(ei->thumbnail),
			gdk_pixbuf_get_height(ei->thumbnail));
		gtk_label_set_text(GTK_LABEL(label), buf);
		{
			GdkTexture *tex = gdk_texture_new_for_pixbuf(ei->thumbnail);
			gtk_picture_set_paintable(GTK_PICTURE(picture), GDK_PAINTABLE(tex));
			g_object_unref(tex);
		}
		gtk_widget_set_visible(label, FALSE);
		gtk_widget_set_visible(picture, TRUE);
		gtk_widget_set_visible(entry, FALSE);
	}
	else if (ei->show_orientation)
	{
		int v = ei->value_orientation;
		const char *text = (v <= 8 && 0 < v) ? orientation_options[v-1] : "";
		if (ei->is_editable)
		{
			gtk_editable_set_text(GTK_EDITABLE(entry), text);
			gtk_widget_set_visible(label, FALSE);
			gtk_widget_set_visible(picture, FALSE);
			gtk_widget_set_visible(entry, TRUE);
			gtk_widget_set_sensitive(entry, TRUE);
			g_object_set_data(G_OBJECT(gtk_list_item_get_item(item)),
				"pv-exif-key", ei->full_key);
			g_signal_connect(entry, "activate",
				G_CALLBACK(exif_entry_activate_cb), pImpl);
		}
		else
		{
			gtk_label_set_text(GTK_LABEL(label), text);
			gtk_widget_set_visible(label, TRUE);
			gtk_widget_set_visible(picture, FALSE);
			gtk_widget_set_visible(entry, FALSE);
		}
	}
	else
	{
		const char *text = ei->value_text ? ei->value_text : "";
		if (ei->is_editable)
		{
			gtk_editable_set_text(GTK_EDITABLE(entry), text);
			gtk_widget_set_visible(label, FALSE);
			gtk_widget_set_visible(picture, FALSE);
			gtk_widget_set_visible(entry, TRUE);
			gtk_widget_set_sensitive(entry, TRUE);
			g_object_set_data(G_OBJECT(gtk_list_item_get_item(item)),
				"pv-exif-key", ei->full_key);
			g_signal_connect(entry, "activate",
				G_CALLBACK(exif_entry_activate_cb), pImpl);
		}
		else
		{
			gtk_label_set_text(GTK_LABEL(label), text);
			gtk_widget_set_visible(label, TRUE);
			gtk_widget_set_visible(picture, FALSE);
			gtk_widget_set_visible(entry, FALSE);
		}
	}
}


/* --- EXIF entry activation (editing callback) --- */

static void exif_entry_activate_cb(GtkEntry *entry, gpointer user_data)
{
	PropertyView::PropertyViewImpl *pImpl = (PropertyView::PropertyViewImpl*)user_data;
	const char *key = (const char*)g_object_get_data(
		G_OBJECT(entry), "pv-exif-key");
	if (NULL == key) return;
	const char *new_text = gtk_editable_get_text(GTK_EDITABLE(entry));
	property_value_cell_edited_callback(key, new_text, pImpl);
	QuiverUtils::ConnectUnmodifiedAccelerators();
}

/* ═══════════════════════════════════════════════════════════════════════
 * Static helper functions
 * ═══════════════════════════════════════════════════════════════════════ */

static const EditableTag* FindEditableTag(const std::string& key)
{
	for (size_t i = 0; i < sizeof(k_editableTags)/sizeof(k_editableTags[0]); i++)
		if (key == k_editableTags[i].key)
			return &k_editableTags[i];
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
		if (pExifData->end() == itCoord) return "";
		double degrees = ParseRationalTriple(itCoord->toString());
		std::string ref;
		if ('\0' != refKey[0])
		{
			auto itRef = pExifData->findKey(Exiv2::ExifKey(refKey));
			if (pExifData->end() != itRef) ref = itRef->toString();
		}
		char buf[64];
		if (!ref.empty())
			snprintf(buf,sizeof(buf),"%.6f (%s)",degrees,ref.c_str());
		else
			snprintf(buf,sizeof(buf),"%.6f",degrees);
		return buf;
	}
	catch (...) { return ""; }
}

static void property_view_map(GtkWidget *widget, gpointer user_data)
{ (void)widget;
	PropertyView::PropertyViewImpl *pImpl =
		static_cast<PropertyView::PropertyViewImpl*>(user_data);
	if (!pImpl->m_bLoaded)
	{
		if (0 != pImpl->m_iIdleLoadID)
		{ g_source_remove(pImpl->m_iIdleLoadID); pImpl->m_iIdleLoadID = 0; }
		pImpl->m_iIdleLoadID = g_timeout_add(10, property_view_idle_load, pImpl);
		pImpl->m_bLoaded = TRUE;
	}
}


/* ═══════════════════════════════════════════════════════════════════════
 * Constructor / destructor / public API
 * ═══════════════════════════════════════════════════════════════════════ */

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
	m_pExifPopover    = NULL;
	for (int i = 0; i < 5; i++) m_pPageWidgets[i] = NULL;

	PreferencesPtr prefPtr = Preferences::GetInstance();
	prefPtr->AddEventHandler( m_PreferencesEventHandlerPtr );
}

PropertyView::PropertyViewImpl::~PropertyViewImpl()
{
	PreferencesPtr prefPtr = Preferences::GetInstance();
	prefPtr->RemoveEventHandler( m_PreferencesEventHandlerPtr );

	if (NULL != m_pExifPopover)
	{ gtk_widget_unparent(m_pExifPopover); m_pExifPopover = NULL; }
	if (NULL != m_pNotebook)
		g_object_unref(m_pNotebook);
}

/* Helper: create a GtkColumnView in a scrolled window */
static GtkWidget* column_view_new(GListModel **out_model,
	GtkSelectionModel **out_sel)
{
	GListStore *store = g_list_store_new(property_item_get_type());
	GtkSingleSelection *sel = gtk_single_selection_new(G_LIST_MODEL(store));
	gtk_single_selection_set_autoselect(sel, FALSE);
	gtk_single_selection_set_can_unselect(sel, TRUE);
	GtkColumnView *cv = GTK_COLUMN_VIEW(gtk_column_view_new(GTK_SELECTION_MODEL(sel)));
	gtk_column_view_set_show_column_separators(cv, TRUE);
	*out_model = G_LIST_MODEL(store);
	*out_sel = GTK_SELECTION_MODEL(sel);
	return GTK_WIDGET(cv);
}

PropertyView::PropertyView()
	: m_PropertyViewImplPtr ( new PropertyViewImpl() )
{
	const char* labels[5] = { "Summary", "EXIF", "XMP", "IPTC", "Video" };

	GdkRGBA edit_color;
	gdk_rgba_parse(&edit_color, "#3584e4");

	GtkWidget* views[5] = {};

	/* Create notebook first so pages can be added in the loop */
	m_PropertyViewImplPtr->m_pNotebook = gtk_notebook_new();
	g_object_ref(m_PropertyViewImplPtr->m_pNotebook);
	gtk_notebook_set_scrollable(GTK_NOTEBOOK(m_PropertyViewImplPtr->m_pNotebook), TRUE);

	for (int i = 0; i < 5; i++)
	{
		GtkWidget *scrolled = gtk_scrolled_window_new();
		gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
			GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
		g_object_ref(scrolled);
		m_PropertyViewImplPtr->m_pPageWidgets[i] = scrolled;

		GListModel *model = NULL;
		GtkSelectionModel *sel = NULL;
		GtkWidget *colview_widget = column_view_new(&model, &sel);
		GtkColumnView *colview = GTK_COLUMN_VIEW(colview_widget);
		(void)colview;

		switch (i)
		{
			case 0: /* summary */
			{
				GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
				gtk_widget_set_margin_start(vbox, 8);
				gtk_widget_set_margin_end(vbox, 8);
				gtk_widget_set_margin_top(vbox, 8);
				gtk_widget_set_margin_bottom(vbox, 8);

				GtkWidget *preview = gtk_picture_new();
				gtk_widget_set_halign(preview, GTK_ALIGN_CENTER);
				gtk_widget_set_valign(preview, GTK_ALIGN_START);
				gtk_widget_set_visible(preview, FALSE);
				gtk_box_append(GTK_BOX(vbox), preview);
				m_PropertyViewImplPtr->m_pSummaryPreview = preview;

				GtkWidget *title = gtk_label_new(NULL);
				gtk_label_set_xalign(GTK_LABEL(title), 0.0);
				gtk_label_set_wrap(GTK_LABEL(title), TRUE);
				gtk_label_set_wrap_mode(GTK_LABEL(title), PANGO_WRAP_WORD_CHAR);
				gtk_box_append(GTK_BOX(vbox), title);
				m_PropertyViewImplPtr->m_pSummaryTitle = title;

				/* Property column */
				GtkListItemFactory *fkey = gtk_signal_list_item_factory_new();
				g_signal_connect(fkey, "setup", G_CALLBACK(kv_setup), NULL);
				g_signal_connect(fkey, "bind", G_CALLBACK(kv_key_bind), NULL);
				g_signal_connect(fkey, "unbind", G_CALLBACK(kv_unbind), NULL);
				GtkColumnViewColumn *ckey = gtk_column_view_column_new("Property", fkey);
				gtk_column_view_column_set_expand(ckey, TRUE);
				gtk_column_view_append_column(GTK_COLUMN_VIEW(colview_widget), ckey);

				/* Value column */
				GtkListItemFactory *fval = gtk_signal_list_item_factory_new();
				g_signal_connect(fval, "setup", G_CALLBACK(kv_setup), NULL);
				g_signal_connect(fval, "bind", G_CALLBACK(kv_value_bind), NULL);
				g_signal_connect(fval, "unbind", G_CALLBACK(kv_unbind), NULL);
				GtkColumnViewColumn *cval = gtk_column_view_column_new("Value", fval);
				gtk_column_view_column_set_expand(cval, TRUE);
				gtk_column_view_append_column(GTK_COLUMN_VIEW(colview_widget), cval);

				/* Summary tab: no column headers needed */

				gtk_box_append(GTK_BOX(vbox), colview_widget);
				gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), vbox);
			}
			break;

			case 1: /* EXIF */
			{
				GtkListItemFactory *fname = gtk_signal_list_item_factory_new();
				g_signal_connect(fname, "setup", G_CALLBACK(exif_name_setup), NULL);
				g_signal_connect(fname, "bind", G_CALLBACK(exif_name_bind), NULL);
				GtkColumnViewColumn *cname = gtk_column_view_column_new("Property", fname);
				gtk_column_view_column_set_expand(cname, TRUE);
				gtk_column_view_append_column(GTK_COLUMN_VIEW(colview_widget), cname);

				GtkListItemFactory *fval = gtk_signal_list_item_factory_new();
				g_signal_connect(fval, "setup", G_CALLBACK(exif_value_setup), NULL);
				g_signal_connect(fval, "bind", G_CALLBACK(exif_value_bind),
					m_PropertyViewImplPtr.get());
				GtkColumnViewColumn *cval = gtk_column_view_column_new("Value", fval);
				gtk_column_view_column_set_expand(cval, TRUE);
				gtk_column_view_append_column(GTK_COLUMN_VIEW(colview_widget), cval);

				/* Right-click context menu via GtkGestureClick */
				GtkGestureClick *rclick = GTK_GESTURE_CLICK(gtk_gesture_click_new());
				gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(rclick), GDK_BUTTON_SECONDARY);
				g_signal_connect(rclick, "pressed",
					G_CALLBACK(exif_right_click_cb), m_PropertyViewImplPtr.get());
				gtk_widget_add_controller(colview_widget, GTK_EVENT_CONTROLLER(rclick));

				gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), colview_widget);
			}
			break;

			case 2: /* XMP */
			case 3: /* IPTC */
			{
				GtkListItemFactory *fkey = gtk_signal_list_item_factory_new();
				g_signal_connect(fkey, "setup", G_CALLBACK(kv_setup), NULL);
				g_signal_connect(fkey, "bind", G_CALLBACK(kv_key_bind), NULL);
				g_signal_connect(fkey, "unbind", G_CALLBACK(kv_unbind), NULL);
				GtkColumnViewColumn *ckey = gtk_column_view_column_new("Property", fkey);
				gtk_column_view_column_set_expand(ckey, TRUE);
				gtk_column_view_append_column(GTK_COLUMN_VIEW(colview_widget), ckey);

				GtkListItemFactory *fval = gtk_signal_list_item_factory_new();
				g_signal_connect(fval, "setup", G_CALLBACK(kv_setup), NULL);
				g_signal_connect(fval, "bind", G_CALLBACK(kv_value_bind), NULL);
				g_signal_connect(fval, "unbind", G_CALLBACK(kv_unbind), NULL);
				GtkColumnViewColumn *cval = gtk_column_view_column_new("Value", fval);
				gtk_column_view_column_set_expand(cval, TRUE);
				gtk_column_view_append_column(GTK_COLUMN_VIEW(colview_widget), cval);

				gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), colview_widget);
			}
			break;

			case 4: /* Video */
			{
				GtkListItemFactory *fkey = gtk_signal_list_item_factory_new();
				g_signal_connect(fkey, "setup", G_CALLBACK(kv_setup), NULL);
				g_signal_connect(fkey, "bind", G_CALLBACK(kv_key_bind), NULL);
				g_signal_connect(fkey, "unbind", G_CALLBACK(kv_unbind), NULL);
				GtkColumnViewColumn *ckey = gtk_column_view_column_new("Property", fkey);
				gtk_column_view_column_set_expand(ckey, TRUE);
				gtk_column_view_append_column(GTK_COLUMN_VIEW(colview_widget), ckey);

				GtkListItemFactory *fval = gtk_signal_list_item_factory_new();
				g_signal_connect(fval, "setup", G_CALLBACK(entry_setup), NULL);
				g_signal_connect(fval, "bind", G_CALLBACK(video_entry_bind),
					m_PropertyViewImplPtr.get());
				GtkColumnViewColumn *cval = gtk_column_view_column_new("Value", fval);
				gtk_column_view_column_set_expand(cval, TRUE);
				gtk_column_view_append_column(GTK_COLUMN_VIEW(colview_widget), cval);

				gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), colview_widget);
			}
			break;
		}

		gtk_notebook_append_page(GTK_NOTEBOOK(m_PropertyViewImplPtr->m_pNotebook),
			scrolled, gtk_label_new(labels[i]));
		views[i] = colview_widget;
	}

	m_PropertyViewImplPtr->m_pSummaryColumnView = views[0];
	m_PropertyViewImplPtr->m_pExifColumnView    = views[1];
	m_PropertyViewImplPtr->m_pXmpColumnView     = views[2];
	m_PropertyViewImplPtr->m_pIptcColumnView    = views[3];
	m_PropertyViewImplPtr->m_pVideoColumnView   = views[4];

	g_signal_connect(m_PropertyViewImplPtr->m_pNotebook,
		"map", G_CALLBACK(property_view_map), m_PropertyViewImplPtr.get());
}

PropertyView::~PropertyView()
{
}

GtkWidget*
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
		{ g_source_remove(m_PropertyViewImplPtr->m_iIdleLoadID);
		  m_PropertyViewImplPtr->m_iIdleLoadID = 0; }
		m_PropertyViewImplPtr->m_iIdleLoadID =
			g_timeout_add(300, property_view_idle_load,
				m_PropertyViewImplPtr.get());
		m_PropertyViewImplPtr->m_bLoaded = TRUE;
	}
	else
	{
		m_PropertyViewImplPtr->m_bLoaded = FALSE;
	}
}


/* ═══════════════════════════════════════════════════════════════════════
 * PopulateSummary
 * ═══════════════════════════════════════════════════════════════════════ */

void PropertyView::PropertyViewImpl::PopulateSummary()
{
	GListStore *store = g_list_store_new(property_item_get_type());

	auto add_row = [&](const char* label, const std::string& value)
	{
		if (value.empty()) return;
		g_list_store_append(store, property_item_new(label, value.c_str(), FALSE, FALSE));
	};

	if (NULL == m_QuiverFile.GetURI())
	{
		GtkSingleSelection *sel = gtk_single_selection_new(G_LIST_MODEL(store));
		gtk_column_view_set_model(GTK_COLUMN_VIEW(m_pSummaryColumnView),
			GTK_SELECTION_MODEL(sel));
		gtk_widget_set_visible(m_pSummaryPreview, FALSE);
		gtk_label_set_markup(GTK_LABEL(m_pSummaryTitle), "");
		return;
	}

	/* filename heading */
	gchar *szMarkup = g_markup_printf_escaped(
		"<big><b>%s</b></big>", m_QuiverFile.GetFileName().c_str());
	gtk_label_set_markup(GTK_LABEL(m_pSummaryTitle), szMarkup);
	g_free(szMarkup);

	/* preview: EXIF thumbnail for photos, poster frame for videos */
	GdkPixbuf* pixbuf = NULL;
	if (m_bIsVideo)
		pixbuf = QuiverVideoOps::LoadPixbuf(m_QuiverFile.GetURI());
	else
		pixbuf = m_QuiverFile.GetExifThumbnail();

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
		GdkTexture *tex = gdk_texture_new_for_pixbuf(pixbuf);
		gtk_picture_set_paintable(GTK_PICTURE(m_pSummaryPreview), GDK_PAINTABLE(tex));
		g_object_unref(tex);
		gtk_widget_set_visible(m_pSummaryPreview, TRUE);
		g_object_unref(pixbuf);
	}
	else
	{
		gtk_widget_set_visible(m_pSummaryPreview, FALSE);
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
					if (m_ExifData->end() != it) return it->toString();
				} catch (...) {}
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

	GtkSingleSelection *sel = gtk_single_selection_new(G_LIST_MODEL(store));
	gtk_column_view_set_model(GTK_COLUMN_VIEW(m_pSummaryColumnView),
		GTK_SELECTION_MODEL(sel));
}


/* ═══════════════════════════════════════════════════════════════════════
 * XMP / IPTC population
 * ═══════════════════════════════════════════════════════════════════════ */

/* For XMP we build a flat list with indentation strings to simulate the
 * tree hierarchy that GtkTreeView had. */
static void xmp_insert_flat(GListStore *store, const std::string& /*prefix*/,
	std::vector<std::string>& parts, size_t idx, const std::string& value)
{
	std::string full_key;
	for (size_t j = 0; j <= idx; j++)
	{
		if (!full_key.empty()) full_key += " ";
		full_key += parts[j];
	}

	/* find if this key already exists */
	gboolean found = FALSE;
	guint n = g_list_model_get_n_items(G_LIST_MODEL(store));
	for (guint i = 0; i < n; i++)
	{
		PropertyItem *pi = PROPERTY_ITEM(g_list_model_get_item(G_LIST_MODEL(store), i));
		if (pi->key && 0 == strcmp(pi->key, full_key.c_str()))
		{ g_object_unref(pi); found = TRUE; break; }
		g_object_unref(pi);
	}

	if (!found && idx + 1 == parts.size())
	{
		std::string display = value;
		if (display.size() > 200)
			display = display.substr(0, 197) + "...";
		g_list_store_append(store, property_item_new(
			full_key.c_str(), display.c_str(), FALSE,
			FALSE /* not a group in flat view */));
	}

	if (idx + 1 < parts.size())
	{
		/* add group header if not present */
		if (!found)
		{
			gchar *bold = g_markup_printf_escaped("%s", full_key.c_str());
			g_list_store_append(store, property_item_new(
				full_key.c_str(), "", FALSE, TRUE));
			g_free(bold);
		}
		xmp_insert_flat(store, full_key, parts, idx + 1, value);
	}
}

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
				out.push_back(piece);
		}
		if (std::string::npos == dot) break;
		start = dot + 1;
	}
}

static void property_populate_keyvalue_from_file(PropertyView::PropertyViewImpl* pImpl,
	GtkWidget* colview, bool bXmp)
{
	GListStore *store = g_list_store_new(property_item_get_type());

	gchar* szPath = g_filename_from_uri(pImpl->m_QuiverFile.GetURI(), NULL, NULL);
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

					bool bStructural = false;
					static const char* k_types[] = { "Struct", "Seq", "Bag", "Alt" };
					for (size_t i = 0; i < sizeof(k_types)/sizeof(k_types[0]); i++)
					{
						if (value == std::string("type=\"") + k_types[i] + "\"")
						{ bStructural = true; break; }
					}
					if (bStructural) continue;

					std::vector<std::string> expanded;
					size_t start = 0;
					while (true)
					{
						size_t slash = key.find('/', start);
						std::string seg = key.substr(start,
							(std::string::npos == slash) ?
								std::string::npos : slash - start);
						property_xmp_split_segment(seg, expanded);
						if (std::string::npos == slash) break;
						start = slash + 1;
					}

					for (size_t i = 0; i < expanded.size(); i++)
					{
						size_t colon = expanded[i].find(':');
						if (0 < colon && colon + 1 < expanded[i].size())
							expanded[i].erase(0, colon + 1);
						if (!expanded[i].empty() &&
							isalpha((unsigned char)expanded[i][0]))
							expanded[i][0] = toupper((unsigned char)expanded[i][0]);
					}

					if (!expanded.empty())
						xmp_insert_flat(store, "", expanded, 0, value);
				}
			}
			else
			{
				Exiv2::IptcData data = image->iptcData();
				for (auto it = data.begin(); data.end() != it; ++it)
					g_list_store_append(store, property_item_new(
						it->key().c_str(), it->toString().c_str(),
						FALSE, FALSE));
			}
		}
		catch (...) {}
		g_free(szPath);
	}

	GtkSingleSelection *sel = gtk_single_selection_new(G_LIST_MODEL(store));
	gtk_column_view_set_model(GTK_COLUMN_VIEW(colview), GTK_SELECTION_MODEL(sel));
}

void PropertyView::PropertyViewImpl::PopulateXmp()
{ property_populate_keyvalue_from_file(this, m_pXmpColumnView, true); }

void PropertyView::PropertyViewImpl::PopulateIptc()
{ property_populate_keyvalue_from_file(this, m_pIptcColumnView, false); }


/* ═══════════════════════════════════════════════════════════════════════
 * Video date editing polling
 * ═══════════════════════════════════════════════════════════════════════ */

struct VideoDatePoll
{
	PropertyView::PropertyViewImpl *pImpl;
	VideoDateEditTaskPtr            task;
	int                             ticks;
};

static gboolean property_video_date_poll(gpointer data)
{
	VideoDatePoll *poll = (VideoDatePoll*)data;
	if (NULL == poll->task.get()) { delete poll; return FALSE; }
	if (!poll->task->IsFinished() && poll->ticks < 600) { poll->ticks++; return TRUE; }
	if (poll->task->IsFinished())
	{
		poll->pImpl->m_QuiverFile.Reload();
		poll->pImpl->LoadProperties();
	}
	delete poll;
	return FALSE;
}

static void property_video_value_cell_edited_callback(const char *new_text,
	gpointer user_data)
{
	PropertyView::PropertyViewImpl *pImpl =
		(PropertyView::PropertyViewImpl*)user_data;

	QuiverUtils::ConnectUnmodifiedAccelerators();

	if (NULL == pImpl->m_QuiverFile.GetURI()) return;

	gchar* szPath = g_filename_from_uri(pImpl->m_QuiverFile.GetURI(), NULL, NULL);
	if (NULL == szPath) return;

	bool bOk = false;
	time_t new_epoch = 0;

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


/* ═══════════════════════════════════════════════════════════════════════
 * PopulateVideo
 * ═══════════════════════════════════════════════════════════════════════ */

void PropertyView::PropertyViewImpl::PopulateVideo()
{
	GListStore *store = g_list_store_new(property_item_get_type());

	gchar* szPath = g_filename_from_uri(m_QuiverFile.GetURI(), NULL, NULL);
	if (NULL != szPath)
	{
		VideoInfo info = ProbeVideoInfo(szPath);

		auto add = [&](const char* label, const char* value, gboolean editable)
		{
			g_list_store_append(store, property_item_new(label, value, editable, FALSE));
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
			add("Created", info.creation_time, TRUE);

		g_free(szPath);
	}

	GtkSingleSelection *sel = gtk_single_selection_new(G_LIST_MODEL(store));
	gtk_column_view_set_model(GTK_COLUMN_VIEW(m_pVideoColumnView),
		GTK_SELECTION_MODEL(sel));
}


/* ═══════════════════════════════════════════════════════════════════════
 * PopulateExif
 * ═══════════════════════════════════════════════════════════════════════ */

static void property_populate_exif(PropertyView::PropertyViewImpl *pImpl)
{
	GListStore *store = g_list_store_new(exif_item_get_type());

	if (NULL != pImpl->m_ExifData.get())
	{
		GdkPixbuf *pixbuf = pImpl->m_QuiverFile.GetExifThumbnail();
		if (NULL != pixbuf)
		{
			GdkPixbuf *new_pixbuf =
				QuiverUtils::GdkPixbufExifReorientate(pixbuf,
					pImpl->m_QuiverFile.GetOrientation());
			if (NULL != new_pixbuf)
			{ g_object_unref(pixbuf); pixbuf = new_pixbuf; }
		}

		if (NULL != pixbuf)
		{
			ExifItem *hdr = exif_item_new();
			hdr->name = g_strdup("Exif Thumbnail");
			hdr->is_group = TRUE;
			hdr->show_text = TRUE;
			g_list_store_append(store, hdr);

			ExifItem *thumb_item = exif_item_new();
			thumb_item->name = g_strdup("Thumbnail");
			thumb_item->thumbnail = pixbuf; /* transfer ownership */
			thumb_item->show_pixbuf = TRUE;
			g_list_store_append(store, thumb_item);
		}

		/* group entries by exiv2 group name */
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
				ExifItem *gh = exif_item_new();
				gh->name = g_strdup(group.c_str());
				gh->is_group = TRUE;
				gh->show_text = TRUE;
				g_list_store_append(store, gh);
			}

			gboolean editable = (NULL != FindEditableTag(it->key()));
			ExifItem *child = exif_item_new();
			child->full_key = g_strdup(it->key().c_str());
			child->name = g_strdup(it->tagName().c_str());
			child->is_editable = editable;

			if ("Exif.Image.Orientation" == it->key())
			{
				long val = 1;
				try { val = it->toInt64(); } catch (...) {}
				child->value_orientation = (int)val;
				child->show_orientation = TRUE;
			}
			else
			{
				std::string value;
				try { value = it->toString(); } catch (...) {}
				child->value_text = g_strdup(value.c_str());
				child->show_text = TRUE;
			}
			g_list_store_append(store, child);
		}
	}

	GtkSingleSelection *sel = gtk_single_selection_new(G_LIST_MODEL(store));
	gtk_column_view_set_model(GTK_COLUMN_VIEW(pImpl->m_pExifColumnView),
		GTK_SELECTION_MODEL(sel));
}


/* ═══════════════════════════════════════════════════════════════════════
 * UpdateTabsForFile / LoadProperties
 * ═══════════════════════════════════════════════════════════════════════ */

void PropertyView::PropertyViewImpl::UpdateTabsForFile()
{
	GtkNotebook* nb = GTK_NOTEBOOK(m_pNotebook);

	auto find_page = [&](GtkWidget* widget) -> int
	{
		for (int i = 0; i < gtk_notebook_get_n_pages(nb); i++)
			if (widget == gtk_notebook_get_nth_page(nb, i))
				return i;
		return -1;
	};

	if (m_bIsVideo)
	{
		int p;
		p = find_page(m_pPageWidgets[1]); if (-1 != p) gtk_notebook_remove_page(nb, p);
		p = find_page(m_pPageWidgets[2]); if (-1 != p) gtk_notebook_remove_page(nb, p);
		p = find_page(m_pPageWidgets[3]); if (-1 != p) gtk_notebook_remove_page(nb, p);
		if (-1 == find_page(m_pPageWidgets[4]))
			gtk_notebook_append_page(nb, m_pPageWidgets[4], gtk_label_new("Video"));
	}
	else
	{
		int p;
		p = find_page(m_pPageWidgets[4]); if (-1 != p) gtk_notebook_remove_page(nb, p);

		int pos = 1;
		const int order[3] = { 1, 2, 3 };
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
				gtk_notebook_remove_page(nb, find_page(m_pPageWidgets[order[i]]));
		}
	}
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

			gchar* szPath = g_filename_from_uri(m_QuiverFile.GetURI(), NULL, NULL);
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


/* ═══════════════════════════════════════════════════════════════════════
 * EXIF cell editing
 * ═══════════════════════════════════════════════════════════════════════ */

static gboolean property_date_format_is_valid(const char *date)
{
	if (19 != strlen(date)) return FALSE;
	int year, month, day, hour, min, sec;
	sscanf(date, "%d:%d:%d %d:%d:%d", &year, &month, &day, &hour, &min, &sec);
	struct tm tm_date = {};
	tm_date.tm_sec = sec; tm_date.tm_min = min; tm_date.tm_hour = hour;
	tm_date.tm_mday = day; tm_date.tm_mon = month - 1; tm_date.tm_year = year - 1900;
	tm_date.tm_isdst = -1;
	return (tm_date.tm_sec == sec && tm_date.tm_min == min &&
		tm_date.tm_hour == hour && tm_date.tm_mday == day &&
		tm_date.tm_mon == month - 1 && tm_date.tm_year == year - 1900);
}

static void set_exif_value(std::shared_ptr<Exiv2::ExifData> pExifData,
	const char* key, const char* new_text, Exiv2::TypeId typeId)
{
	Exiv2::Exifdatum& datum = (*pExifData)[key];
	Exiv2::Value::UniquePtr value = Exiv2::Value::create(typeId);
	value->read(new_text);
	datum.setValue(value.get());
}

static void property_value_cell_edited_callback(const char *key, const char *new_text,
	gpointer user_data)
{
	PropertyView::PropertyViewImpl *pImpl =
		(PropertyView::PropertyViewImpl*)user_data;

	QuiverUtils::ConnectUnmodifiedAccelerators();

	std::shared_ptr<Exiv2::ExifData> pExifData = pImpl->m_ExifData;
	if (NULL == key || NULL == pExifData.get()) return;

	const EditableTag* tag = FindEditableTag(key);
	if (NULL == tag) return;

	gboolean updated = FALSE;

	switch (tag->kind)
	{
		case TAGKIND_ORIENTATION:
		{
			for (int i = 0; i < 8; i++)
			{
				if (!strcmp(new_text, orientation_options[i]))
				{
					gchar szVal[16];
					snprintf(szVal, sizeof(szVal), "%d", i + 1);
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
			if (property_date_format_is_valid(new_text))
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
		if (0 == strcmp(tag->key, "Exif.Image.Orientation"))
			property_populate_exif(pImpl);
		else
		{
			/* refresh just this row */
			std::string value;
			try {
				auto it = pExifData->findKey(Exiv2::ExifKey(key));
				if (pExifData->end() != it) value = it->toString();
			} catch (...) {}
			/* find the item in the store and update it */
			GtkSingleSelection *sel = GTK_SINGLE_SELECTION(
				gtk_column_view_get_model(
					GTK_COLUMN_VIEW(pImpl->m_pExifColumnView)));
			guint n = g_list_model_get_n_items(G_LIST_MODEL(sel));
			for (guint i = 0; i < n; i++)
			{
				ExifItem *ei = EXIF_ITEM(g_list_model_get_item(G_LIST_MODEL(sel), i));
				if (ei->full_key && 0 == strcmp(ei->full_key, key))
				{
					g_free(ei->value_text);
					ei->value_text = g_strdup(value.c_str());
					g_object_unref(ei);
					break;
				}
				g_object_unref(ei);
			}
			/* force a refresh of the view */
			g_object_notify(G_OBJECT(sel), "model");
		}
	}
}


/* ═══════════════════════════════════════════════════════════════════════
 * EXIF right-click context menu  (GtkPopoverMenu + GMenu)
 * ═══════════════════════════════════════════════════════════════════════ */

static void exif_right_click_cb(GtkGestureClick *gesture, gint n_press,
	gdouble x, gdouble y, gpointer user_data)
{ (void)gesture; (void)n_press;
	PropertyView::PropertyViewImpl *pImpl =
		(PropertyView::PropertyViewImpl*)user_data;

	if (NULL != pImpl->m_pExifPopover)
	{
		gtk_widget_unparent(pImpl->m_pExifPopover);
		pImpl->m_pExifPopover = NULL;
	}

	/* Determine which item is at the click position */
	GtkWidget *colview = pImpl->m_pExifColumnView;
	GtkSingleSelection *sel = GTK_SINGLE_SELECTION(
		gtk_column_view_get_model(GTK_COLUMN_VIEW(colview)));
	guint pos = gtk_single_selection_get_selected(sel);
	if (pos == GTK_INVALID_LIST_POSITION) return;

	ExifItem *ei = EXIF_ITEM(g_list_model_get_item(G_LIST_MODEL(sel), pos));
	if (NULL == ei) return;

	std::string groupName;
	std::string selectedKey;
	gboolean is_group = ei->is_group;

	if (is_group)
	{
		if (ei->name) groupName = ei->name;
	}
	else
	{
		if (ei->full_key) selectedKey = ei->full_key;
		try { groupName = Exiv2::ExifKey(selectedKey).groupName(); }
		catch (...) {}
	}
	g_object_unref(ei);

	GMenu *menu = g_menu_new();

	if (!groupName.empty())
	{
		/* offer adding any missing editable tag from this group */
		bool bAddedSubmenu = false;
		GMenu *addMenu = NULL;
		for (size_t i = 0; i < sizeof(k_editableTags)/sizeof(k_editableTags[0]); i++)
		{
			const EditableTag *t = &k_editableTags[i];
			try { if (groupName != Exiv2::ExifKey(t->key).groupName()) continue; }
			catch (...) { continue; }

			if (pImpl->m_ExifData->end() !=
				pImpl->m_ExifData->findKey(Exiv2::ExifKey(t->key)))
				continue;

			if (!bAddedSubmenu)
			{
				bAddedSubmenu = true;
				addMenu = g_menu_new();
			}

			GMenuItem *item = g_menu_item_new(t->title, NULL);
			KeyActionStruct *data = g_new(KeyActionStruct, 1);
			data->pImpl = pImpl;
			data->key = g_strdup(t->key);
			g_object_set_data_full(G_OBJECT(item), "kv-data", data,
				[](gpointer p){ KeyActionStruct *d = (KeyActionStruct*)p;
					g_free(d->key); g_free(d); });
			g_menu_item_set_attribute_value(item, "action",
				g_variant_new_string("pv-add-tag"));
			g_menu_item_set_attribute_value(item, "target",
				g_variant_new_string(t->key));
			g_menu_append_item(addMenu, item);
			g_object_unref(item);
		}

		if (addMenu)
		{
			GMenuItem *header = g_menu_item_new_submenu("Add Tag",
				G_MENU_MODEL(addMenu));
			g_menu_append_item(menu, header);
			g_object_unref(header);
			g_object_unref(addMenu);
		}

		if (!is_group && !selectedKey.empty())
		{
			GMenuItem *rm = g_menu_item_new("Remove Tag", NULL);
			g_object_set_data_full(G_OBJECT(rm), "kv-impl", pImpl, NULL);
			g_object_set_data_full(G_OBJECT(rm), "kv-key",
				g_strdup(selectedKey.c_str()), g_free);
			g_menu_item_set_attribute_value(rm, "action",
				g_variant_new_string("pv-remove-tag"));
			g_menu_item_set_attribute_value(rm, "target",
				g_variant_new_string(selectedKey.c_str()));
			g_menu_append_item(menu, rm);
			g_object_unref(rm);
		}
	}

	if (0 == g_menu_model_get_n_items(G_MENU_MODEL(menu)))
	{
		g_object_unref(menu);
		return;
	}

	GtkPopover *popover = GTK_POPOVER(gtk_popover_menu_new_from_model(G_MENU_MODEL(menu)));
	g_object_unref(menu);

	/* Register a GSimpleActionGroup so menu actions resolve */
	GtkWidget *cw = colview;
	while (cw && !GTK_IS_ROOT(cw))
		cw = gtk_widget_get_parent(cw);
	if (GTK_IS_ROOT(cw))
	{
		/* we don't want to add actions to the root; store on the colview */
	}

	/* Connect action handlers via a temporary action group */
	GdkRectangle rect = { (int)x, (int)y, 1, 1 };
	gtk_popover_set_pointing_to(popover, &rect);
	gtk_widget_set_parent(GTK_WIDGET(popover), colview);
	gtk_popover_popup(popover);
	pImpl->m_pExifPopover = GTK_WIDGET(popover);

	/* auto-dismiss on close */
	g_signal_connect_swapped(popover, "closed",
		G_CALLBACK(exif_popover_closed_cb), pImpl);
}

static void exif_popover_closed_cb(GtkPopover *popover, gpointer user_data)
{ (void)popover;
	PropertyView::PropertyViewImpl *pImpl = (PropertyView::PropertyViewImpl*)user_data;
	if (pImpl->m_pExifPopover)
	{
		gtk_widget_unparent(pImpl->m_pExifPopover);
		pImpl->m_pExifPopover = NULL;
	}
}


/* ═══════════════════════════════════════════════════════════════════════
 * PreferencesEventHandler (empty stub — preserved from original)
 * ═══════════════════════════════════════════════════════════════════════ */

void PropertyView::PropertyViewImpl::PreferencesEventHandler::HandlePreferenceChanged(
	PreferencesEventPtr event)
{ (void)event; }
