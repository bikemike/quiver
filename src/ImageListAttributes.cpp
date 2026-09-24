#include "ImageListAttributes.h"

#include <list>
#include <algorithm>

ImageListAttributes::ImageListAttributes()
	: m_bRecursive(false)
{
}

void ImageListAttributes::AddFolder(const std::string& rFolder)
{
	for (std::list<std::string>::const_iterator itr = m_Folders.begin();
			m_Folders.end() != itr; ++itr)
	{
		if (*itr == rFolder)
		{
			return;
		}
	}
	m_Folders.push_back(rFolder);
}

bool ImageListAttributes::Matches(const std::list<std::string>& rFolders,
		bool bRecursive) const
{
	if (m_bRecursive != bRecursive)
	{
		return false;
	}
	if (m_Folders.size() != rFolders.size())
	{
		return false;
	}
	return std::equal(m_Folders.begin(), m_Folders.end(), rFolders.begin());
}