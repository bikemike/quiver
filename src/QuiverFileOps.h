#ifndef FILE_QUIVER_FILE_OPS_H
#define FILE_QUIVER_FILE_OPS_H

#include "QuiverFile.h"

namespace QuiverFileOps
{
	bool Delete(QuiverFile quiverFile);
	bool MoveToTrash(QuiverFile quiverFile);
	/* FIXME: implement move, copy */
}

#endif

