#include <config.h>
#include "ExifView.h"
#include <gtk/gtk.h>
#include "QuiverUtils.h"
#include "QuiverStockIcons.h"

using namespace std;

class ExifView::ExifViewImpl {
public:
    ExifViewImpl(ExifView *parent) : m_pExifView(parent) {}
    ExifView *m_pExifView;
    GtkWidget *m_pWidget;
    gpointer m_pUIManager;
    QuiverFile m_QuiverFile;
};

ExifView::ExifView() : m_ExifViewImplPtr(new ExifViewImpl(this)) {
    m_ExifViewImplPtr->m_pWidget = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
}

ExifView::~ExifView() {
}

GtkWidget* ExifView::GetWidget() { return m_ExifViewImplPtr->m_pWidget; }
void ExifView::SetQuiverFile(QuiverFile f) { m_ExifViewImplPtr->m_QuiverFile = f; }
void ExifView::SetUIManager(GtkUIManager *ui_manager) { m_ExifViewImplPtr->m_pUIManager = (gpointer)ui_manager; }
void ExifView::Update() {}
void ExifView::Clear() {}
