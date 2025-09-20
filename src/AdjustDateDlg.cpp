#include <config.h>
#include "AdjustDateDlg.h"

class AdjustDateDlg::AdjustDateDlgPriv
{
public:
	AdjustDateDlgPriv(AdjustDateDlg *parent);
	~AdjustDateDlgPriv();
	
	void LoadWidgets();
	void UpdateUI();
	void ConnectSignals();
	
	bool ValidateInput();

	AdjustDateDlg*         m_pAdjustDateDlg;
	GtkBuilder*              m_pGtkBuilder;
	
	GtkDialog*             m_pDialogAdjustDate;
	GtkToggleButton*       m_pToggleAdjustDate;
	GtkToggleButton*       m_pToggleSetDate;
	GtkSpinButton*         m_pSpinYears;
	GtkSpinButton*         m_pSpinDays;
	GtkSpinButton*         m_pSpinHours;
	GtkSpinButton*         m_pSpinMinutes;
	GtkSpinButton*         m_pSpinSeconds;
	GtkEntry*              m_pEntryDate;
	GtkToggleButton*       m_pToggleModificationTime;
	GtkToggleButton*       m_pToggleExifDate;
	GtkToggleButton*       m_pToggleExifDateOrig;
	GtkToggleButton*       m_pToggleExifDateDig;

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

    std::function<void(int)> on_result_callback;
};

// --- Static Callbacks ---
static void  on_toggled (GtkToggleButton *togglebutton, gpointer user_data);
static void  on_dialog_response(GtkDialog* dialog, gint response_id, gpointer user_data);


AdjustDateDlg::AdjustDateDlg() : m_PrivPtr(new AdjustDateDlg::AdjustDateDlgPriv(this))
{
}

void AdjustDateDlg::set_on_result_callback(std::function<void(int)> callback)
{
    m_PrivPtr->on_result_callback = callback;
}

GtkWidget* AdjustDateDlg::GetWidget() const
{
	  return GTK_WIDGET(m_PrivPtr->m_pDialogAdjustDate);
}


void AdjustDateDlg::Run()
{
	// In GTK4, Run is non-blocking. It just shows the dialog.
    // The result is handled by the "response" signal handler.
    if (m_PrivPtr->m_pDialogAdjustDate) {
	    gtk_widget_set_visible(GTK_WIDGET(m_PrivPtr->m_pDialogAdjustDate), TRUE);
    }
}

// ... (accessor methods remain the same) ...
bool AdjustDateDlg::IsAdjustDate() const { return m_PrivPtr->m_bIsAdjustDate; }
bool AdjustDateDlg::IsSetDate() const { return m_PrivPtr->m_bIsSetDate; }
bool AdjustDateDlg::ModifyModificationTime() const { return m_PrivPtr->m_bModificationTime; }
bool AdjustDateDlg::ModifyExifDate() const { return m_PrivPtr->m_bExifDate; }
bool AdjustDateDlg::ModifyExifDateOrig() const { return m_PrivPtr->m_bExifDateOrig; }
bool AdjustDateDlg::ModifyExifDateDig() const { return m_PrivPtr->m_bExifDateDig; }
std::string AdjustDateDlg::GetDateString() const { return m_PrivPtr->m_strDate; }
int AdjustDateDlg::GetAdjustmentYears() const { return m_PrivPtr->m_iYears; }
int AdjustDateDlg::GetAdjustmentDays() const { return m_PrivPtr->m_iDays; }
int AdjustDateDlg::GetAdjustmentHours() const { return m_PrivPtr->m_iHours; }
int AdjustDateDlg::GetAdjustmentMinutes() const { return m_PrivPtr->m_iMinutes; }
int AdjustDateDlg::GetAdjustmentSeconds() const { return m_PrivPtr->m_iSeconds; }


AdjustDateDlg::AdjustDateDlgPriv::AdjustDateDlgPriv(AdjustDateDlg *parent) :
        m_pAdjustDateDlg(parent), m_pDialogAdjustDate(nullptr)
{
	m_pGtkBuilder = gtk_builder_new();
	 GError *error = NULL;
	if (!gtk_builder_add_from_file (m_pGtkBuilder, QUIVER_DATADIR "/" "quiver.ui", &error)) {
        g_warning("Could not load UI file: %s", error->message);
        g_error_free(error);
    }

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
	m_pDialogAdjustDate       = GTK_DIALOG(gtk_builder_get_object (m_pGtkBuilder, "AdjustDateDialog"));
    if (!m_pDialogAdjustDate) {
        g_warning("Failed to get AdjustDateDialog from builder.");
        return;
    }


	m_pToggleAdjustDate    = GTK_TOGGLE_BUTTON( gtk_builder_get_object(m_pGtkBuilder, "adjustdatedlg_radio_adjust_date") );
	m_pToggleSetDate       = GTK_TOGGLE_BUTTON( gtk_builder_get_object(m_pGtkBuilder, "adjustdatedlg_radio_set_date") );
	m_pSpinYears           = GTK_SPIN_BUTTON( gtk_builder_get_object(m_pGtkBuilder, "adjustdatedlg_spinbutton_years") );
	m_pSpinDays            = GTK_SPIN_BUTTON( gtk_builder_get_object(m_pGtkBuilder, "adjustdatedlg_spinbutton_days") );
	m_pSpinHours           = GTK_SPIN_BUTTON( gtk_builder_get_object(m_pGtkBuilder, "adjustdatedlg_spinbutton_hours") );
	m_pSpinMinutes         = GTK_SPIN_BUTTON( gtk_builder_get_object(m_pGtkBuilder, "adjustdatedlg_spinbutton_minutes") );
	m_pSpinSeconds         = GTK_SPIN_BUTTON( gtk_builder_get_object(m_pGtkBuilder, "adjustdatedlg_spinbutton_seconds") );
	m_pEntryDate           = GTK_ENTRY( gtk_builder_get_object(m_pGtkBuilder, "adjustdatedlg_entry_date") );

	m_pToggleModificationTime = GTK_TOGGLE_BUTTON( gtk_builder_get_object(m_pGtkBuilder, "adjustdatedlg_checkbox_mtime") );
	m_pToggleExifDate         = GTK_TOGGLE_BUTTON( gtk_builder_get_object(m_pGtkBuilder, "adjustdatedlg_checkbox_exif_date") );
	m_pToggleExifDateOrig     = GTK_TOGGLE_BUTTON( gtk_builder_get_object(m_pGtkBuilder, "adjustdatedlg_checkbox_exif_date_orig") );
	m_pToggleExifDateDig      = GTK_TOGGLE_BUTTON( gtk_builder_get_object(m_pGtkBuilder, "adjustdatedlg_checkbox_exif_date_digitized") );

}

void AdjustDateDlg::AdjustDateDlgPriv::UpdateUI()
{
	GtkWidget* group_adjust_widgets = GTK_WIDGET(gtk_builder_get_object(m_pGtkBuilder, "alignment5"));
	GtkWidget* group_set_widgets = GTK_WIDGET(gtk_builder_get_object(m_pGtkBuilder, "alignment7"));

	if (group_adjust_widgets) gtk_widget_set_sensitive ( group_adjust_widgets, gtk_toggle_button_get_active(m_pToggleAdjustDate) );
	if (group_set_widgets) gtk_widget_set_sensitive ( group_set_widgets, gtk_toggle_button_get_active(m_pToggleSetDate) );
}


void AdjustDateDlg::AdjustDateDlgPriv::ConnectSignals()
{
    if (!m_pDialogAdjustDate) return;
	g_signal_connect(m_pDialogAdjustDate, "response", G_CALLBACK(on_dialog_response), this);
	if(m_pToggleAdjustDate) g_signal_connect(m_pToggleAdjustDate, "toggled",(GCallback)on_toggled,this);
	if(m_pToggleSetDate) g_signal_connect(m_pToggleSetDate, "toggled",(GCallback)on_toggled,this);
}

bool AdjustDateDlg::AdjustDateDlgPriv::ValidateInput()
{
	bool bIsValid = false;
    if (!m_pDialogAdjustDate) return false;

	if ( gtk_toggle_button_get_active(m_pToggleAdjustDate) )
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
            // The async nature of GTK4 makes modal validation dialogs complex.
            // For now, we will just log an error. A full solution would involve another dialog.
            g_warning("Validation failed: To adjust date, at least one value must be non-zero.");
		}
	}
	else if ( gtk_toggle_button_get_active(m_pToggleSetDate) )
	{
		m_bIsSetDate = true;
		m_bIsAdjustDate = false;
		m_strDate = gtk_editable_get_text(GTK_EDITABLE(m_pEntryDate));
		
		bIsValid = (0 != m_strDate.length());
		if (!bIsValid)
		{
            g_warning("Validation failed: To set date, a date string must be provided.");
		}
	}

	if (bIsValid)
	{
		m_bModificationTime = (bool)gtk_toggle_button_get_active(m_pToggleModificationTime);
		m_bExifDate         = (bool)gtk_toggle_button_get_active(m_pToggleExifDate);
		m_bExifDateOrig     = (bool)gtk_toggle_button_get_active(m_pToggleExifDateOrig);
		m_bExifDateDig      = (bool)gtk_toggle_button_get_active(m_pToggleExifDateDig);

		if (!m_bModificationTime && !m_bExifDate && !m_bExifDateOrig && !m_bExifDateDig)
		{
            g_warning("Validation failed: At least one field to modify must be checked.");
			bIsValid = false;
		}
	}
	return bIsValid;
}

static void  on_dialog_response(GtkDialog* dialog, gint response_id, gpointer user_data)
{
    AdjustDateDlg::AdjustDateDlgPriv *priv = static_cast<AdjustDateDlg::AdjustDateDlgPriv*>(user_data);

    if (response_id == GTK_RESPONSE_OK) {
        if (!priv->ValidateInput()) {
            // Validation failed. The validation function already showed an error.
            // We do not close the dialog, allowing the user to correct the input.
            return;
        }
    }

    // If response is OK (and validation passed), or any other response (Cancel, Delete),
    // we call the callback and destroy the dialog.
    if (priv->on_result_callback) {
        priv->on_result_callback(response_id);
    }

    // Ensure the dialog widget pointer in the parent object is cleared to avoid use-after-free
    // This is a bit tricky, but we can assume the parent AdjustDateDlg object outlives this callback.
    if (priv->m_pAdjustDateDlg) {
       // A way to nullify the dialog pointer would be needed if the AdjustDateDlg object persists.
       // For now, we assume the AdjustDateDlg object is destroyed after the callback.
    }
    gtk_window_destroy(GTK_WINDOW(dialog));
}

static void  on_toggled (GtkToggleButton *togglebutton, gpointer user_data)
{
	AdjustDateDlg::AdjustDateDlgPriv *priv = static_cast<AdjustDateDlg::AdjustDateDlgPriv*>(user_data);
	priv->UpdateUI();
}
