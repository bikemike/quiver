#include <config.h>

#include "ExternalToolAddEditDlg.h"
#include "QuiverStockIcons.h"
#include "ShortcutManager.h"

#include <list>
#include <vector>

using namespace std;

class ExternalToolAddEditDlg::ExternalToolAddEditDlgPriv
{
public:
// constructor, destructor
	ExternalToolAddEditDlgPriv(ExternalTool b, ExternalToolAddEditDlg *parent);
	~ExternalToolAddEditDlgPriv();
	
// methods
	void LoadWidgets();
	void UpdateUI();
	void ConnectSignals();
	void UpdateShortcutLabel();

// variables
	ExternalToolAddEditDlg*     m_pExternalToolAddEditDlg;
	GtkBuilder*         m_pGtkBuilder;
	ExternalTool m_ExternalTool;
	bool m_bCancelled;

	bool m_bLoadedDlg;
	
	// dlg widgets
	GtkWidget*             m_pWidget;
	GtkEntry*              m_pEntryName;
	GtkEntry*              m_pEntryTooltip;
	GtkEntry*              m_pEntryCmd;
	GtkEntry*              m_pEntryIcon;
	GtkButton*             m_pButtonOk;
	GtkButton*             m_pButtonCancel;
	GtkCheckButton*        m_pToggleMultiple;
	GtkCheckButton*        m_pToggleShowOnlyOnError;
	GtkLabel*              m_pLabelShortcut;
	GtkButton*             m_pButtonShortcutSet;
	GtkButton*             m_pButtonShortcutClear;
	std::string            m_strCurrentShortcut;
	GMainLoop*             m_pRunLoop;
	gint                   m_iRunResponse;
};


ExternalToolAddEditDlg::ExternalToolAddEditDlg() : m_PrivPtr(new ExternalToolAddEditDlg::ExternalToolAddEditDlgPriv(ExternalTool(),this))
{
}
ExternalToolAddEditDlg::ExternalToolAddEditDlg(ExternalTool b) : m_PrivPtr(new ExternalToolAddEditDlg::ExternalToolAddEditDlgPriv(b,this))
{
}


ExternalTool ExternalToolAddEditDlg::GetExternalTool() const
{
	  return m_PrivPtr->m_ExternalTool;
}

GtkWidget* ExternalToolAddEditDlg::GetWidget() const
{
	  return NULL;
}

void ExternalToolAddEditDlg::Run()
{
	if (m_PrivPtr->m_bLoadedDlg)
	{
		m_PrivPtr->m_pRunLoop = g_main_loop_new(NULL, FALSE);
		m_PrivPtr->m_iRunResponse = 0;
		gtk_window_set_modal(GTK_WINDOW(m_PrivPtr->m_pWidget), TRUE);
		gtk_widget_set_visible(m_PrivPtr->m_pWidget, TRUE);
		g_main_loop_run(m_PrivPtr->m_pRunLoop);
		g_main_loop_unref(m_PrivPtr->m_pRunLoop);
		m_PrivPtr->m_pRunLoop = NULL;

		if (GTK_RESPONSE_OK == m_PrivPtr->m_iRunResponse)
		{
			m_PrivPtr->m_bCancelled = false;

			m_PrivPtr->m_ExternalTool.SetName( gtk_editable_get_text(GTK_EDITABLE(m_PrivPtr->m_pEntryName)) );
			std::string tooltip = gtk_editable_get_text(GTK_EDITABLE(m_PrivPtr->m_pEntryTooltip));
			if (tooltip.empty())
			{
				tooltip = gtk_editable_get_text(GTK_EDITABLE(m_PrivPtr->m_pEntryName));
			}
			m_PrivPtr->m_ExternalTool.SetTooltip( tooltip );
			m_PrivPtr->m_ExternalTool.SetIcon( gtk_editable_get_text(GTK_EDITABLE(m_PrivPtr->m_pEntryIcon)) );
			m_PrivPtr->m_ExternalTool.SetCmd( gtk_editable_get_text(GTK_EDITABLE(m_PrivPtr->m_pEntryCmd)) );
			m_PrivPtr->m_ExternalTool.SetSupportsMultiple( gtk_check_button_get_active(m_PrivPtr->m_pToggleMultiple) ? true : false );
			if (m_PrivPtr->m_pToggleShowOnlyOnError)
			{
				m_PrivPtr->m_ExternalTool.SetShowOnlyOnError( gtk_check_button_get_active(m_PrivPtr->m_pToggleShowOnlyOnError) ? true : false );
			}
			m_PrivPtr->m_ExternalTool.SetShortcut( m_PrivPtr->m_strCurrentShortcut );
		}
		if (NULL != m_PrivPtr->m_pWidget)
		{
			gtk_window_destroy(GTK_WINDOW(m_PrivPtr->m_pWidget));
			m_PrivPtr->m_pWidget = NULL;
		}
	}
}

bool ExternalToolAddEditDlg::Cancelled() const
{
	return m_PrivPtr->m_bCancelled;
}
// private stuff


// prototypes
static void  on_clicked (GtkButton *button, gpointer user_data);
//static void  on_toggled (GtkToggleButton *button, gpointer user_data);

ExternalToolAddEditDlg::ExternalToolAddEditDlgPriv::ExternalToolAddEditDlgPriv(ExternalTool b, ExternalToolAddEditDlg *parent) :
        m_pExternalToolAddEditDlg(parent), m_ExternalTool(b)
{
	m_bLoadedDlg = false;
	m_bCancelled = true;


	m_pGtkBuilder = gtk_builder_new();
	const gchar* objectids[] = {
		"ExternalToolAddEditDialog",
		NULL};
	gtk_builder_add_objects_from_file (m_pGtkBuilder, quiver_get_resource_path("quiver.ui").c_str(), objectids, NULL);
	LoadWidgets();
	UpdateUI();
	ConnectSignals();
}

ExternalToolAddEditDlg::ExternalToolAddEditDlgPriv::~ExternalToolAddEditDlgPriv()
{
	if (NULL != m_pWidget)
	{
		gtk_window_destroy(GTK_WINDOW(m_pWidget));
		m_pWidget = NULL;
	}
	if (NULL != m_pGtkBuilder)
	{
		g_object_unref(m_pGtkBuilder);
		m_pGtkBuilder = NULL;
	}
}


void ExternalToolAddEditDlg::ExternalToolAddEditDlgPriv::UpdateShortcutLabel()
{
	if (m_pLabelShortcut)
	{
		if (m_strCurrentShortcut.empty())
		{
			gtk_label_set_text(m_pLabelShortcut, "None");
		}
		else
		{
			std::string human = ShortcutManager::AccelStringToHumanLabel(m_strCurrentShortcut);
			gtk_label_set_text(m_pLabelShortcut, human.c_str());
		}
	}
}

struct ToolKeyCaptureState {
	ExternalToolAddEditDlg::ExternalToolAddEditDlgPriv *priv;
	std::string captured_accel;
	std::string conflicting_label;
	GtkWidget *dialog;
	GtkLabel *lbl_display;
	GtkLabel *lbl_conflict;
	GtkButton *btn_assign;
};

static gboolean on_tool_capture_key_pressed(GtkEventControllerKey *controller, guint keyval, guint keycode, GdkModifierType state, gpointer user_data)
{
	(void)controller; (void)keycode;
	ToolKeyCaptureState *data = static_cast<ToolKeyCaptureState*>(user_data);

	if (keyval == GDK_KEY_Escape) {
		gtk_window_destroy(GTK_WINDOW(data->dialog));
		return TRUE;
	}

	if (keyval == GDK_KEY_Control_L || keyval == GDK_KEY_Control_R ||
		keyval == GDK_KEY_Shift_L || keyval == GDK_KEY_Shift_R ||
		keyval == GDK_KEY_Alt_L || keyval == GDK_KEY_Alt_R ||
		keyval == GDK_KEY_Super_L || keyval == GDK_KEY_Super_R ||
		keyval == GDK_KEY_Meta_L || keyval == GDK_KEY_Meta_R) {
		return TRUE;
	}

	std::string accel = ShortcutManager::KeyvalAndModsToAccelString(keyval, state);
	if (accel.empty()) return TRUE;

	std::string human = ShortcutManager::AccelStringToHumanLabel(accel);
	data->captured_accel = accel;

	gchar *markup = g_markup_printf_escaped("<span size='xx-large'><b>%s</b></span>", human.c_str());
	gtk_label_set_markup(data->lbl_display, markup);
	g_free(markup);

	std::string current_action = "ExternalTool_" + std::to_string(data->priv->m_ExternalTool.GetID());
	std::string conflict = ShortcutManager::GetInstance().FindConflictingAction(accel, current_action);
	data->conflicting_label = conflict;

	if (!conflict.empty()) {
		gchar *c_markup = g_markup_printf_escaped("<span foreground='#e67e22'>⚠️ Already assigned to '<b>%s</b>'.</span>", conflict.c_str());
		gtk_label_set_markup(data->lbl_conflict, c_markup);
		g_free(c_markup);
		gtk_widget_set_visible(GTK_WIDGET(data->lbl_conflict), TRUE);
	} else {
		gtk_widget_set_visible(GTK_WIDGET(data->lbl_conflict), FALSE);
	}

	gtk_widget_set_sensitive(GTK_WIDGET(data->btn_assign), TRUE);
	return TRUE;
}

static void on_tool_capture_assign_clicked(GtkButton *btn, gpointer user_data)
{
	(void)btn;
	ToolKeyCaptureState *data = static_cast<ToolKeyCaptureState*>(user_data);
	if (!data->captured_accel.empty()) {
		data->priv->m_strCurrentShortcut = data->captured_accel;
		data->priv->UpdateShortcutLabel();
	}
	gtk_window_destroy(GTK_WINDOW(data->dialog));
}

static void on_tool_capture_cancel_clicked(GtkButton *btn, gpointer user_data)
{
	(void)btn;
	ToolKeyCaptureState *data = static_cast<ToolKeyCaptureState*>(user_data);
	gtk_window_destroy(GTK_WINDOW(data->dialog));
}

static void on_tool_capture_destroy(GtkWidget *widget, gpointer user_data)
{
	(void)widget;
	ToolKeyCaptureState *data = static_cast<ToolKeyCaptureState*>(user_data);
	delete data;
}

static void ShowToolKeyCaptureDialog(ExternalToolAddEditDlg::ExternalToolAddEditDlgPriv *priv)
{
	ToolKeyCaptureState *data = new ToolKeyCaptureState();
	data->priv = priv;

	data->dialog = gtk_window_new();
	gtk_window_set_title(GTK_WINDOW(data->dialog), "Record Shortcut");
	gtk_window_set_modal(GTK_WINDOW(data->dialog), TRUE);
	if (priv->m_pWidget) {
		gtk_window_set_transient_for(GTK_WINDOW(data->dialog), GTK_WINDOW(priv->m_pWidget));
	}
	gtk_window_set_default_size(GTK_WINDOW(data->dialog), 380, 220);

	GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
	gtk_widget_set_margin_start(vbox, 24);
	gtk_widget_set_margin_end(vbox, 24);
	gtk_widget_set_margin_top(vbox, 20);
	gtk_widget_set_margin_bottom(vbox, 20);
	gtk_widget_set_halign(vbox, GTK_ALIGN_FILL);
	gtk_widget_set_valign(vbox, GTK_ALIGN_CENTER);

	GtkWidget *lbl_instruct = gtk_label_new("Press the key combination you want to assign\nor press Escape to cancel.");
	gtk_label_set_justify(GTK_LABEL(lbl_instruct), GTK_JUSTIFY_CENTER);
	gtk_box_append(GTK_BOX(vbox), lbl_instruct);

	data->lbl_display = GTK_LABEL(gtk_label_new(NULL));
	gtk_label_set_markup(data->lbl_display, "<span size='xx-large' foreground='gray'><b>Press keys...</b></span>");
	gtk_widget_set_margin_top(GTK_WIDGET(data->lbl_display), 12);
	gtk_widget_set_margin_bottom(GTK_WIDGET(data->lbl_display), 8);
	gtk_box_append(GTK_BOX(vbox), GTK_WIDGET(data->lbl_display));

	data->lbl_conflict = GTK_LABEL(gtk_label_new(NULL));
	gtk_label_set_justify(GTK_LABEL(data->lbl_conflict), GTK_JUSTIFY_CENTER);
	gtk_label_set_wrap(data->lbl_conflict, TRUE);
	gtk_widget_set_visible(GTK_WIDGET(data->lbl_conflict), FALSE);
	gtk_box_append(GTK_BOX(vbox), GTK_WIDGET(data->lbl_conflict));

	GtkWidget *hbox_btns = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	gtk_widget_set_halign(hbox_btns, GTK_ALIGN_END);
	gtk_widget_set_margin_top(hbox_btns, 12);

	GtkWidget *btn_cancel = gtk_button_new_with_label("Cancel");
	g_signal_connect(btn_cancel, "clicked", G_CALLBACK(on_tool_capture_cancel_clicked), data);
	gtk_box_append(GTK_BOX(hbox_btns), btn_cancel);

	data->btn_assign = GTK_BUTTON(gtk_button_new_with_label("Assign"));
	gtk_widget_add_css_class(GTK_WIDGET(data->btn_assign), "suggested-action");
	gtk_widget_set_sensitive(GTK_WIDGET(data->btn_assign), FALSE);
	g_signal_connect(data->btn_assign, "clicked", G_CALLBACK(on_tool_capture_assign_clicked), data);
	gtk_box_append(GTK_BOX(hbox_btns), GTK_WIDGET(data->btn_assign));

	gtk_box_append(GTK_BOX(vbox), hbox_btns);

	GtkEventController *key_ctrl = gtk_event_controller_key_new();
	g_signal_connect(key_ctrl, "key-pressed", G_CALLBACK(on_tool_capture_key_pressed), data);
	gtk_widget_add_controller(data->dialog, key_ctrl);

	g_signal_connect(data->dialog, "destroy", G_CALLBACK(on_tool_capture_destroy), data);

	gtk_window_set_child(GTK_WINDOW(data->dialog), vbox);
	gtk_widget_set_visible(data->dialog, TRUE);
}

void ExternalToolAddEditDlg::ExternalToolAddEditDlgPriv::LoadWidgets()
{

	if (NULL != m_pGtkBuilder)
	{
		m_pWidget                = GTK_WIDGET( gtk_builder_get_object (m_pGtkBuilder, "ExternalToolAddEditDialog"));

		m_pButtonCancel          = GTK_BUTTON( gtk_button_new_with_mnemonic("_Cancel") );
		m_pButtonOk              = GTK_BUTTON( gtk_button_new_with_mnemonic("_OK") );

		if (m_pWidget)
		{
			GtkHeaderBar* hbar = GTK_HEADER_BAR(gtk_header_bar_new());
			gtk_header_bar_set_show_title_buttons(GTK_HEADER_BAR(hbar), TRUE);
			gtk_header_bar_pack_end(hbar, GTK_WIDGET(m_pButtonCancel));
			gtk_header_bar_pack_end(hbar, GTK_WIDGET(m_pButtonOk));
			gtk_widget_add_css_class(GTK_WIDGET(m_pButtonOk), "suggested-action");
			gtk_window_set_default_widget(GTK_WINDOW(m_pWidget), GTK_WIDGET(m_pButtonOk));
			gtk_window_set_titlebar(GTK_WINDOW(m_pWidget), GTK_WIDGET(hbar));
		}

		m_pToggleMultiple        = GTK_CHECK_BUTTON( gtk_builder_get_object(m_pGtkBuilder, "external_tools_edit_multiple"));
		m_pToggleShowOnlyOnError = GTK_CHECK_BUTTON( gtk_builder_get_object(m_pGtkBuilder, "external_tools_edit_show_only_on_error"));
		m_pEntryName             = GTK_ENTRY        ( gtk_builder_get_object(m_pGtkBuilder, "external_tools_edit_name"));
		m_pEntryCmd              = GTK_ENTRY        ( gtk_builder_get_object(m_pGtkBuilder, "external_tools_edit_cmd"));
		m_pEntryTooltip          = GTK_ENTRY        ( gtk_builder_get_object(m_pGtkBuilder, "external_tools_edit_tooltip"));
		m_pEntryIcon             = GTK_ENTRY        ( gtk_builder_get_object(m_pGtkBuilder, "external_tools_edit_icon"));
		m_pLabelShortcut         = GTK_LABEL        ( gtk_builder_get_object(m_pGtkBuilder, "external_tools_edit_shortcut_label"));
		m_pButtonShortcutSet     = GTK_BUTTON       ( gtk_builder_get_object(m_pGtkBuilder, "external_tools_edit_shortcut_set"));
		m_pButtonShortcutClear   = GTK_BUTTON       ( gtk_builder_get_object(m_pGtkBuilder, "external_tools_edit_shortcut_clear"));

		m_bLoadedDlg = (
				NULL != m_pWidget && 
				NULL != m_pButtonOk && 
				NULL != m_pButtonCancel && 
				NULL != m_pEntryName && 
				NULL != m_pEntryCmd && 
				NULL != m_pEntryTooltip && 
				NULL != m_pEntryIcon && 
				NULL != m_pToggleMultiple
				); 

		if (m_bLoadedDlg)
		{
			gtk_editable_set_text(GTK_EDITABLE(m_pEntryName), m_ExternalTool.GetName().c_str());
			gtk_editable_set_text(GTK_EDITABLE(m_pEntryCmd), m_ExternalTool.GetCmd().c_str());
			gtk_editable_set_text(GTK_EDITABLE(m_pEntryTooltip), m_ExternalTool.GetTooltip().c_str());
			gtk_editable_set_text(GTK_EDITABLE(m_pEntryIcon), m_ExternalTool.GetIcon().c_str());

			gtk_entry_set_activates_default(m_pEntryName, TRUE);
			gtk_entry_set_activates_default(m_pEntryCmd, TRUE);
			gtk_entry_set_activates_default(m_pEntryTooltip, TRUE);
			gtk_entry_set_activates_default(m_pEntryIcon, TRUE);

			gtk_check_button_set_active(m_pToggleMultiple, m_ExternalTool.GetSupportsMultiple() ? TRUE : FALSE );
			if (m_pToggleShowOnlyOnError)
			{
				gtk_check_button_set_active(m_pToggleShowOnlyOnError, m_ExternalTool.GetShowOnlyOnError() ? TRUE : FALSE );
			}
			m_strCurrentShortcut = m_ExternalTool.GetShortcut();
			UpdateShortcutLabel();
		}
	}
}

void ExternalToolAddEditDlg::ExternalToolAddEditDlgPriv::UpdateUI()
{
	if (m_bLoadedDlg)
	{
		
	}	
}


void ExternalToolAddEditDlg::ExternalToolAddEditDlgPriv::ConnectSignals()
{
	if (m_bLoadedDlg)
	{
		g_signal_connect(m_pWidget, "close-request",
			G_CALLBACK(+[](GtkWidget* widget, gpointer user_data) -> gboolean {
				(void)widget;
				ExternalToolAddEditDlg::ExternalToolAddEditDlgPriv *priv =
					static_cast<ExternalToolAddEditDlg::ExternalToolAddEditDlgPriv*>(user_data);
				priv->m_iRunResponse = GTK_RESPONSE_CANCEL;
				if (NULL != priv->m_pRunLoop)
					g_main_loop_quit(priv->m_pRunLoop);
				return TRUE;
			}), this);

		GtkEventController *key_ctrl = gtk_event_controller_key_new();
		g_signal_connect(key_ctrl, "key-pressed",
			G_CALLBACK(+[](GtkEventControllerKey *controller, guint keyval, guint keycode, GdkModifierType state, gpointer user_data) -> gboolean {
				(void)controller; (void)keycode; (void)state;
				if (keyval == GDK_KEY_Escape) {
					ExternalToolAddEditDlg::ExternalToolAddEditDlgPriv *priv =
						static_cast<ExternalToolAddEditDlg::ExternalToolAddEditDlgPriv*>(user_data);
					priv->m_iRunResponse = GTK_RESPONSE_CANCEL;
					if (NULL != priv->m_pRunLoop)
						g_main_loop_quit(priv->m_pRunLoop);
					return GDK_EVENT_STOP;
				}
				return GDK_EVENT_PROPAGATE;
			}), this);
		gtk_widget_add_controller(m_pWidget, key_ctrl);

		g_signal_connect(m_pButtonOk,
			"clicked",(GCallback)on_clicked,this);
		g_signal_connect(m_pButtonCancel,
			"clicked",(GCallback)on_clicked,this);

		if (m_pButtonShortcutSet) {
			g_signal_connect(m_pButtonShortcutSet, "clicked",
				G_CALLBACK(+[](GtkButton*, gpointer user_data) {
					ShowToolKeyCaptureDialog(static_cast<ExternalToolAddEditDlgPriv*>(user_data));
				}), this);
		}
		if (m_pButtonShortcutClear) {
			g_signal_connect(m_pButtonShortcutClear, "clicked",
				G_CALLBACK(+[](GtkButton*, gpointer user_data) {
					auto* priv = static_cast<ExternalToolAddEditDlgPriv*>(user_data);
					priv->m_strCurrentShortcut = "";
					priv->UpdateShortcutLabel();
				}), this);
		}
	}
}

static void  on_clicked (GtkButton *button, gpointer user_data)
{
	ExternalToolAddEditDlg::ExternalToolAddEditDlgPriv *priv = static_cast<ExternalToolAddEditDlg::ExternalToolAddEditDlgPriv*>(user_data);
	
	if (button == priv->m_pButtonOk)
	{
		priv->m_iRunResponse = GTK_RESPONSE_OK;
		if (NULL != priv->m_pRunLoop)
			g_main_loop_quit(priv->m_pRunLoop);
	}
	else if (button == priv->m_pButtonCancel)
	{
		priv->m_iRunResponse = GTK_RESPONSE_CANCEL;
		if (NULL != priv->m_pRunLoop)
			g_main_loop_quit(priv->m_pRunLoop);
	}
}

