#ifndef FILE_EXTERNAL_TOOLS_H
#define FILE_EXTERNAL_TOOLS_H
#include <vector>
#include <list>
#include <map>
#include <string>
#include <boost/shared_ptr.hpp>

#include "ExternalToolsEventSource.h"


class ExternalTools;
typedef boost::shared_ptr<ExternalTools> ExternalToolsPtr;

class ExternalTool
{
public:
	ExternalTool(){};
	ExternalTool(std::string name, std::string tooltip, std::string icon,
		std::string cmd, bool bSupportsMult, bool bShowOutput, bool bShowErrors) :
		m_strName(name), m_strTooltip(tooltip), m_strCmd(cmd), m_strIcon(icon),
		m_bSupportsMultiple(bSupportsMult), m_bShowOutput(bShowOutput), m_bShowErrors(bShowErrors)
		{};

	~ExternalTool(){};

	std::string GetName() const {return m_strName;}
	void        SetName(std::string name){m_strName = name;}
	
	std::string GetCategory() const {return m_strCategory;}
	void        SetCategory(std::string cat){m_strCategory = cat;}

	std::string GetTooltip() const{return m_strTooltip;}
	void        SetTooltip(std::string tooltip){m_strTooltip = tooltip;}

	std::string GetCmd() const{return m_strCmd;}
	void        SetCmd(std::string cmd) { m_strCmd = cmd;}

	std::string GetIcon() const{return m_strIcon;}
	void        SetIcon(std::string icon) { m_strIcon = icon;}

	bool        GetSupportsMultiple() const{return m_bSupportsMultiple;}
	void        SetSupportsMultiple(bool bSupportsMultiple){m_bSupportsMultiple = bSupportsMultiple;}

	bool        GetShowOutput() const{return m_bShowOutput;}
	void        SetShowOutput(bool bShowOutput){m_bShowOutput = bShowOutput;}

	bool        GetShowErrors() const{return m_bShowErrors;}
	void        SetShowErrors(bool bShowErrors){m_bShowErrors = bShowErrors;}

	int         GetID() const{return m_iID;}
private:
	void        SetID(int id){m_iID = id;};
	std::string m_strName;	
	std::string m_strCategory;	
	std::string m_strTooltip;	
	std::string m_strCmd;	
	std::string m_strIcon;
	bool m_bSupportsMultiple;
	bool m_bShowOutput;
	bool m_bShowErrors;
	int m_iID;
	// friend for SetID function
	friend class ExternalTools;
};

class ExternalTools : public ExternalToolsEventSource
{
public:
	~ExternalTools(){ SaveToPreferences(); };

	static ExternalToolsPtr
	               GetInstance();

	bool           AddExternalTool(ExternalTool eternal_tool);
	bool           AddExternalTool(ExternalTool eternal_tool, std::string category );

	bool           UpdateExternalTool(ExternalTool eternal_tool);
	bool           MoveUp (int id);
	bool           MoveDown (int id);

	int            GetUniqueID() const;

	const ExternalTool*
	               GetExternalTool(int id); 

	bool           Remove(int id);

	std::vector<ExternalTool> 
	               GetExternalTools(); 

	std::vector<ExternalTool>
	               GetExternalTools(std::string category);

	class ExternalToolsImpl;
	typedef boost::shared_ptr<ExternalToolsImpl> ExternalToolsImplPtr;

private:
	ExternalTools();

	void           LoadFromPreferences();
	void           SaveToPreferences();

	typedef std::map<int, ExternalTool > ExternalToolMap;
	ExternalToolMap m_mapExternalTools;

	static ExternalToolsPtr c_ExternalToolsPtr;
	ExternalToolsImplPtr m_ExternalToolsImplPtr;
};


#endif // FILE_EXTERNAL_TOOLS_H

