#ifndef FILE_QUIVER_FILE_OPS_H
#define FILE_QUIVER_FILE_OPS_H

#include "QuiverFile.h"
#include <boost/shared_ptr.hpp>

namespace QuiverFileOps
{
	class StatusCallback
	{
	public:
		StatusCallback();
		virtual ~StatusCallback();
		double GetProgress ();
		void   Cancel();

		class PrivateImpl;
		typedef boost::shared_ptr<PrivateImpl> PrivateImplPtr;
	private:
		PrivateImplPtr m_PrivateImplPtr;
	};

	bool Delete(QuiverFile quiverFile);
	bool MoveToTrash(QuiverFile quiverFile);
	bool RestoreFromTrash(QuiverFile quiverFile);
	bool CopyFile(QuiverFile src, QuiverFile dst);
	bool MoveFile(QuiverFile src, QuiverFile dst);
}

#endif

