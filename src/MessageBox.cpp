#include "MessageBox.h"

#include <gtk/gtk.h>

#include "ThreadUtil.h"

#include <utility>
#include <vector>

static gboolean idle_destroy_dialog(gpointer user_data) {
	gtk_window_destroy(GTK_WINDOW(user_data));
	return G_SOURCE_REMOVE;
}

struct RunData {
	GtkWidget* dlg;
	gint response;
	bool done;
	GMutex mutex;
	GCond cond;
};

// Store the active RunData on the dialog widget and wake any waiter with the
// given response. This replaces the (deprecated) GtkDialog "response" signal.
static void messagebox_emit_response(GtkWidget* dlg, gint response) {
	RunData* data = (RunData*)g_object_get_data(G_OBJECT(dlg), "msgbox-run-data");
	if (data) {
		g_mutex_lock(&data->mutex);
		data->response = response;
		data->done = true;
		g_cond_signal(&data->cond);
		g_mutex_unlock(&data->mutex);
	}
	gtk_widget_set_visible(dlg, FALSE);
}

static void button_clicked_cb(GtkButton* button, gpointer user_data) {
	GtkWidget* dlg = GTK_WIDGET(user_data);
	gint response = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "msgbox-response"));
	messagebox_emit_response(dlg, response);
}

static GtkWidget* make_button(GtkWidget* dlg, const char* label, gint response) {
	GtkWidget* button = gtk_button_new_with_mnemonic(label);
	g_object_set_data(G_OBJECT(button), "msgbox-response", GINT_TO_POINTER(response));
	g_signal_connect(button, "clicked", G_CALLBACK(button_clicked_cb), dlg);
	return button;
}

static gboolean idle_present_dialog(gpointer user_data) {
	RunData* data = (RunData*)user_data;
	gtk_window_set_modal(GTK_WINDOW(data->dlg), TRUE);
	g_object_set_data(G_OBJECT(data->dlg), "msgbox-run-data", data);
	gtk_widget_set_visible(data->dlg, TRUE);
	return G_SOURCE_REMOVE;
}

static gint run_dialog_synchronous(GtkWidget* dlg) {
	RunData data;
	data.dlg = dlg;
	data.done = false;
	data.response = GTK_RESPONSE_NONE;
	g_mutex_init(&data.mutex);
	g_cond_init(&data.cond);

	g_object_set_data(G_OBJECT(dlg), "msgbox-run-data", &data);
	gtk_window_set_modal(GTK_WINDOW(dlg), TRUE);
	gtk_widget_set_visible(dlg, TRUE);

	g_mutex_lock(&data.mutex);
	while (!data.done) {
		g_cond_wait(&data.cond, &data.mutex);
		// keep the main loop running so the dialog can process events
		if (g_main_context_pending(NULL)) {
			g_main_context_iteration(NULL, FALSE);
		}
	}
	g_mutex_unlock(&data.mutex);

	g_object_set_data(G_OBJECT(dlg), "msgbox-run-data", NULL);
	g_mutex_clear(&data.mutex);
	g_cond_clear(&data.cond);
	return data.response;
}

static const char* icon_name_for_type(MessageBox::IconType iconType) {
	switch (iconType)
	{
		case MessageBox::ICON_TYPE_QUESTION:
			return "dialog-question";
		case MessageBox::ICON_TYPE_WARNING:
			return "dialog-warning";
		case MessageBox::ICON_TYPE_ERROR:
			return "dialog-error";
		default:
			return "dialog-information";
	}
}

class MessageBox::PrivateImpl
{
public:
	PrivateImpl(MessageBox* parent, IconType iconType, 
		ButtonType buttonType, std::string msg, std::string details)
	    	: m_pParent(parent),
			m_IconType(iconType), m_ButtonType(buttonType), m_strMsg(msg),
			m_strDetails(details)
	{
		m_pDlg = gtk_window_new();
		gtk_window_set_resizable(GTK_WINDOW(m_pDlg), TRUE);

		GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
		gtk_widget_set_margin_top(box, 12);
		gtk_widget_set_margin_bottom(box, 12);
		gtk_widget_set_margin_start(box, 12);
		gtk_widget_set_margin_end(box, 12);
		gtk_window_set_child(GTK_WINDOW(m_pDlg), box);

		GtkWidget* hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
		gtk_box_append(GTK_BOX(box), hbox);

		GtkWidget* image = gtk_image_new_from_icon_name(icon_name_for_type(iconType));
		gtk_image_set_pixel_size(GTK_IMAGE(image), 48);
		gtk_widget_set_valign(image, GTK_ALIGN_START);
		gtk_box_append(GTK_BOX(hbox), image);

		GtkWidget* labels = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
		gtk_box_append(GTK_BOX(hbox), labels);

		GtkWidget* label = gtk_label_new(m_strMsg.c_str());
		gtk_label_set_wrap(GTK_LABEL(label), TRUE);
		gtk_label_set_xalign(GTK_LABEL(label), 0.0);
		gtk_widget_set_halign(label, GTK_ALIGN_FILL);
		gtk_box_append(GTK_BOX(labels), label);

		GtkWidget* detailsLabel = gtk_label_new(m_strDetails.c_str());
		gtk_label_set_wrap(GTK_LABEL(detailsLabel), TRUE);
		gtk_label_set_xalign(GTK_LABEL(detailsLabel), 0.0);
		gtk_widget_set_halign(detailsLabel, GTK_ALIGN_FILL);
		gtk_label_set_selectable(GTK_LABEL(detailsLabel), TRUE);
		gtk_box_append(GTK_BOX(labels), detailsLabel);

		GtkHeaderBar* hbar = GTK_HEADER_BAR(gtk_header_bar_new());
		gtk_header_bar_set_show_title_buttons(GTK_HEADER_BAR(hbar), FALSE);
		gtk_window_set_titlebar(GTK_WINDOW(m_pDlg), GTK_WIDGET(hbar));

		add_standard_buttons(buttonType, hbar);
	}

	~PrivateImpl()
	{
		if (!ThreadUtil::IsGUIThread()) {
			g_idle_add(idle_destroy_dialog, m_pDlg);
		} else {
			gtk_window_destroy (GTK_WINDOW(m_pDlg));
		}
	}

	ResponseType Run()
	{
		gint response;
		if (!ThreadUtil::IsGUIThread()) {
			RunData data;
			data.dlg = m_pDlg;
			data.done = false;
			data.response = GTK_RESPONSE_NONE;
			g_mutex_init(&data.mutex);
			g_cond_init(&data.cond);
			
			g_idle_add(idle_present_dialog, &data);
			
			g_mutex_lock(&data.mutex);
			while (!data.done) {
				g_cond_wait(&data.cond, &data.mutex);
			}
			g_mutex_unlock(&data.mutex);
			response = data.response;
			
			g_mutex_clear(&data.mutex);
			g_cond_clear(&data.cond);
		} else {
			response = run_dialog_synchronous (m_pDlg);
		}

		ResponseType responseType;
		switch (response)
		{
			case GTK_RESPONSE_NONE:
				responseType = RESPONSE_TYPE_NONE;
				break;
			case GTK_RESPONSE_OK:
				responseType = RESPONSE_TYPE_OK;
				break;
			case GTK_RESPONSE_CANCEL:
				responseType = RESPONSE_TYPE_CANCEL;
				break;
			case GTK_RESPONSE_CLOSE:
				responseType = RESPONSE_TYPE_CLOSE;
				break;
			case GTK_RESPONSE_YES:
				responseType = RESPONSE_TYPE_YES;
				break;
			case GTK_RESPONSE_NO:
				responseType = RESPONSE_TYPE_NO;
				break;
			default:
				responseType = (ResponseType)response;
				break;
		}

		return responseType;
	}

	void AddButton(const std::string &text, ResponseType respType)
	{
		gint response = response_type_to_gint(respType);
		GtkWidget* button = make_button(m_pDlg, text.c_str(), response);
		m_buttons.push_back(button);
		GtkHeaderBar* hbar = GTK_HEADER_BAR(gtk_window_get_titlebar(GTK_WINDOW(m_pDlg)));
		gtk_header_bar_pack_end(hbar, button);
	}

	void SetDefaultResponseType(ResponseType respType)
	{
		gint response = response_type_to_gint(respType);
		for (size_t i = 0 ; i < m_buttons.size() ; i++)
		{
			gint btnResp = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(m_buttons[i]), "msgbox-response"));
			if (btnResp == response)
			{
				gtk_window_set_default_widget(GTK_WINDOW(m_pDlg), m_buttons[i]);
				break;
			}
		}
	}

private:
	static gint response_type_to_gint(ResponseType respType)
	{
		switch (respType)
		{
			case RESPONSE_TYPE_NONE:
				return GTK_RESPONSE_NONE;
			case RESPONSE_TYPE_OK:
				return GTK_RESPONSE_OK;
			case RESPONSE_TYPE_CANCEL:
				return GTK_RESPONSE_CANCEL;
			case RESPONSE_TYPE_CLOSE:
				return GTK_RESPONSE_CLOSE;
			case RESPONSE_TYPE_YES:
				return GTK_RESPONSE_YES;
			case RESPONSE_TYPE_NO:
				return GTK_RESPONSE_NO;
			default:
				return (gint) respType;
		}
	}

	void add_standard_buttons(ButtonType buttonType, GtkHeaderBar* hbar)
	{
		std::vector<std::pair<const char*, gint> > buttons;
		switch (buttonType)
		{
			case BUTTON_TYPE_NONE:
				break;
			case BUTTON_TYPE_OK:
				buttons.push_back(std::make_pair("_OK", GTK_RESPONSE_OK));
				break;
			case BUTTON_TYPE_CLOSE:
				buttons.push_back(std::make_pair("_Close", GTK_RESPONSE_CLOSE));
				break;
			case BUTTON_TYPE_CANCEL:
				buttons.push_back(std::make_pair("_Cancel", GTK_RESPONSE_CANCEL));
				break;
			case BUTTON_TYPE_YES_NO:
				buttons.push_back(std::make_pair("_Yes", GTK_RESPONSE_YES));
				buttons.push_back(std::make_pair("_No", GTK_RESPONSE_NO));
				break;
			case BUTTON_TYPE_OK_CANCEL:
				buttons.push_back(std::make_pair("_OK", GTK_RESPONSE_OK));
				buttons.push_back(std::make_pair("_Cancel", GTK_RESPONSE_CANCEL));
				break;
			default:
				break;
		}
		for (size_t i = 0 ; i < buttons.size() ; i++)
		{
			GtkWidget* button = make_button(m_pDlg, buttons[i].first, buttons[i].second);
			m_buttons.push_back(button);
			gtk_header_bar_pack_end(hbar, button);
		}
	}

	MessageBox* m_pParent;
	GtkWidget* m_pDlg;
	std::vector<GtkWidget*> m_buttons;

	IconType m_IconType;
	ButtonType m_ButtonType;
	std::string m_strMsg;
	std::string m_strDetails;

};


MessageBox::MessageBox(IconType iconType, ButtonType buttonType, std::string msg, std::string details)
	: m_pPrivateImpl(new PrivateImpl(this, iconType, buttonType, msg, details))
{
}


MessageBox::~MessageBox()
{
	delete m_pPrivateImpl;
}

MessageBox::ResponseType MessageBox::Run()
{
	return m_pPrivateImpl->Run();
}

MessageBox::ResponseType  MessageBox::Run(IconType iconType, ButtonType buttonType, std::string msg, std::string details)
{
	MessageBox taskMsgBox(iconType, buttonType, msg, details);
	return taskMsgBox.Run();
}


void MessageBox::AddButton(BUTTON_ICON icon, const std::string &text, ResponseType respType)
{
	(void)icon;
	AddButton(text, respType);
}

void MessageBox::AddButton(const std::string &text, ResponseType resp_type)
{
	m_pPrivateImpl->AddButton(text, resp_type);
}

void MessageBox::SetDefaultResponseType(ResponseType respType)
{
	m_pPrivateImpl->SetDefaultResponseType(respType);
}


