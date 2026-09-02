#include <config.h>
#include "AdjustDateDlg.h"

#include "QuiverStockIcons.h"

static void show_error_dialog(GtkWindow* parent, const char* message)
{
	GtkWidget* dialog = gtk_window_new();
	gtk_window_set_title(GTK_WINDOW(dialog), "Error");
	gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
	gtk_window_set_transient_for(GTK_WINDOW(dialog), parent);
	gtk_window_set_resizable(GTK_WINDOW(dialog), FALSE);
	GtkWidget* dlgBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
	gtk_widget_set_margin_start(dlgBox, 12);
	gtk_widget_set_margin_end(dlgBox, 12);
	gtk_widget_set_margin_top(dlgBox, 12);
	gtk_widget_set_margin_bottom(dlgBox, 12);
	GtkWidget* dlgLabel = gtk_label_new(message);
	gtk_label_set_wrap(GTK_LABEL(dlgLabel), TRUE);
	gtk_label_set_xalign(GTK_LABEL(dlgLabel), 0.0);
	gtk_box_append(GTK_BOX(dlgBox), dlgLabel);
	GtkWidget* btnBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	gtk_widget_set_halign(btnBox, GTK_ALIGN_END);
	gtk_box_append(GTK_BOX(dlgBox), btnBox);
	gtk_window_set_child(GTK_WINDOW(dialog), dlgBox);
	GtkWidget* btnClose = gtk_button_new_with_label("Close");
	gtk_box_append(GTK_BOX(btnBox), btnClose);
	g_signal_connect_swapped(btnClose, "clicked",
		G_CALLBACK(gtk_window_destroy), dialog);
	gtk_window_present(GTK_WINDOW(dialog));
}


class AdjustDateDlg::AdjustDateDlgPriv
{
public:
// constructor, destructor
	AdjustDateDlgPriv(AdjustDateDlg *parent);
	~AdjustDateDlgPriv();
	
// methods
	void LoadWidgets();
	void UpdateUI();
	void ConnectSignals();
	
	bool ValidateInput();

// variables
	AdjustDateDlg*         m_pAdjustDateDlg;
	GtkBuilder*              m_pGtkBuilder;
	
	// dlg widgets
	GtkWidget*             m_pDialogAdjustDate;
	GtkWidget*             m_pButtonOK;
	GtkCheckButton*        m_pToggleAdjustDate;
	GtkCheckButton*        m_pToggleSetDate;
	GtkSpinButton*         m_pSpinYears;
	GtkSpinButton*         m_pSpinDays;
	GtkSpinButton*         m_pSpinHours;
	GtkSpinButton*         m_pSpinMinutes;
	GtkSpinButton*         m_pSpinSeconds;
	GtkEntry*              m_pEntryDate;
	GtkCheckButton*        m_pToggleModificationTime;
	GtkCheckButton*        m_pToggleExifDate;
	GtkCheckButton*        m_pToggleExifDateOrig;
	GtkCheckButton*        m_pToggleExifDateDig;

	bool                   m_bIsAdjustDate;
	bool                   m_bIsSetDate;

	bool                   m_bModificationTime;
	bool                   m_bExifDate;
	bool                   m_bExifDateOrig;
	bool                   m_bExifDateDig;

	std::string            m_strDate;
	int                    m_iYears;
	int                    m_iDays;
	int                    m_iHours;
	int                    m_iMinutes;
	int                    m_iSeconds;

	bool                   m_bRunDone;
	gint                   m_iRunResponse;
	
};


AdjustDateDlg::AdjustDateDlg() : m_PrivPtr(new AdjustDateDlg::AdjustDateDlgPriv(this))
{
	
}


GtkWidget* AdjustDateDlg::GetWidget() const
{
	  return NULL;
}


bool AdjustDateDlg::Run()
{
	m_PrivPtr->m_bRunDone = false;
	m_PrivPtr->m_iRunResponse = GTK_RESPONSE_NONE;
	gtk_window_set_modal(GTK_WINDOW(m_PrivPtr->m_pDialogAdjustDate), TRUE);
	gtk_widget_set_visible(GTK_WIDGET(m_PrivPtr->m_pDialogAdjustDate), TRUE);

	GMainContext* ctx = g_main_context_default();
	while (!m_PrivPtr->m_bRunDone)
	{
		g_main_context_iteration(ctx, TRUE);
	}
	return (GTK_RESPONSE_OK == m_PrivPtr->m_iRunResponse);
}

bool AdjustDateDlg::IsAdjustDate() const
{
	return m_PrivPtr->m_bIsAdjustDate;
}

bool AdjustDateDlg::IsSetDate() const
{
	return m_PrivPtr->m_bIsSetDate;
}

bool AdjustDateDlg::ModifyModificationTime() const
{
	return m_PrivPtr->m_bModificationTime;
}

bool AdjustDateDlg::ModifyExifDate() const
{
	return m_PrivPtr->m_bExifDate;
}

bool AdjustDateDlg::ModifyExifDateOrig() const
{
	return m_PrivPtr->m_bExifDateOrig;
}

bool AdjustDateDlg::ModifyExifDateDig() const
{
	return m_PrivPtr->m_bExifDateDig;
}


std::string AdjustDateDlg::GetDateString() const
{
	return m_PrivPtr->m_strDate;
}

int AdjustDateDlg::GetAdjustmentYears() const
{
	return m_PrivPtr->m_iYears;
}

int AdjustDateDlg::GetAdjustmentDays() const
{
	return m_PrivPtr->m_iDays;
}

int AdjustDateDlg::GetAdjustmentHours() const
{
	return m_PrivPtr->m_iHours;
}

int AdjustDateDlg::GetAdjustmentMinutes() const
{
	return m_PrivPtr->m_iMinutes;
}

int AdjustDateDlg::GetAdjustmentSeconds() const
{
	return m_PrivPtr->m_iSeconds;
}

// private stuff


// prototypes
static void  on_clicked (GtkButton *button, gpointer   user_data);
static void  on_toggled (GtkCheckButton *togglebutton, gpointer user_data);

AdjustDateDlg::AdjustDateDlgPriv::AdjustDateDlgPriv(AdjustDateDlg *parent) :
        m_pAdjustDateDlg(parent)
{
	m_pGtkBuilder = gtk_builder_new();
	const char* objectids[] = {
		"AdjustDateDialog", 
		"adjustment3", 
		"adjustment4", 
		"adjustment5", 
		"adjustment6", 
		"adjustment7", 
		NULL};
	gtk_builder_add_objects_from_file (m_pGtkBuilder, QUIVER_DATADIR "/" "quiver.ui", objectids, NULL);
	m_bRunDone = false;
	m_iRunResponse = GTK_RESPONSE_NONE;

	LoadWidgets();
	UpdateUI();
	ConnectSignals();
}

AdjustDateDlg::AdjustDateDlgPriv::~AdjustDateDlgPriv()
{
	if (NULL != m_pGtkBuilder)
	{
		g_object_unref(m_pGtkBuilder);
		m_pGtkBuilder = NULL;
	}
}


void AdjustDateDlg::AdjustDateDlgPriv::LoadWidgets()
{
	m_pDialogAdjustDate       = GTK_WIDGET(gtk_builder_get_object (m_pGtkBuilder, "AdjustDateDialog"));

	m_pButtonOK               = gtk_button_new_with_mnemonic("_OK");
	if (m_pDialogAdjustDate)
	{
		GtkHeaderBar* hbar = GTK_HEADER_BAR(gtk_header_bar_new());
		gtk_header_bar_set_show_title_buttons(GTK_HEADER_BAR(hbar), TRUE);
		gtk_header_bar_pack_end(hbar, GTK_WIDGET(m_pButtonOK));
		gtk_window_set_titlebar(GTK_WINDOW(m_pDialogAdjustDate), GTK_WIDGET(hbar));
	}

	m_pToggleAdjustDate    = GTK_CHECK_BUTTON( gtk_builder_get_object(m_pGtkBuilder, "adjustdatedlg_radio_adjust_date") );
	m_pToggleSetDate       = GTK_CHECK_BUTTON( gtk_builder_get_object(m_pGtkBuilder, "adjustdatedlg_radio_set_date") );
	m_pSpinYears           = GTK_SPIN_BUTTON( gtk_builder_get_object(m_pGtkBuilder, "adjustdatedlg_spinbutton_years") );
	m_pSpinDays            = GTK_SPIN_BUTTON( gtk_builder_get_object(m_pGtkBuilder, "adjustdatedlg_spinbutton_days") );
	m_pSpinHours           = GTK_SPIN_BUTTON( gtk_builder_get_object(m_pGtkBuilder, "adjustdatedlg_spinbutton_hours") );
	m_pSpinMinutes         = GTK_SPIN_BUTTON( gtk_builder_get_object(m_pGtkBuilder, "adjustdatedlg_spinbutton_minutes") );
	m_pSpinSeconds         = GTK_SPIN_BUTTON( gtk_builder_get_object(m_pGtkBuilder, "adjustdatedlg_spinbutton_seconds") );
	m_pEntryDate           = GTK_ENTRY( gtk_builder_get_object(m_pGtkBuilder, "adjustdatedlg_entry_date") );

	m_pToggleModificationTime = GTK_CHECK_BUTTON( gtk_builder_get_object(m_pGtkBuilder, "adjustdatedlg_checkbox_mtime") );
	m_pToggleExifDate         = GTK_CHECK_BUTTON( gtk_builder_get_object(m_pGtkBuilder, "adjustdatedlg_checkbox_exif_date") );
	m_pToggleExifDateOrig     = GTK_CHECK_BUTTON( gtk_builder_get_object(m_pGtkBuilder, "adjustdatedlg_checkbox_exif_date_orig") );
	m_pToggleExifDateDig      = GTK_CHECK_BUTTON( gtk_builder_get_object(m_pGtkBuilder, "adjustdatedlg_checkbox_exif_date_digitized") );

}

void AdjustDateDlg::AdjustDateDlgPriv::UpdateUI()
{
	 
	gtk_widget_set_sensitive ( GTK_WIDGET(gtk_builder_get_object(m_pGtkBuilder, "alignment5")), gtk_check_button_get_active(m_pToggleAdjustDate) );
	gtk_widget_set_sensitive ( GTK_WIDGET(gtk_builder_get_object(m_pGtkBuilder, "alignment7")), gtk_check_button_get_active(m_pToggleSetDate) );
}


void AdjustDateDlg::AdjustDateDlgPriv::ConnectSignals()
{
	g_signal_connect(m_pDialogAdjustDate, "close-request",
		G_CALLBACK(+[](GtkWidget* widget, gpointer user_data) -> gboolean {
			AdjustDateDlg::AdjustDateDlgPriv* priv = static_cast<AdjustDateDlg::AdjustDateDlgPriv*>(user_data);
			priv->m_iRunResponse = GTK_RESPONSE_CANCEL;
			priv->m_bRunDone = true;
			gtk_widget_set_visible(widget, FALSE);
			return TRUE;
		}), this);

	g_signal_connect(GTK_BUTTON(m_pButtonOK),
		"clicked",(GCallback)on_clicked,this);	

	g_signal_connect(m_pToggleAdjustDate,
		"toggled",(GCallback)on_toggled,this);	

	g_signal_connect(m_pToggleSetDate,
		"toggled",(GCallback)on_toggled,this);
}

bool AdjustDateDlg::AdjustDateDlgPriv::ValidateInput()
{
	bool bIsValid = false;
	if ( gtk_check_button_get_active(m_pToggleAdjustDate) )
	{
		m_bIsSetDate = false;
		m_bIsAdjustDate = true;
		m_iYears = (int)gtk_spin_button_get_value(m_pSpinYears);
		m_iDays = (int)gtk_spin_button_get_value(m_pSpinDays);
		m_iHours = (int)gtk_spin_button_get_value(m_pSpinHours);
		m_iMinutes = (int)gtk_spin_button_get_value(m_pSpinMinutes);
		m_iSeconds = (int)gtk_spin_button_get_value(m_pSpinSeconds);

		bIsValid = (0 != m_iYears || 0 != m_iDays || 0 != m_iHours || 0 != m_iMinutes || 0 != m_iSeconds);
		
		if (!bIsValid)
		{
			show_error_dialog(GTK_WINDOW(m_pDialogAdjustDate),
				"To adjust the date of the picture(s), you must enter at least one value in the years, days, hours, minutes, or seconds field.");
		}
	}
	else if ( gtk_check_button_get_active(m_pToggleSetDate) )
	{
		m_bIsSetDate = true;
		m_bIsAdjustDate = false;
		m_strDate = gtk_editable_get_text(GTK_EDITABLE(m_pEntryDate));
		
		bIsValid = (0 != m_strDate.length());
		if (!bIsValid)
		{
			show_error_dialog(GTK_WINDOW(m_pDialogAdjustDate),
				"To set the date of the picture(s) you must enter the date in the following format: YYYY:MM:DD HH:MM:SS.");
		}

	}

	if (bIsValid)
	{

		m_bModificationTime = (bool)gtk_check_button_get_active(m_pToggleModificationTime);
		m_bExifDate         = (bool)gtk_check_button_get_active(m_pToggleExifDate);
		m_bExifDateOrig     = (bool)gtk_check_button_get_active(m_pToggleExifDateOrig);
		m_bExifDateDig      = (bool)gtk_check_button_get_active(m_pToggleExifDateDig);

		if (!m_bModificationTime && !m_bExifDate && !m_bExifDateOrig && !m_bExifDateDig)
		{
			show_error_dialog(GTK_WINDOW(m_pDialogAdjustDate),
				"You must have at least one field checked off.");

			bIsValid = false;
		}
	}

	return bIsValid;
}

static void  on_clicked (GtkButton *button, gpointer   user_data)
{ (void)button; 
	AdjustDateDlg::AdjustDateDlgPriv *priv = static_cast<AdjustDateDlg::AdjustDateDlgPriv*>(user_data);
	if (priv->ValidateInput())
	{
		priv->m_iRunResponse = GTK_RESPONSE_OK;
		priv->m_bRunDone = true;
		gtk_widget_set_visible(GTK_WIDGET(priv->m_pDialogAdjustDate), FALSE);
	}
}

static void  on_toggled (GtkCheckButton *togglebutton, gpointer user_data)
{ (void)togglebutton; 
	AdjustDateDlg::AdjustDateDlgPriv *priv = static_cast<AdjustDateDlg::AdjustDateDlgPriv*>(user_data);
	
	priv->UpdateUI();
}


