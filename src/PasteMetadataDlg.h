#ifndef PASTE_METADATA_DLG_H
#define PASTE_METADATA_DLG_H

#include <gtk/gtk.h>
#include <memory>
#include "QuiverMetadata.h"

class PasteMetadataDlg
{
public:
	PasteMetadataDlg(const QuiverMetadata::MetadataPayload& payload,
	                 size_t targetCount,
	                 bool allTargetsAreImages);
	~PasteMetadataDlg();

	bool Run();
	QuiverMetadata::PasteOptions GetOptions() const;

private:
	class PasteMetadataDlgPriv;
	std::unique_ptr<PasteMetadataDlgPriv> m_priv;
};

#endif // PASTE_METADATA_DLG_H
