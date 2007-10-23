#ifndef IMAGEDESCRIPTOR_H_
#define IMAGEDESCRIPTOR_H_

#include <string>

#include "QuiverFile.h"

class ImageDescriptor
{
public:
	ImageDescriptor();
	ImageDescriptor(QuiverFile *m_pImage)
	{
		m_pImage = m_pImage;
	}
	virtual ~ImageDescriptor();
	
	void SetImage(QuiverFile *m_pImage)
	{
		m_pImage = m_pImage;
	}

	QuiverFile* GetImage()
	{
		return m_pImage;
	}
	
	virtual int ExtractDescriptor(unsigned char **ppBytes, unsigned char *pSize);
	
protected:
	QuiverFile *m_pImage;
};

#endif /*IMAGEDESCRIPTOR_H_*/
