#ifndef FILE_MESSAGE_BOX_H
#define FILE_MESSAGE_BOX_H

#include <string>

class MessageBox
{
public:
	typedef enum {
		ICON_TYPE_INFO,
		ICON_TYPE_QUESTION,
		ICON_TYPE_WARNING,
		ICON_TYPE_ERROR,
	} IconType;

	typedef enum {
		BUTTON_TYPE_NONE,
		BUTTON_TYPE_OK,
		BUTTON_TYPE_CLOSE,
		BUTTON_TYPE_CANCEL,
		BUTTON_TYPE_YES_NO,
		BUTTON_TYPE_OK_CANCEL,
	} ButtonType;

	typedef enum {
		RESPONSE_TYPE_NONE,
		RESPONSE_TYPE_OK,
		RESPONSE_TYPE_CANCEL,
		RESPONSE_TYPE_CLOSE,
		RESPONSE_TYPE_YES,
		RESPONSE_TYPE_NO,
		RESPONSE_TYPE_CUSTOM1,
		RESPONSE_TYPE_CUSTOM2,
		RESPONSE_TYPE_CUSTOM3,
		RESPONSE_TYPE_CUSTOM4,
		RESPONSE_TYPE_CUSTOM5,
	} ResponseType;

	typedef enum {
		BUTTON_ICON_NONE,
		BUTTON_ICON_ADD,
		BUTTON_ICON_APPLY,
		BUTTON_ICON_CANCEL,
		BUTTON_ICON_CLEAR,
		BUTTON_ICON_CLOSE,
		BUTTON_ICON_CONNECT,
		BUTTON_ICON_DELETE,
		BUTTON_ICON_DIRECTORY,
		BUTTON_ICON_DISCONNECT,
		BUTTON_ICON_EDIT,
		BUTTON_ICON_EXECUTE,
		BUTTON_ICON_INFO,
		BUTTON_ICON_NO,
		BUTTON_ICON_OK,
		BUTTON_ICON_REDO,
		BUTTON_ICON_REFRESH,
		BUTTON_ICON_REMOVE,
		BUTTON_ICON_SAVE,
		BUTTON_ICON_SAVE_AS,
		BUTTON_ICON_STOP,
		BUTTON_ICON_UNDO,
		BUTTON_ICON_YES,
	} BUTTON_ICON;

	                     MessageBox(IconType iconType, 
	                         ButtonType buttonType, 
	                         std::string msg,
	                         std::string details);

	                     ~MessageBox();

	void                 AddButton(BUTTON_ICON icon, const std::string &text, 
	                         ResponseType resp_type);

	void                 AddButton(const std::string &text, 
	                         ResponseType resp_type);

	void                 SetDefaultResponseType(ResponseType respType);

	// blocks until the user reponds
	ResponseType         Run();
	static ResponseType  Run(IconType iconType, ButtonType buttonType, std::string msg, std::string details);

	class PrivateImpl;

private:
	PrivateImpl* m_pPrivateImpl;
};

#endif // FILE_MESSAGE_BOX_H


