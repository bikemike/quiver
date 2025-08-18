#include "MessageBox.h"

#include <gtk/gtk.h>

#include "ThreadUtil.h"

class MessageBox::PrivateImpl
{
public:
	PrivateImpl(MessageBox* parent, IconType iconType, 
		ButtonType buttonType, std::string msg, std::string details)
	    	: m_pParent(parent),
			m_IconType(iconType), m_ButtonType(buttonType), m_strMsg(msg),
			m_strDetails(details)
	{
		// translate buttonType to gtk type
		GtkButtonsType button_type;
		switch (m_ButtonType)
		{
			case BUTTON_TYPE_NONE:
				button_type = GTK_BUTTONS_NONE;
				break;
			case BUTTON_TYPE_OK:
				button_type = GTK_BUTTONS_OK;
				break;
			case BUTTON_TYPE_CLOSE:
				button_type = GTK_BUTTONS_CLOSE;
				break;
			case BUTTON_TYPE_CANCEL:
				button_type = GTK_BUTTONS_CANCEL;
				break;
			case BUTTON_TYPE_YES_NO:
				button_type = GTK_BUTTONS_YES_NO;
				break;
			case BUTTON_TYPE_OK_CANCEL:
				button_type = GTK_BUTTONS_OK_CANCEL;
				break;
			default:
				button_type = GTK_BUTTONS_NONE;
				break;
		}

		// translate icon type to gtk type
		GtkMessageType messageType;
		switch (m_IconType)
		{
			case ICON_TYPE_INFO:
				messageType = GTK_MESSAGE_INFO;
				break;
			case ICON_TYPE_QUESTION:
				messageType = GTK_MESSAGE_QUESTION;
				break;
			case ICON_TYPE_WARNING:
				messageType = GTK_MESSAGE_WARNING;
				break;
			case ICON_TYPE_ERROR:
				messageType = GTK_MESSAGE_ERROR;
				break;
			default:
				messageType = GTK_MESSAGE_INFO;
				break;
		}

		// FIXME : NULL should be the main application window
		GtkWindow *window = NULL;
		m_pDlg = gtk_message_dialog_new(window, GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_OTHER, GTK_BUTTONS_NONE, "%s", m_strMsg.c_str());
		gtk_message_dialog_set_markup(GTK_MESSAGE_DIALOG(m_pDlg), m_strMsg.c_str());
		gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(m_pDlg), "%s", m_strDetails.c_str());
	}

	~PrivateImpl()
	{
		gtk_window_destroy (GTK_WINDOW(m_pDlg));
	}

	ResponseType Run()
	{
		g_signal_connect (m_pDlg, "response", G_CALLBACK (gtk_window_destroy), NULL);
		gtk_window_present(GTK_WINDOW(m_pDlg));
        return RESPONSE_TYPE_OK;
	}

	MessageBox* m_pParent;
	GtkWidget* m_pDlg;

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
	m_pPrivateImpl->Run();
}

MessageBox::ResponseType  MessageBox::Run(IconType iconType, ButtonType buttonType, std::string msg, std::string details)
{
	MessageBox taskMsgBox(iconType, buttonType, msg, details);
	return taskMsgBox.Run();
}


void MessageBox::AddButton(BUTTON_ICON icon, const std::string &text, ResponseType respType)
{
	const char* icon_name = NULL;
	if (icon == BUTTON_ICON_ADD) icon_name = "list-add-symbolic";
	else if (icon == BUTTON_ICON_APPLY) icon_name = "object-select-symbolic";
	else if (icon == BUTTON_ICON_CANCEL) icon_name = "window-close-symbolic";
	else if (icon == BUTTON_ICON_CLEAR) icon_name = "edit-clear-all-symbolic";
	else if (icon == BUTTON_ICON_CLOSE) icon_name = "window-close-symbolic";
	else if (icon == BUTTON_ICON_CONNECT) icon_name = "network-wired-symbolic";
	else if (icon == BUTTON_ICON_DELETE) icon_name = "edit-delete-symbolic";
	else if (icon == BUTTON_ICON_DIRECTORY) icon_name = "folder-symbolic";
	else if (icon == BUTTON_ICON_DISCONNECT) icon_name = "network-wired-disconnected-symbolic";
	else if (icon == BUTTON_ICON_EDIT) icon_name = "document-edit-symbolic";
	else if (icon == BUTTON_ICON_EXECUTE) icon_name = "system-run-symbolic";
	else if (icon == BUTTON_ICON_INFO) icon_name = "dialog-information-symbolic";
	else if (icon == BUTTON_ICON_NO) icon_name = "dialog-cancel-symbolic";
	else if (icon == BUTTON_ICON_OK) icon_name = "dialog-ok-symbolic";
	else if (icon == BUTTON_ICON_REDO) icon_name = "edit-redo-symbolic";
	else if (icon == BUTTON_ICON_REFRESH) icon_name = "view-refresh-symbolic";
	else if (icon == BUTTON_ICON_REMOVE) icon_name = "list-remove-symbolic";
	else if (icon == BUTTON_ICON_SAVE) icon_name = "document-save-symbolic";
	else if (icon == BUTTON_ICON_SAVE_AS) icon_name = "document-save-as-symbolic";
	else if (icon == BUTTON_ICON_STOP) icon_name = "process-stop-symbolic";
	else if (icon == BUTTON_ICON_UNDO) icon_name = "edit-undo-symbolic";
	else if (icon == BUTTON_ICON_YES) icon_name = "dialog-ok-symbolic";

	GtkWidget* button = gtk_button_new_with_mnemonic(text.c_str());
	if (icon_name) {
		GtkWidget* image = gtk_image_new_from_icon_name(icon_name);
		gtk_button_set_child(GTK_BUTTON(button), image);
	}
	gtk_widget_set_visible(button, true);
	gtk_widget_set_can_focus(button, TRUE);
	gtk_dialog_add_button(GTK_DIALOG(m_pPrivateImpl->m_pDlg), (const char*)button, (gint)respType);

}

void MessageBox::AddButton(const std::string &text, ResponseType resp_type)
{
	AddButton(BUTTON_ICON_NONE, text, resp_type);
}

void MessageBox::SetDefaultResponseType(ResponseType respType)
{
	gint response = GTK_RESPONSE_NONE;
	switch (respType)
	{
		case RESPONSE_TYPE_NONE:
			response = GTK_RESPONSE_NONE;
			break;
		case RESPONSE_TYPE_OK:
			response = GTK_RESPONSE_OK;
			break;
		case RESPONSE_TYPE_CANCEL:
			response = GTK_RESPONSE_CANCEL;
			break;
		case RESPONSE_TYPE_CLOSE:
			response = GTK_RESPONSE_CLOSE;
			break;
		case RESPONSE_TYPE_YES:
			response = GTK_RESPONSE_YES;
			break;
		case RESPONSE_TYPE_NO:
			response = GTK_RESPONSE_NO;
			break;
		default:
			response = (gint) respType;
	}

	gtk_dialog_set_default_response(GTK_DIALOG(m_pPrivateImpl->m_pDlg), response);
}



