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
		m_pDlg = gtk_message_dialog_new (window,
			  GTK_DIALOG_DESTROY_WITH_PARENT,
			  messageType,
			  button_type,
			  m_strMsg.c_str());

		gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(m_pDlg),
				m_strDetails.c_str());

	}

	~PrivateImpl()
	{
		if (!ThreadUtil::IsGUIThread())
		{
			gdk_threads_enter();
		}

		gtk_widget_destroy (m_pDlg);

		if (!ThreadUtil::IsGUIThread())
		{
			gdk_threads_leave();
		}

	}

	ResponseType Run()
	{
		if (!ThreadUtil::IsGUIThread())
		{
			gdk_threads_enter();
		}

		gint response = gtk_dialog_run (GTK_DIALOG (m_pDlg));

		if (!ThreadUtil::IsGUIThread())
		{
			gdk_threads_leave();
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
	const char* stock_icon;

	switch(icon)
	{
	case BUTTON_ICON_NONE:
		stock_icon = NULL;
		break;
	case BUTTON_ICON_ADD:
		stock_icon = GTK_STOCK_ADD;
		break;

	case BUTTON_ICON_APPLY:
		stock_icon = GTK_STOCK_APPLY;
		break;

	case BUTTON_ICON_CANCEL:
		stock_icon = GTK_STOCK_CANCEL;
		break;

	case BUTTON_ICON_CLEAR:
		stock_icon = GTK_STOCK_CLEAR;
		break;

	case BUTTON_ICON_CLOSE:
		stock_icon = GTK_STOCK_CLOSE;
		break;

	case BUTTON_ICON_CONNECT:
		stock_icon = GTK_STOCK_CONNECT;
		break;

	case BUTTON_ICON_DELETE:
		stock_icon = GTK_STOCK_DELETE;
		break;

	case BUTTON_ICON_DIRECTORY:
		stock_icon = GTK_STOCK_DIRECTORY;
		break;

	case BUTTON_ICON_DISCONNECT:
		stock_icon = GTK_STOCK_DISCONNECT;
		break;

	case BUTTON_ICON_EDIT:
		stock_icon = GTK_STOCK_EDIT;
		break;

	case BUTTON_ICON_EXECUTE:
		stock_icon = GTK_STOCK_EXECUTE;
		break;

	case BUTTON_ICON_INFO:
		stock_icon = GTK_STOCK_INFO;
		break;

	case BUTTON_ICON_NO:
		stock_icon = GTK_STOCK_NO;
		break;

	case BUTTON_ICON_OK:
		stock_icon = GTK_STOCK_OK;
		break;

	case BUTTON_ICON_REDO:
		stock_icon = GTK_STOCK_REDO;
		break;

	case BUTTON_ICON_REFRESH:
		stock_icon = GTK_STOCK_REFRESH;
		break;

	case BUTTON_ICON_REMOVE:
		stock_icon = GTK_STOCK_REMOVE;
		break;

	case BUTTON_ICON_SAVE:
		stock_icon = GTK_STOCK_SAVE;
		break;

	case BUTTON_ICON_SAVE_AS:
		stock_icon = GTK_STOCK_SAVE_AS;
		break;

	case BUTTON_ICON_STOP:
		stock_icon = GTK_STOCK_STOP;
		break;

	case BUTTON_ICON_UNDO:
		stock_icon = GTK_STOCK_UNDO;
		break;

	case BUTTON_ICON_YES:
		stock_icon = GTK_STOCK_YES;
		break;

	}

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

	GtkWidget* button = gtk_button_new_with_mnemonic(text.c_str());
	if ( NULL != stock_icon)
	{
		GtkWidget* image  = gtk_image_new_from_stock(stock_icon, GTK_ICON_SIZE_BUTTON);
		gtk_button_set_image(GTK_BUTTON(button), image);
	}
	gtk_widget_show(button);
	gtk_widget_set_can_default(button, TRUE);
	gtk_dialog_add_action_widget(GTK_DIALOG(m_pPrivateImpl->m_pDlg), button, response);

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

	gtk_dialog_set_default_response (GTK_DIALOG(m_pPrivateImpl->m_pDlg), response);
}



