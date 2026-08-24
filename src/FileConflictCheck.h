#ifndef FILE_CONFLICT_CHECK_H
#define FILE_CONFLICT_CHECK_H

#include <string>
#include <vector>

#include <boost/shared_ptr.hpp>

#include <gio/gio.h>
#include <gtk/gtk.h>

// shared batch-rename/move collision analysis.
// A Mapping is one planned operation: source URI -> destination URI
// (destination includes the file name).  Check() flags every mapping
// whose destination equals another mapping's destination, or matches
// a file that already exists on disk (unless it is a no-op move onto
// itself).
class FileConflictCheck
{
public:

	struct Mapping
	{
		std::string strSrcURI;
		std::string strDstURI;
		std::string strContentType; // mime type of the source file

		Mapping() {}
		Mapping(const std::string& src, const std::string& dst)
			: strSrcURI(src), strDstURI(dst) {}
	};

	struct Result
	{
		std::string strSrcName;      // original file name
		std::string strDstName;      // destination file name
		std::string strConflictWith; // human readable description, empty if none
		std::string strTypeDescription; // e.g. "MP4 video"
		std::string strIconName;        // themed icon name, e.g. "video-x-generic"

		bool HasConflict() const { return !strConflictWith.empty(); }
	};

	typedef std::vector<Result> ResultList;

	typedef void (*ProgressFn)(double fraction, gpointer user_data);

	// fills vectResults (parallel to vectMappings) and returns true when
	// at least one conflict was found.  Safe to call from a worker thread;
	// honours pCancellable and reports progress through fnProgress.
	static bool Check(const std::vector<Mapping>& vectMappings,
			ResultList& vectResults,
			GCancellable* pCancellable = NULL,
			ProgressFn fnProgress = NULL,
			gpointer pUserData = NULL);

	// modal table listing the results: Original Name | New Name |
	// Conflicts With.  Conflicted rows are highlighted.
	static void ShowResultsDialog(GtkWindow* pParent,
			const ResultList& vectResults);
};

#endif // FILE_CONFLICT_CHECK_H
