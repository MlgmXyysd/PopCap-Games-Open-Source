#pragma once
#ifndef __BROWSERLIB_H__
#define __BROWSERLIB_H__
//****************************************************************************
//**
//**  File     :  BROWSERLIB.H
//**  Summary  :  Header - BrowserLib - embedded web browser proxy library
//**
//----------------------------------------------------------------------------
//**       $Id:$ 
//** $DateTime:$ 
//**   $Author:$ 
//**   $Change:$ 
//****************************************************************************
//============================================================================
//    HEADERS
//============================================================================
#include <string>
#include <vector>

namespace BrowserLib {
//============================================================================
//    DEFINITIONS / ENUMERATIONS / SIMPLE TYPEDEFS
//============================================================================
#define BROWSERLIB_API_VERSION 2

#ifdef BROWSERLIB_EXPORTS
	#define BROWSERLIB_API __declspec(dllexport)
#else
	#define BROWSERLIB_API __declspec(dllimport)
#endif

//============================================================================
//    CLASSES / STRUCTURES
//============================================================================
struct SWebApiConfig;
class CWebVariant;
class IWebPerfApi;
class IWebApi;
class IWebView;
class IWebViewListener;

/*
	SWebApiConfig
*/
#pragma pack(push, 1)
struct SWebApiConfig
{
	enum // flags
	{
		WACF_Verbose		= (1 << 0),
		WACF_Wrapper		= (1 << 1)
	};

	unsigned short mApiVersion; // set to BROWSERLIB_API_VERSION
	unsigned short mReserved;
	unsigned long mFlags;
	const wchar_t* mCookieStoreFileName;
	const wchar_t* mCacheFilePath;
	IWebPerfApi* mPerfApi;

	inline SWebApiConfig(unsigned long inFlags = 0, const wchar_t* inCookieStoreFileName = NULL, const wchar_t* inCacheFilePath = NULL)
	: mApiVersion(BROWSERLIB_API_VERSION)
	, mReserved(0)
	, mFlags(inFlags)
	, mCookieStoreFileName(inCookieStoreFileName)
	, mCacheFilePath(inCacheFilePath)
	, mPerfApi(0)
	{}
};
#pragma pack(pop)

/*
	CWebVariant
*/
class CWebVariant
{
public:
	enum EVariantType
	{
		VT_NONE=0,

		VT_BOOL,
		VT_INT,
		VT_DOUBLE,
		VT_STRING
	};

	EVariantType mType;
	
	std::string mString;
	union
	{
		int mInt;
		double mDouble;
	};

	inline CWebVariant() : mType(VT_NONE) {}
	inline CWebVariant(bool inBool) : mType(VT_BOOL), mInt(inBool) {}
	inline CWebVariant(int inInt) : mType(VT_INT), mInt(inInt) {}
	inline CWebVariant(double inDouble) : mType(VT_DOUBLE), mDouble(inDouble) {}
	inline CWebVariant(const std::string& inString) : mType(VT_STRING), mString(inString) {}

	inline CWebVariant(const CWebVariant& v) : mType(v.mType), mString(v.mString), mDouble(v.mDouble) {}

	inline bool IsValid() const { return mType != VT_NONE; }

	inline bool IsBool() const { return mType == VT_BOOL; }
	inline bool IsInt() const { return mType == VT_INT; }
	inline bool IsDouble() const { return mType == VT_DOUBLE; }
	inline bool IsString() const { return mType == VT_STRING; }
	inline bool IsNumber() const { return (mType == VT_INT) || (mType == VT_DOUBLE); }

	inline bool GetBool() const { return mInt != 0; }
	inline int GetInt() const { return mInt; }
	inline double GetDouble() const { return mDouble; }
	inline const std::string& GetString() const { return mString; }
};

/*
	IWebPerfApi
*/
class IWebPerfApi
{
public:
	virtual void PerfStartTiming(const char* inName) = 0;
	virtual void PerfStopTiming(const char* inName) = 0;
};

/*
	IWebApi
*/
class IWebApi
{
public:
	virtual void				ApiDestroy() = 0;

	virtual void				ApiUpdate() = 0;
	virtual void				ApiPause() = 0;
	virtual void				ApiResume() = 0;

	virtual IWebView*			ApiCreateWebView(int inWidth, int inHeight, bool inIsTransparent = false, bool inEnableAsyncRendering = false, int inMaxAsyncRenderPerSec = 70) = 0;

	virtual void				ApiSetBaseDirectory(const std::string& inBaseDirectory) = 0;
	virtual std::string			ApiGetBaseDirectory() = 0;

	virtual void				ApiSetCustomResponsePage(int inStatusCode, const std::string& inFilePath) = 0;
};

/*
	IWebView
*/
class IWebView
{
public:
	enum ERenderMode
	{
		RENDERMODE_Default=0,
		RENDERMODE_OmgDirtyRect_Full,
		RENDERMODE_OmgDirtyRect_Frame,
	};

	struct RenderRect
	{
		int mX, mY;
		int mWidth, mHeight;

		inline RenderRect() : mX(0), mY(0), mWidth(0), mHeight(0) {}
		inline RenderRect(int inX, int inY, int inWidth, int inHeight) : mX(inX), mY(inY), mWidth(inWidth), mHeight(inHeight) {}
	};
	struct BackgroundInfo
	{
		enum
		{
			BIF_InjectBackgroundImage	= (1 << 0), // background image is not referenced by web page, and must be injected as a javascript command
		};

		void* mBits;
		int mWidth, mHeight;
		int mPitchBytes;
		int mBitsPerPixel;
		const char* mBgFileUrl;
		const char* mBgSubStr;
		unsigned int mFlags; // BIF_ flags
	};

	virtual void				ViewDestroy() = 0;

	virtual void				ViewSetListener(IWebViewListener* inListener) = 0;
	virtual IWebViewListener*   ViewGetListener() = 0;

	virtual void				ViewLoadURL(const std::string& inUrl, const std::wstring& inFrameName = L"", const std::string& inUsername = "", const std::string& inPassword = "",
											const std::string& inPostData = "", const std::string& inPostSeparator = "") = 0;
	virtual void				ViewLoadHTMLString(const std::string& inHtml, const std::wstring& inFrameName = L"") = 0;
	virtual void				ViewLoadFile(const std::string& inFileName, const std::wstring& inFrameName = L"") = 0;

	virtual void				ViewGoToHistoryOffset(int inOffset) = 0;
	virtual void				ViewExecuteJavascript(const std::string& inJavascript, const std::wstring& inFrameName = L"") = 0;
	virtual void				ViewSetProperty(const std::string& inPropertyName, const CWebVariant& inValue) = 0;
	virtual void				ViewRegisterCallback(const std::string& inCallbackName) = 0;

	virtual bool				ViewIsDirty() = 0;
	virtual void				ViewRender(unsigned char* inDestPtr, int inDestPitchBytes, int inDestPixelDepth,
									       RenderRect* outRenderedRect = 0, BackgroundInfo* inBgInfo = 0, RenderRect* inOpaqueRect = 0) = 0;
	virtual void				ViewSetTransparent(bool inIsTransparent) = 0;
	virtual void				ViewSetRenderMode(ERenderMode inRenderMode) = 0;

	virtual void				ViewInjectMouseMove(int inX, int inY) = 0;
	virtual void				ViewInjectMouseDown(int inButton) = 0;
	virtual void				ViewInjectMouseUp(int inButton) = 0;
	virtual void				ViewInjectMouseWheel(int inScrollAmount) = 0;

	virtual void				ViewInjectKeyboardEvent(void* inHwnd, unsigned int inMessage, unsigned __int64 inWParam, __int64 inLParam) = 0;

	virtual	void				ViewClipboardCut() = 0;
	virtual void				ViewClipboardCopy() = 0;
	virtual void				ViewClipboardPaste() = 0;
	virtual void				ViewEditSelectAll() = 0;
	virtual void				ViewEditDeselectAll() = 0;

	virtual void				ViewGetContentAsText(wchar_t* outBuffer, int inMaxChars) = 0;

	virtual void				ViewZoomIn() = 0;
	virtual void				ViewZoomOut() = 0;
	virtual void				ViewZoomReset() = 0;

	virtual void				ViewResize(int inWidth, int inHeight) = 0;

	virtual void				ViewSetFocus() = 0;
	virtual void				ViewKillFocus() = 0;
};

/*
	IWebViewListener
*/
class IWebViewListener
{
public:
	virtual bool			ListenerAllowNavigation(const std::string& inUrl, const std::wstring& inFrameName) { return true; }
	virtual void			ListenerBeginNavigation(const std::string& inUrl, const std::wstring& inFrameName) {}
	virtual void			ListenerBeginLoading(const std::string& inUrl, const std::wstring& inFrameName, int inStatusCode, const std::wstring& inMimeType) {}
	virtual void			ListenerFinishLoading() {}
	virtual void			ListenerClientCallback(const std::string& inCallbackName, unsigned int inArgCount, const CWebVariant* inArgs) {}
	virtual void			ListenerReceiveTitle(const std::wstring& inTitle, const std::wstring& inFrameName) {}
	virtual void			ListenerChangeTooltip(const std::wstring& inTooltip) {}
	virtual void			ListenerChangeCursor(const void* inCursorHandle) {}
	virtual void			ListenerChangeKeyboardFocus(bool inIsFocused) {}
	virtual void			ListenerChangeTargetURL(const std::string& inUrl) {}
};

//============================================================================
//    GLOBAL DATA
//============================================================================
//============================================================================
//    GLOBAL FUNCTIONS
//============================================================================
extern "C" {
typedef IWebApi* (__cdecl *F_BL_CreateWebApi)(const SWebApiConfig* inConfig);
BROWSERLIB_API IWebApi* __cdecl BL_CreateWebApi(const SWebApiConfig* inConfig);
}
//============================================================================
//    INLINE CLASS METHODS
//============================================================================
//============================================================================
//    TRAILING HEADERS
//============================================================================

} // namespace BrowserLib
//****************************************************************************
//**
//**    END HEADER BROWSERLIB.H
//**
//****************************************************************************
#endif // __BROWSERLIB_H__
