#include "PasteMetadataDlg.h"
#include <cstdio>
#include <string>

extern GtkApplication* g_pApp;

class PasteMetadataDlg::PasteMetadataDlgPriv
{
public:
	PasteMetadataDlgPriv(const QuiverMetadata::MetadataPayload& payload,
	                     size_t targetCount,
	                     bool allTargetsAreImages)
		: m_payload(payload),
		  m_targetCount(targetCount),
		  m_allTargetsAreImages(allTargetsAreImages),
		  m_bRunDone(false),
		  m_iRunResponse(GTK_RESPONSE_NONE)
	{
		BuildUI();
	}

	~PasteMetadataDlgPriv()
	{
		if (m_pDialog)
		{
			gtk_window_destroy(GTK_WINDOW(m_pDialog));
			m_pDialog = nullptr;
		}
	}

	void BuildUI();

	QuiverMetadata::MetadataPayload m_payload;
	size_t m_targetCount;
	bool m_allTargetsAreImages;

	bool m_bRunDone;
	int m_iRunResponse;

	GtkWidget* m_pDialog = nullptr;
	GtkCheckButton* m_pCheckDate = nullptr;
	GtkCheckButton* m_pCheckGps = nullptr;
	GtkCheckButton* m_pCheckCamera = nullptr;
	GtkCheckButton* m_pCheckDescription = nullptr;
	GtkCheckButton* m_pCheckArtist = nullptr;
	GtkCheckButton* m_pCheckAllExif = nullptr;
};

void PasteMetadataDlg::PasteMetadataDlgPriv::BuildUI()
{
	m_pDialog = gtk_window_new();
	gtk_window_set_title(GTK_WINDOW(m_pDialog), "Paste Metadata");
	gtk_window_set_resizable(GTK_WINDOW(m_pDialog), FALSE);

	// HeaderBar
	GtkWidget* header = gtk_header_bar_new();
	gtk_window_set_titlebar(GTK_WINDOW(m_pDialog), header);

	GtkWidget* btnCancel = gtk_button_new_with_mnemonic("_Cancel");
	g_signal_connect_swapped(btnCancel, "clicked", G_CALLBACK(+[](PasteMetadataDlgPriv* priv) {
		priv->m_bRunDone = true;
		priv->m_iRunResponse = GTK_RESPONSE_CANCEL;
		gtk_widget_set_visible(priv->m_pDialog, FALSE);
	}), this);
	gtk_header_bar_pack_start(GTK_HEADER_BAR(header), btnCancel);

	GtkWidget* btnApply = gtk_button_new_with_mnemonic("_Apply");
	gtk_widget_add_css_class(btnApply, "suggested-action");
	g_signal_connect_swapped(btnApply, "clicked", G_CALLBACK(+[](PasteMetadataDlgPriv* priv) {
		priv->m_bRunDone = true;
		priv->m_iRunResponse = GTK_RESPONSE_OK;
		gtk_widget_set_visible(priv->m_pDialog, FALSE);
	}), this);
	gtk_header_bar_pack_end(GTK_HEADER_BAR(header), btnApply);

	// Main Layout
	GtkWidget* mainBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
	gtk_widget_set_margin_start(mainBox, 20);
	gtk_widget_set_margin_end(mainBox, 20);
	gtk_widget_set_margin_top(mainBox, 16);
	gtk_widget_set_margin_bottom(mainBox, 16);
	gtk_window_set_child(GTK_WINDOW(m_pDialog), mainBox);

	// Source & Target Summary Frame
	GtkWidget* infoFrame = gtk_frame_new("Details");
	GtkWidget* infoBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
	gtk_widget_set_margin_start(infoBox, 12);
	gtk_widget_set_margin_end(infoBox, 12);
	gtk_widget_set_margin_top(infoBox, 8);
	gtk_widget_set_margin_bottom(infoBox, 8);
	gtk_frame_set_child(GTK_FRAME(infoFrame), infoBox);

	std::string srcText = "Source: " + m_payload.source_filename +
		(m_payload.source_is_video ? " (Video)" : " (Image)");
	GtkWidget* lblSrc = gtk_label_new(srcText.c_str());
	gtk_label_set_xalign(GTK_LABEL(lblSrc), 0.0f);
	gtk_box_append(GTK_BOX(infoBox), lblSrc);

	char szTarget[64];
	snprintf(szTarget, sizeof(szTarget), "Target: %zu item(s) selected", m_targetCount);
	GtkWidget* lblDst = gtk_label_new(szTarget);
	gtk_label_set_xalign(GTK_LABEL(lblDst), 0.0f);
	gtk_box_append(GTK_BOX(infoBox), lblDst);

	gtk_box_append(GTK_BOX(mainBox), infoFrame);

	// Options Frame
	GtkWidget* optFrame = gtk_frame_new("Metadata Fields to Apply");
	GtkWidget* optBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
	gtk_widget_set_margin_start(optBox, 12);
	gtk_widget_set_margin_end(optBox, 12);
	gtk_widget_set_margin_top(optBox, 10);
	gtk_widget_set_margin_bottom(optBox, 10);
	gtk_frame_set_child(GTK_FRAME(optFrame), optBox);

	// 1. Date & Time
	std::string dateLabel = "Date and Time";
	if (m_payload.has_date) dateLabel += " (" + m_payload.date_str + ")";
	else dateLabel += " [none in source]";
	m_pCheckDate = GTK_CHECK_BUTTON(gtk_check_button_new_with_label(dateLabel.c_str()));
	gtk_check_button_set_active(m_pCheckDate, m_payload.has_date);
	gtk_widget_set_sensitive(GTK_WIDGET(m_pCheckDate), m_payload.has_date);
	gtk_box_append(GTK_BOX(optBox), GTK_WIDGET(m_pCheckDate));

	// 2. GPS Location
	std::string gpsLabel = "GPS Coordinates";
	if (m_payload.has_gps)
	{
		char szGps[64];
		if (m_payload.has_altitude)
			snprintf(szGps, sizeof(szGps), " (%.5f, %.5f, Alt: %.1fm)",
				m_payload.gps_latitude, m_payload.gps_longitude, m_payload.gps_altitude);
		else
			snprintf(szGps, sizeof(szGps), " (%.5f, %.5f)",
				m_payload.gps_latitude, m_payload.gps_longitude);
		gpsLabel += szGps;
	}
	else
	{
		gpsLabel += " [none in source]";
	}
	m_pCheckGps = GTK_CHECK_BUTTON(gtk_check_button_new_with_label(gpsLabel.c_str()));
	gtk_check_button_set_active(m_pCheckGps, m_payload.has_gps);
	gtk_widget_set_sensitive(GTK_WIDGET(m_pCheckGps), m_payload.has_gps);
	gtk_box_append(GTK_BOX(optBox), GTK_WIDGET(m_pCheckGps));

	// 3. Camera
	std::string camLabel = "Camera Make & Model";
	if (m_payload.has_camera)
	{
		std::string cam = m_payload.camera_make;
		if (!m_payload.camera_model.empty())
		{
			if (!cam.empty()) cam += " ";
			cam += m_payload.camera_model;
		}
		camLabel += " (" + cam + ")";
	}
	else
	{
		camLabel += " [none in source]";
	}
	m_pCheckCamera = GTK_CHECK_BUTTON(gtk_check_button_new_with_label(camLabel.c_str()));
	gtk_check_button_set_active(m_pCheckCamera, m_payload.has_camera);
	gtk_widget_set_sensitive(GTK_WIDGET(m_pCheckCamera), m_payload.has_camera);
	gtk_box_append(GTK_BOX(optBox), GTK_WIDGET(m_pCheckCamera));

	// 4. Description & Comment
	std::string descLabel = "Description, Title & Comments";
	if (m_payload.has_description)
	{
		std::string d = !m_payload.title.empty() ? m_payload.title : m_payload.comment;
		if (d.size() > 30) d = d.substr(0, 27) + "…";
		descLabel += " (" + d + ")";
	}
	else
	{
		descLabel += " [none in source]";
	}
	m_pCheckDescription = GTK_CHECK_BUTTON(gtk_check_button_new_with_label(descLabel.c_str()));
	gtk_check_button_set_active(m_pCheckDescription, m_payload.has_description);
	gtk_widget_set_sensitive(GTK_WIDGET(m_pCheckDescription), m_payload.has_description);
	gtk_box_append(GTK_BOX(optBox), GTK_WIDGET(m_pCheckDescription));

	// 5. Artist & Copyright
	std::string artistLabel = "Artist & Copyright";
	if (m_payload.has_artist_copyright)
	{
		std::string a = !m_payload.artist.empty() ? m_payload.artist : m_payload.copyright;
		if (a.size() > 30) a = a.substr(0, 27) + "…";
		artistLabel += " (" + a + ")";
	}
	else
	{
		artistLabel += " [none in source]";
	}
	m_pCheckArtist = GTK_CHECK_BUTTON(gtk_check_button_new_with_label(artistLabel.c_str()));
	gtk_check_button_set_active(m_pCheckArtist, m_payload.has_artist_copyright);
	gtk_widget_set_sensitive(GTK_WIDGET(m_pCheckArtist), m_payload.has_artist_copyright);
	gtk_box_append(GTK_BOX(optBox), GTK_WIDGET(m_pCheckArtist));

	// 6. Full EXIF (Image to Image only)
	if (m_allTargetsAreImages && m_payload.has_raw_exif)
	{
		m_pCheckAllExif = GTK_CHECK_BUTTON(gtk_check_button_new_with_label(
			"All Other EXIF Tags (Lens, Exposure, Flash, etc.)"));
		gtk_check_button_set_active(m_pCheckAllExif, FALSE);
		gtk_box_append(GTK_BOX(optBox), GTK_WIDGET(m_pCheckAllExif));
	}

	gtk_box_append(GTK_BOX(mainBox), optFrame);

	// Select All / Select None buttons
	GtkWidget* buttonBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	gtk_widget_set_halign(buttonBox, GTK_ALIGN_END);

	GtkWidget* btnAll = gtk_button_new_with_mnemonic("Select _All");
	g_signal_connect_swapped(btnAll, "clicked", G_CALLBACK(+[](PasteMetadataDlgPriv* priv) {
		if (priv->m_payload.has_date) gtk_check_button_set_active(priv->m_pCheckDate, TRUE);
		if (priv->m_payload.has_gps) gtk_check_button_set_active(priv->m_pCheckGps, TRUE);
		if (priv->m_payload.has_camera) gtk_check_button_set_active(priv->m_pCheckCamera, TRUE);
		if (priv->m_payload.has_description) gtk_check_button_set_active(priv->m_pCheckDescription, TRUE);
		if (priv->m_payload.has_artist_copyright) gtk_check_button_set_active(priv->m_pCheckArtist, TRUE);
		if (priv->m_pCheckAllExif) gtk_check_button_set_active(priv->m_pCheckAllExif, TRUE);
	}), this);
	gtk_box_append(GTK_BOX(buttonBox), btnAll);

	GtkWidget* btnNone = gtk_button_new_with_mnemonic("Select _None");
	g_signal_connect_swapped(btnNone, "clicked", G_CALLBACK(+[](PasteMetadataDlgPriv* priv) {
		gtk_check_button_set_active(priv->m_pCheckDate, FALSE);
		gtk_check_button_set_active(priv->m_pCheckGps, FALSE);
		gtk_check_button_set_active(priv->m_pCheckCamera, FALSE);
		gtk_check_button_set_active(priv->m_pCheckDescription, FALSE);
		gtk_check_button_set_active(priv->m_pCheckArtist, FALSE);
		if (priv->m_pCheckAllExif) gtk_check_button_set_active(priv->m_pCheckAllExif, FALSE);
	}), this);
	gtk_box_append(GTK_BOX(buttonBox), btnNone);

	gtk_box_append(GTK_BOX(mainBox), buttonBox);

	// Close-request
	g_signal_connect(m_pDialog, "close-request", G_CALLBACK(+[](GtkWindow*, gpointer user_data) -> gboolean {
		PasteMetadataDlgPriv* priv = static_cast<PasteMetadataDlgPriv*>(user_data);
		priv->m_bRunDone = true;
		priv->m_iRunResponse = GTK_RESPONSE_CANCEL;
		return FALSE;
	}), this);
}

PasteMetadataDlg::PasteMetadataDlg(const QuiverMetadata::MetadataPayload& payload,
                                   size_t targetCount,
                                   bool allTargetsAreImages)
	: m_priv(std::make_unique<PasteMetadataDlgPriv>(payload, targetCount, allTargetsAreImages))
{
}

PasteMetadataDlg::~PasteMetadataDlg() = default;

bool PasteMetadataDlg::Run()
{
	m_priv->m_bRunDone = false;
	m_priv->m_iRunResponse = GTK_RESPONSE_NONE;

	if (NULL != g_pApp)
	{
		GtkWindow* mainWin = gtk_application_get_active_window(g_pApp);
		if (mainWin != NULL)
			gtk_window_set_transient_for(GTK_WINDOW(m_priv->m_pDialog), mainWin);
	}
	gtk_window_set_modal(GTK_WINDOW(m_priv->m_pDialog), TRUE);
	gtk_widget_set_visible(GTK_WIDGET(m_priv->m_pDialog), TRUE);

	GMainContext* ctx = g_main_context_default();
	while (!m_priv->m_bRunDone)
	{
		g_main_context_iteration(ctx, TRUE);
	}
	return (GTK_RESPONSE_OK == m_priv->m_iRunResponse);
}

QuiverMetadata::PasteOptions PasteMetadataDlg::GetOptions() const
{
	QuiverMetadata::PasteOptions opts;
	opts.copy_date = gtk_check_button_get_active(m_priv->m_pCheckDate);
	opts.copy_gps = gtk_check_button_get_active(m_priv->m_pCheckGps);
	opts.copy_camera = gtk_check_button_get_active(m_priv->m_pCheckCamera);
	opts.copy_description = gtk_check_button_get_active(m_priv->m_pCheckDescription);
	opts.copy_artist_copyright = gtk_check_button_get_active(m_priv->m_pCheckArtist);
	if (m_priv->m_pCheckAllExif)
		opts.copy_all_exif = gtk_check_button_get_active(m_priv->m_pCheckAllExif);
	else
		opts.copy_all_exif = false;
	return opts;
}
