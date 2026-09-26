#ifndef FILE_IMAGELIST_ATTRIBUTES_H
#define FILE_IMAGELIST_ATTRIBUTES_H

#include <string>
#include <list>
#include <set>
#include <boost/shared_ptr.hpp>

/* The definition of an image list: the set of root folders (or files) it was
 * built from, plus the recursive flag.  It intentionally holds no items —
 * the populated QuiverFileList lives in ImageList — so a lightweight instance
 * can be shared (via ImageListAttributesPtr) by every consumer that refers to
 * "this particular list view", e.g. the recently-viewed items list. */

class ImageListAttributes
{
public:
	ImageListAttributes();

	const std::list<std::string>& GetFolders() const { return m_Folders; }
	void SetFolders(const std::list<std::string>& rFolders) { m_Folders = rFolders; }
	void AddFolder(const std::string& rFolder);

	bool GetRecursive() const { return m_bRecursive; }
	void SetRecursive(bool bRecursive) { m_bRecursive = bRecursive; }

	const std::set<std::string>& GetRecursiveFolders() const { return m_RecursiveFolders; }
	void SetRecursiveFolders(const std::set<std::string>& folders) { m_RecursiveFolders = folders; }

	bool IsEmpty() const { return m_Folders.empty(); }

	/* True when this definition matches the other's folder set and recursive
	 * flag (a shallow "same view" comparison, regardless of object identity). */
	bool Matches(const std::list<std::string>& rFolders, bool bRecursive,
	             const std::set<std::string>& rRecursiveFolders) const;
	bool Matches(const std::list<std::string>& rFolders, bool bRecursive) const;
	bool Matches(const ImageListAttributes& rOther) const
	{
		return Matches(rOther.m_Folders, rOther.m_bRecursive, rOther.m_RecursiveFolders);
	}

private:
	std::list<std::string> m_Folders;
	bool m_bRecursive;
	std::set<std::string> m_RecursiveFolders;
};

typedef boost::shared_ptr<ImageListAttributes> ImageListAttributesPtr;

#endif