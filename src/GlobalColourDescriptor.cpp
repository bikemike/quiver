#include <config.h>
#include "GlobalColourDescriptor.h"

#include <math.h>
#include <iostream>
#include <fstream>
#include <gdk-pixbuf/gdk-pixbuf.h>

using namespace std;

GlobalColourDescriptor::GlobalColourDescriptor(QuiverFile *pImage)
{
	m_pImage = pImage;
	m_pR = NULL;
	m_pG = NULL;
	m_pB = NULL;
}

GlobalColourDescriptor::~GlobalColourDescriptor()
{
}

void GlobalColourDescriptor::SetImage(QuiverFile *m_pImage)
{
	m_pImage = m_pImage;
}

QuiverFile* GlobalColourDescriptor::GetImage()
{
	return m_pImage;
}

int GlobalColourDescriptor::Calculate(int nBins, int nSize)
{
	m_nBins = nBins;
	
	if(m_pR)
		delete m_pR;
	if(m_pG)
		delete m_pG;
	if(m_pB)
		delete m_pB;
		
	m_pR = new int[nBins];
	m_pG = new int[nBins];
	m_pB = new int[nBins];
	
	memset(m_pR, 0, sizeof(int)*nBins);
	memset(m_pG, 0, sizeof(int)*nBins);
	memset(m_pB, 0, sizeof(int)*nBins);

	GdkPixbuf *buf = m_pImage->GetThumbnail(nSize);
	if(NULL == buf)
	{
		return -1;
	}
	guchar *pixels = gdk_pixbuf_get_pixels(buf);
	int n_channels = gdk_pixbuf_get_n_channels(buf);
	int w = gdk_pixbuf_get_width(buf);
	int h = gdk_pixbuf_get_height(buf);
	int rs = gdk_pixbuf_get_rowstride(buf);

	cout << n_channels << " channels" << endl;
	
	g_assert(n_channels >= 3); // or else?
	
	for(int i=0; i<w*h*n_channels; i+=n_channels)
	{
		int val = pixels[i];
		guchar r,g,b;
		r = pixels[i]/(256/nBins); // Assume 8 bits per sample
		g = pixels[i+1]/(256/nBins);
		b = pixels[i+2]/(256/nBins);
		m_pR[ r ]++;
		m_pG[ g ]++;
		m_pB[ b ]++;
	}

//	cout << "RED" << endl;	
//	for(int i=0; i<nBins; i++)
//	{
//		cout << m_pR[i] << " ";
//	}
//	cout << endl;
//	
//	cout << "GREEN" << endl;	
//	for(int i=0; i<nBins; i++)
//	{
//		cout << m_pG[i] << " ";
//	}
//	cout << endl;
//	
//	cout << "BLUE" << endl;	
//	for(int i=0; i<nBins; i++)
//	{
//		cout << m_pB[i] << " ";
//	}
//	cout << endl;
}

int **GlobalColourDescriptor::GetHistogram(int *nBins)
{
	*nBins = m_nBins;
	
	int **ppRGB = new int*[3];
	ppRGB[0] = m_pR;
	ppRGB[1] = m_pG;
	ppRGB[2] = m_pB;
	//new int[3] = {m_pR, m_pG, m_pB};//
	return ppRGB;
}

int GlobalColourDescriptor::GetPackedHistogram(int **blob, int *nBins)
{
	*nBins = m_nBins;
	
	int pos = 0;
	*blob = new int[m_nBins*3 + 1];
	
	(*blob)[pos] = m_nBins;
	pos ++;
	for(int i=0; i<m_nBins; i++)
	{
		(*blob)[pos] = m_pR[i];
		pos ++;
		(*blob)[pos] = m_pG[i];
		pos ++;
		(*blob)[pos] = m_pB[i];
		pos ++;
	}
	return pos;
}

int GlobalColourDescriptor::LoadHistogram(int **data, int nBins)
{
	m_pR = new int[nBins];
	m_pG = new int[nBins];
	m_pB = new int[nBins];

	for(int i=0; i<nBins; i++)
	{
		m_pR[i] = data[0][i];
		m_pG[i] = data[1][i];
		m_pB[i] = data[2][i];
	}
	
	return 0;
}

double GlobalColourDescriptor::operator- (GlobalColourDescriptor &other) const
{
	// Calculate the Euclidean distance between two histograms
	int nOtherBins=0;
	int **p = other.GetHistogram(&nOtherBins);
	
	if(nOtherBins != m_nBins)
	{
		delete p;
		return -1;
	}
	
	int sum=0;
	for(int i=0; i<m_nBins; i++)
	{
		sum += (m_pR[i] - p[0][i])*(m_pR[i] - p[0][i]);
		sum += (m_pG[i] - p[1][i])*(m_pG[i] - p[1][i]);
		sum += (m_pB[i] - p[2][i])*(m_pB[i] - p[2][i]);
	}
	
	delete p;

	return sqrt(sum);
}
