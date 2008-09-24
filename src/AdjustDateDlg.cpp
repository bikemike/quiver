#include <config.h>
#include "AdjustDateDlg.h"
#include <glade/glade.h>

#include "QuiverStockIcons.h"


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
	GladeXML*              m_pGladeXML;
	
	// dlg widgets
	GtkDialog*             m_pDialogAdjustDate;
	GtkWidget*             m_pButtonOK;
	GtkToggleButton*       m_pToggleAdjustDate;
	GtkToggleButton*       m_pToggleSetDate;
	GtkSpinButton*         m_pSpinYears;
	GtkSpinButton*         m_pSpinDays;
	GtkSpinButton*         m_pSpinHours;
	GtkSpinButton*         m_pSpinMinutes;
	GtkSpinButton*         m_pSpinSeconds;
	GtkEntry*              m_pEntryDate;
	GtkToggleButton*       m_pToggleExifDate;
	GtkToggleButton*       m_pToggleExifDateOrig;
	GtkToggleButton*       m_pToggleExifDateDig;

	bool                   m_bIsAdjustDate;
	bool                   m_bIsSetDate;

	bool                   m_bExifDate;
	bool                   m_bExifDateOrig;
	bool                   m_bExifDateDig;

	std::string            m_strDate;
	int                    m_iYears;
	int                    m_iDays;
	int                    m_iHours;
	int                    m_iMinutes;
	int                    m_iSeconds;
	
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
	gint result = gtk_dialog_run(GTK_DIALOG(m_PrivPtr->m_pDialogAdjustDate));
	gtk_widget_destroy(GTK_WIDGET(m_PrivPtr->m_pDialogAdjustDate));
	return (GTK_RESPONSE_OK == result);
}

bool AdjustDateDlg::IsAdjustDate() const
{
	return m_PrivPtr->m_bIsAdjustDate;
}

bool AdjustDateDlg::IsSetDate() const
{
	return m_PrivPtr->m_bIsSetDate;
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
static void  on_toggled (GtkToggleButton *togglebutton, gpointer user_data);

AdjustDateDlg::AdjustDateDlgPriv::AdjustDateDlgPriv(AdjustDateDlg *parent) :
        m_pAdjustDateDlg(parent)
{
	m_pGladeXML = glade_xml_new (QUIVER_GLADEDIR "/" "quiver.glade", "AdjustDateDialog", NULL);

	LoadWidgets();
	UpdateUI();
	ConnectSignals();
}

AdjustDateDlg::AdjustDateDlgPriv::~AdjustDateDlgPriv()
{
	if (NULL != m_pGladeXML)
	{
		g_object_unref(m_pGladeXML);
		m_pGladeXML = NULL;
	}
}


void AdjustDateDlg::AdjustDateDlgPriv::LoadWidgets()
{
	m_pDialogAdjustDate       = GTK_DIALOG(glade_xml_get_widget (m_pGladeXML, "AdjustDateDialog"));

	m_pButtonOK               = gtk_button_new_from_stock(QUIVER_STOCK_OK);
	gtk_widget_show(m_pButtonOK);
	gtk_container_add(GTK_CONTAINER(m_pDialogAdjustDate->action_area),m_pButtonOK);

	m_pToggleAdjustDate    = GTK_TOGGLE_BUTTON( glade_xml_get_widget(m_pGladeXML, "adjustdatedlg_radio_adjust_date") );
	m_pToggleSetDate       = GTK_TOGGLE_BUTTON( glade_xml_get_widget(m_pGladeXML, "adjustdatedlg_radio_set_date") );
	m_pSpinYears           = GTK_SPIN_BUTTON( glade_xml_get_widget(m_pGladeXML, "adjustdatedlg_spinbutton_years") );
	m_pSpinDays            = GTK_SPIN_BUTTON( glade_xml_get_widget(m_pGladeXML, "adjustdatedlg_spinbutton_days") );
	m_pSpinHours           = GTK_SPIN_BUTTON( glade_xml_get_widget(m_pGladeXML, "adjustdatedlg_spinbutton_hours") );
	m_pSpinMinutes         = GTK_SPIN_BUTTON( glade_xml_get_widget(m_pGladeXML, "adjustdatedlg_spinbutton_minutes") );
	m_pSpinSeconds         = GTK_SPIN_BUTTON( glade_xml_get_widget(m_pGladeXML, "adjustdatedlg_spinbutton_seconds") );
	m_pEntryDate           = GTK_ENTRY( glade_xml_get_widget(m_pGladeXML, "adjustdatedlg_entry_date") );

	m_pToggleExifDate      = GTK_TOGGLE_BUTTON( glade_xml_get_widget(m_pGladeXML, "adjustdatedlg_checkbox_exif_date") );
	m_pToggleExifDateOrig  = GTK_TOGGLE_BUTTON( glade_xml_get_widget(m_pGladeXML, "adjustdatedlg_checkbox_exif_date_orig") );
	m_pToggleExifDateDig   = GTK_TOGGLE_BUTTON( glade_xml_get_widget(m_pGladeXML, "adjustdatedlg_checkbox_exif_date_digitized") );

}

void AdjustDateDlg::AdjustDateDlgPriv::UpdateUI()
{
	 
	gtk_widget_set_sensitive ( glade_xml_get_widget(m_pGladeXML, "alignment5"), gtk_toggle_button_get_active(m_pToggleAdjustDate) );
	gtk_widget_set_sensitive ( glade_xml_get_widget(m_pGladeXML, "alignment7"), gtk_toggle_button_get_active(m_pToggleSetDate) );
}



void AdjustDateDlg::AdjustDateDlgPriv::ConnectSignals()
{
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
			GtkWidget* dialog = gtk_message_dialog_new (GTK_WINDOW(m_pDialogAdjustDate),
				GTK_DIALOG_DESTROY_WITH_PARENT,
				GTK_MESSAGE_ERROR,
				GTK_BUTTONS_CLOSE,
				"To adjust the date of the picture(s), you must enter at least one value in the years, days, hours, minutes, or seconds field.");
			gtk_dialog_run (GTK_DIALOG (dialog));
			gtk_widget_destroy (dialog);
		}
	}
	else if ( gtk_toggle_button_get_active(m_pToggleSetDate) )
	{
		m_bIsSetDate = true;
		m_bIsAdjustDate = false;
		m_strDate = gtk_entry_get_text(m_pEntryDate);
		
		bIsValid = (0 != m_strDate.length());
		if (!bIsValid)
		{
			GtkWidget* dialog = gtk_message_dialog_new (GTK_WINDOW(m_pDialogAdjustDate),
				GTK_DIALOG_DESTROY_WITH_PARENT,
				GTK_MESSAGE_ERROR,
				GTK_BUTTONS_CLOSE,
				"To set the date of the picture(s) you must enter the date in the following format: YYYY:MM:DD HH:MM:SS.");
			gtk_dialog_run (GTK_DIALOG (dialog));
			gtk_widget_destroy (dialog);
		}

	}

	if (bIsValid)
	{

		m_bExifDate     = (bool)gtk_toggle_button_get_active(m_pToggleExifDate);
		m_bExifDateOrig = (bool)gtk_toggle_button_get_active(m_pToggleExifDateOrig);
		m_bExifDateDig  = (bool)gtk_toggle_button_get_active(m_pToggleExifDateDig);

		if (!m_bExifDate && !m_bExifDateOrig && !m_bExifDateDig)
		{

			GtkWidget* dialog = gtk_message_dialog_new (GTK_WINDOW(m_pDialogAdjustDate),
				GTK_DIALOG_DESTROY_WITH_PARENT,
				GTK_MESSAGE_ERROR,
				GTK_BUTTONS_CLOSE,
				"You must have at least one field checked off.");
			gtk_dialog_run (GTK_DIALOG (dialog));
			gtk_widget_destroy (dialog);

			bIsValid = false;
		}
	}

	return bIsValid;
}

static void  on_clicked (GtkButton *button, gpointer   user_data)
{
	AdjustDateDlg::AdjustDateDlgPriv *priv = static_cast<AdjustDateDlg::AdjustDateDlgPriv*>(user_data);
	if (priv->ValidateInput())
	{
		gtk_dialog_response(priv->m_pDialogAdjustDate, GTK_RESPONSE_OK);
	}
}

static void  on_toggled (GtkToggleButton *togglebutton, gpointer user_data)
{
	AdjustDateDlg::AdjustDateDlgPriv *priv = static_cast<AdjustDateDlg::AdjustDateDlgPriv*>(user_data);
	
	priv->UpdateUI();
}


