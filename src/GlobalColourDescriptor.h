#ifndef GLOBALCOLOURDESCRIPTOR_H_
#define GLOBALCOLOURDESCRIPTOR_H_

//#include "ImageDescriptor.h"
#include <string>
#include "QuiverFile.h"

class GlobalColourDescriptor
{
public:
	GlobalColourDescriptor();
	GlobalColourDescriptor(QuiverFile *pImage);
	~GlobalColourDescriptor();
	
	void SetImage(QuiverFile *m_pImage);
	QuiverFile* GetImage();
	
	int Calculate(int nBins, int nSize);
	
	int **GetHistogram(int *nBins);
	int GetPackedHistogram(int **blob, int *nBins);
	int LoadHistogram(int **data, int nBins);
	
	double operator- (GlobalColourDescriptor &other) const;
protected:
	QuiverFile *m_pImage;
	int m_nBins;
	int *m_pR;
	int *m_pG;
	int *m_pB;
};


#endif /*GLOBALCOLOURDESCRIPTOR_H_*/
