#include <config.h>
#include "LocalColourDescriptor.h"

LocalColourDescriptor::LocalColourDescriptor()
{
}

LocalColourDescriptor::LocalColourDescriptor(QuiverFile *pImage)
{
	m_pImage = pImage;
}

LocalColourDescriptor::~LocalColourDescriptor()
{
}