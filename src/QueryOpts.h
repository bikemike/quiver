#ifndef QUERYOPTS_H
#define QUERYOPTS_H

#include <string>
#include <list>
#include <boost/shared_ptr.hpp>

#include "QueryOptsEventSource.h"

typedef struct _GtkWidget GtkWidget;
typedef struct _GtkUIManager GtkUIManager;

class QueryOpts : public virtual QueryOptsEventSource
{

public:
	QueryOpts();
	~QueryOpts();

	GtkWidget* GetWidget() const;

	void SetUIManager(GtkUIManager *ui_manager);

	class QueryOptsImpl;
	typedef boost::shared_ptr<QueryOptsImpl> QueryOptsImplPtr;

private:
	QueryOptsImplPtr m_QueryOptsImplPtr;
};


#endif
