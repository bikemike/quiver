#include "MessageBox.h"
#include <gtk/gtk.h>
#include <vector>

class MessageBox::PrivateImpl
{
public:
    PrivateImpl(GtkWindow* parent,
                IconType iconType,
                ButtonType buttonType,
                const std::string& msg,
                const std::string& details)
        : m_parent(parent),
          m_dialog(nullptr),
          m_response(RESPONSE_TYPE_NONE)
    {
        GtkAlertDialog* alert = gtk_alert_dialog_new("%s", msg.c_str());
        if (!details.empty()) {
            gtk_alert_dialog_set_detail(alert, details.c_str());
        }

        std::vector<const char*> buttons;

        switch (buttonType)
        {
            case BUTTON_TYPE_OK:
                buttons.push_back("Ok");
                m_responses.push_back(RESPONSE_TYPE_OK);
                break;
            case BUTTON_TYPE_CLOSE:
                buttons.push_back("Close");
                m_responses.push_back(RESPONSE_TYPE_CLOSE);
                break;
            case BUTTON_TYPE_CANCEL:
                buttons.push_back("Cancel");
                m_responses.push_back(RESPONSE_TYPE_CANCEL);
                break;
            case BUTTON_TYPE_YES_NO:
                buttons.push_back("No");
                m_responses.push_back(RESPONSE_TYPE_NO);
                buttons.push_back("Yes");
                m_responses.push_back(RESPONSE_TYPE_YES);
                break;
            case BUTTON_TYPE_OK_CANCEL:
                buttons.push_back("Cancel");
                m_responses.push_back(RESPONSE_TYPE_CANCEL);
                buttons.push_back("Ok");
                m_responses.push_back(RESPONSE_TYPE_OK);
                break;
            default:
                break;
        }

        if (!buttons.empty()) {
            buttons.push_back(NULL); // Null-terminate the array
            gtk_alert_dialog_set_buttons(alert, &buttons[0]);
        }
        m_dialog = alert;
    }

    ~PrivateImpl()
    {
        if (m_dialog) {
            g_object_unref(m_dialog);
        }
    }

    static void response_callback(GObject* source, GAsyncResult* res, gpointer user_data)
    {
        PrivateImpl* self = static_cast<PrivateImpl*>(user_data);
        gint response_index = gtk_alert_dialog_choose_finish(GTK_ALERT_DIALOG(source), res, NULL);

        if (response_index >= 0 && (size_t)response_index < self->m_responses.size()) {
            self->m_response = (ResponseType)self->m_responses[response_index];
        } else {
            self->m_response = RESPONSE_TYPE_NONE;
        }

        // Quit the nested main loop
        if (self->m_loop) {
            g_main_loop_quit(self->m_loop);
        }
    }

    ResponseType Run()
    {
        gtk_alert_dialog_choose(m_dialog, m_parent, NULL, response_callback, this);

        // Block until a response is received.
        m_loop = g_main_loop_new(NULL, FALSE);
        g_main_loop_run(m_loop);
        g_main_loop_unref(m_loop);
        m_loop = nullptr;

        return m_response;
    }

    GtkWindow* m_parent;
    GtkAlertDialog* m_dialog;
    ResponseType m_response;
    GMainLoop* m_loop = nullptr;
    std::vector<int> m_responses;
};


MessageBox::MessageBox(GtkWindow* parent, IconType iconType, ButtonType buttonType, std::string msg, std::string details)
    : m_pPrivateImpl(new PrivateImpl(parent, iconType, buttonType, msg, details))
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

MessageBox::ResponseType MessageBox::Run(GtkWindow* parent, IconType iconType, ButtonType buttonType, std::string msg, std::string details)
{
    MessageBox taskMsgBox(parent, iconType, buttonType, msg, details);
    return taskMsgBox.Run();
}

void MessageBox::AddButton(BUTTON_ICON icon, const std::string &text, ResponseType respType)
{
    // This is now handled by the GtkAlertDialog constructor. This function is now a no-op.
}

void MessageBox::AddButton(const std::string &text, ResponseType resp_type)
{
    // This is now handled by the GtkAlertDialog constructor. This function is now a no-op.
}

void MessageBox::SetDefaultResponseType(ResponseType respType)
{
    // This is now handled by the GtkAlertDialog constructor. This function is now a no-op.
}
