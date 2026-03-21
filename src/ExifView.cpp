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
    gpointer m_pUIManager; // Stub for now
};

ExifView::ExifView() : m_ExifViewImplPtr(new ExifViewImpl(this)) {
    m_ExifViewImplPtr->m_pWidget = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
}

ExifView::~ExifView() {}

GtkWidget* ExifView::GetWidget() { return m_ExifViewImplPtr->m_pWidget; }

void ExifView::SetQuiverFile(QuiverFile f) {}
void ExifView::SetUIManager(gpointer ui_manager) { m_ExifViewImplPtr->m_pUIManager = ui_manager; }

// Stubs for other methods to allow compilation
void ExifView::Update() {}
void ExifView::Clear() {}
