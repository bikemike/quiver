#ifndef PASTE_METADATA_TASK_H
#define PASTE_METADATA_TASK_H

#include "AbstractTask.h"
#include "QuiverFile.h"
#include "QuiverMetadata.h"
#include <vector>
#include <string>
#include <functional>

class PasteMetadataTask : public AbstractTask
{
public:
	typedef std::function<void(QuiverFile, bool)> NotifyCallback;

	PasteMetadataTask(const std::vector<QuiverFile>& files,
	                  const QuiverMetadata::MetadataPayload& payload,
	                  const QuiverMetadata::PasteOptions& options,
	                  NotifyCallback notifyCb = nullptr);
	virtual ~PasteMetadataTask();

	virtual std::string GetDescription() const override;
	virtual std::string GetIterationTypeName(bool shortname, bool plural = true) const override;
	virtual int GetTotalIterations() const override;
	virtual int GetCurrentIteration() const override;
	virtual double GetProgress() const override;

	void AddFile(const QuiverFile& f);

protected:
	virtual void Run() override;

private:
	std::vector<QuiverFile> m_files;
	QuiverMetadata::MetadataPayload m_payload;
	QuiverMetadata::PasteOptions m_options;
	NotifyCallback m_notifyCb;
	size_t m_currentIdx;
	double m_fileSavePercent;
};

typedef boost::shared_ptr<PasteMetadataTask> PasteMetadataTaskPtr;

#endif // PASTE_METADATA_TASK_H
