#ifndef LOCALCOLOURDESCRIPTOR_H_
#define LOCALCOLOURDESCRIPTOR_H_

#include <string>
#include <list>
#include "QuiverFile.h"

using namespace std;

class LocalColourDescriptor
{
public:
	LocalColourDescriptor();
	LocalColourDescriptor(QuiverFile *pImage);
	~LocalColourDescriptor();
private:
	QuiverFile *m_pImage;
	
	int m_nBinsPerRegion;
	int m_nRegions;
	list <list <int> > m_R;
	list <list <int> > m_G;
	list <list <int> > m_B;
};

#endif /*LOCALCOLOURDESCRIPTOR_H_*/
