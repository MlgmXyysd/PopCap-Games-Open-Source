//****************************************************************************
//**
//**  File     :  BROWSERLIB.CPP
//**  Summary  :  BrowserLib - embedded web browser proxy library
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
#include "..\Include\BrowserLib.h"

#include "Awesomium\Include\WebCore.h"
#ifdef _DEBUG
	#pragma comment(lib, "Src\\Awesomium\\Lib\\Awesomium_d.lib")
#else
	#pragma comment(lib, "Src\\Awesomium\\Lib\\Awesomium.lib")
#endif

#define DBG_ASSERTSTATIC(xExpr) typedef int $_StaticAssert_$_If_You_Get_An_Error_On_This_Line_Then_A_Static_Assert_Failed_$[!!(xExpr)]
DBG_ASSERTSTATIC(1);

namespace BrowserLib {

DBG_ASSERTSTATIC(sizeof(IWebView::BackgroundInfo) == sizeof(Awesomium::WebView::BackgroundInfo));
DBG_ASSERTSTATIC(sizeof(IWebView::RenderRect) == sizeof(Awesomium::Rect));

using namespace Awesomium;
//============================================================================
//    DEFINITIONS / ENUMERATIONS / SIMPLE TYPEDEFS
//============================================================================
//============================================================================
//    CLASSES / STRUCTURES
//============================================================================
/*
	CWebPerfApi
*/
class CWebPerfApi
: public Awesomium::WebPerfApi
{
public:
	IWebPerfApi* mPerfApi;

	CWebPerfApi(IWebPerfApi* inPerfApi = 0)
	: mPerfApi(inPerfApi)
	{}

	virtual void onPerfStart(const char* inName) override
	{
		if (mPerfApi)
			mPerfApi->PerfStartTiming(inName);
	}
	virtual void onPerfStop(const char* inName) override
	{
		if (mPerfApi)
			mPerfApi->PerfStopTiming(inName);
	}
};

/*
	CWebViewListenerProxy
*/
class CWebViewListenerProxy
: public Awesomium::WebViewListener
{
public:
	IWebViewListener* mListener;

	CWebViewListenerProxy(IWebViewListener* inListener)
	: mListener(inListener)
	{}

	virtual bool onAllowNavigation(const std::string& url, const std::wstring& frameName) override
	{
		return mListener->ListenerAllowNavigation(url, frameName);
	}
	virtual void onBeginNavigation(const std::string& url, const std::wstring& frameName) override
	{
		mListener->ListenerBeginNavigation(url, frameName);
	}
	virtual void onBeginLoading(const std::string& url, const std::wstring& frameName, int statusCode, const std::wstring& mimeType) override
	{
		mListener->ListenerBeginLoading(url, frameName, statusCode, mimeType);
	}
	virtual void onFinishLoading() override
	{
		mListener->ListenerFinishLoading();
	}
	virtual void onCallback(const std::string& name, unsigned int argCount, const Awesomium::JSValue* args) override
	{
		std::vector<CWebVariant> v;
		for (unsigned int iArg=0; iArg<argCount; ++iArg)
		{
			const Awesomium::JSValue* it = &args[iArg];

			if (it->isBoolean())
				v.push_back(CWebVariant(it->toBoolean()));
			else if (it->isInteger())
				v.push_back(CWebVariant(it->toInteger()));
			else if (it->isDouble())
				v.push_back(CWebVariant(it->toDouble()));
			else if (it->isString())
				v.push_back(CWebVariant(it->toString()));
			else
				v.push_back(CWebVariant());
		}

		if (v.empty())
			mListener->ListenerClientCallback(name, 0, NULL);
		else
			mListener->ListenerClientCallback(name, (int)v.size(), &v[0]);
	}
	virtual void onReceiveTitle(const std::wstring& title, const std::wstring& frameName) override
	{
		mListener->ListenerReceiveTitle(title, frameName);
	}
	virtual void onChangeTooltip(const std::wstring& tooltip) override
	{
		mListener->ListenerChangeTooltip(tooltip);
	}
	virtual void onChangeCursor(const HCURSOR& cursor) override
	{
		mListener->ListenerChangeCursor(cursor);
	}
	virtual void onChangeKeyboardFocus(bool isFocused) override
	{
		mListener->ListenerChangeKeyboardFocus(isFocused);
	}
	virtual void onChangeTargetURL(const std::string& url) override
	{
		mListener->ListenerChangeTargetURL(url);
	}
};

/*
	CWebView
*/
class CWebView
: public IWebView
{
public:
	WebView* mWebView;
	CWebViewListenerProxy* mListenerProxy;

	CWebView(WebView* inWebView)
	: mWebView(inWebView)
	, mListenerProxy(NULL)
	{}
	~CWebView()
	{
		ViewSetListener(NULL);
		mWebView->destroy();
	}

	void ViewDestroy()
	{
		delete this;
	}

	void ViewSetListener(IWebViewListener* inListener)
	{
		if (inListener)
		{
			if (mListenerProxy)
				mListenerProxy->mListener = inListener;
			else
				mListenerProxy = new CWebViewListenerProxy(inListener);

			mWebView->setListener(mListenerProxy);
		}
		else
		{
			if (mListenerProxy)
			{
				delete mListenerProxy;
				mListenerProxy = NULL;
			}

			mWebView->setListener(NULL);
		}
	}
	IWebViewListener* ViewGetListener()
	{
		return mListenerProxy ? mListenerProxy->mListener : NULL;
	}

	void ViewLoadURL(const std::string& inUrl, const std::wstring& inFrameName = L"", const std::string& inUsername = "", const std::string& inPassword = "",
					 const std::string& inPostData = "", const std::string& inPostSeparator = "")
	{
		mWebView->loadURL(inUrl, inFrameName, inUsername, inPassword, inPostData, inPostSeparator);
	}
	void ViewLoadHTMLString(const std::string& inHtml, const std::wstring& inFrameName = L"")
	{
		mWebView->loadHTML(inHtml, inFrameName);
	}
	void ViewLoadFile(const std::string& inFileName, const std::wstring& inFrameName = L"")
	{
		mWebView->loadFile(inFileName, inFrameName);
	}

	void ViewGoToHistoryOffset(int inOffset)
	{
		mWebView->goToHistoryOffset(inOffset);
	}
	void ViewExecuteJavascript(const std::string& inJavascript, const std::wstring& inFrameName = L"")
	{
		mWebView->executeJavascript(inJavascript, inFrameName);
	}
	void ViewSetProperty(const std::string& inPropertyName, const CWebVariant& inValue)
	{
		if (inValue.IsBool())
			mWebView->setProperty(inPropertyName, inValue.GetBool());
		else if (inValue.IsInt())
			mWebView->setProperty(inPropertyName, inValue.GetInt());
		else if (inValue.IsDouble())
			mWebView->setProperty(inPropertyName, inValue.GetDouble());
		else if (inValue.IsString())
			mWebView->setProperty(inPropertyName, inValue.GetString());
	}
	void ViewRegisterCallback(const std::string& inCallbackName)
	{
		mWebView->setCallback(inCallbackName);
	}

	bool ViewIsDirty()
	{
		return mWebView->isDirty();
	}
	void ViewRender(unsigned char* inDestPtr, int inDestPitchBytes, int inDestPixelDepth, RenderRect* outRenderedRect = 0, BackgroundInfo* inBgInfo = 0, RenderRect* inOpaqueRect = 0)
	{
		mWebView->render(inDestPtr, inDestPitchBytes, inDestPixelDepth, (Awesomium::Rect*)outRenderedRect, (Awesomium::WebView::BackgroundInfo*)inBgInfo, (Awesomium::Rect*)inOpaqueRect);
	}
	void ViewSetTransparent(bool inIsTransparent)
	{
		mWebView->setTransparent(inIsTransparent);
	}
	void ViewSetRenderMode(ERenderMode inRenderMode)
	{
		mWebView->setRenderMode((Awesomium::WebView::ERenderMode)inRenderMode);
	}

	void ViewInjectMouseMove(int inX, int inY)
	{
		mWebView->injectMouseMove(inX, inY);
	}
	void ViewInjectMouseDown(int inButton)
	{
		switch (inButton)
		{
		case 0: mWebView->injectMouseDown(LEFT_MOUSE_BTN); break;
		case 1: mWebView->injectMouseDown(RIGHT_MOUSE_BTN); break;
		case 2: mWebView->injectMouseDown(MIDDLE_MOUSE_BTN); break;
		default: break;
		}
	}
	void ViewInjectMouseUp(int inButton)
	{
		switch (inButton)
		{
		case 0: mWebView->injectMouseUp(LEFT_MOUSE_BTN); break;
		case 1: mWebView->injectMouseUp(RIGHT_MOUSE_BTN); break;
		case 2: mWebView->injectMouseUp(MIDDLE_MOUSE_BTN); break;
		default: break;
		}
	}
	void ViewInjectMouseWheel(int inScrollAmount)
	{
		mWebView->injectMouseWheel(inScrollAmount);
	}

	void ViewInjectKeyboardEvent(void* inHwnd, unsigned int inMessage, unsigned __int64 inWParam, __int64 inLParam)
	{
		mWebView->injectKeyboardEvent((HWND)inHwnd, inMessage, (WPARAM)inWParam, (LPARAM)inLParam);
	}

	void ViewClipboardCut()
	{
		mWebView->cut();
	}
	void ViewClipboardCopy()
	{
		mWebView->copy();
	}
	void ViewClipboardPaste()
	{
		mWebView->paste();
	}
	void ViewEditSelectAll()
	{
		mWebView->selectAll();
	}
	void ViewEditDeselectAll()
	{
		mWebView->deselectAll();
	}

	void ViewGetContentAsText(wchar_t* outBuffer, int inMaxChars)
	{
		std::wstring s;
		s.reserve(inMaxChars+1);
		mWebView->getContentAsText(s, inMaxChars);
		wcscpy_s(outBuffer, inMaxChars, s.c_str());
	}

	void ViewZoomIn()
	{
		mWebView->zoomIn();
	}
	void ViewZoomOut()
	{
		mWebView->zoomOut();
	}
	void ViewZoomReset()
	{
		mWebView->resetZoom();
	}

	void ViewResize(int inWidth, int inHeight)
	{
		mWebView->resize(inWidth, inHeight);
	}

	void ViewSetFocus()
	{
		mWebView->focus();
	}
	void ViewKillFocus()
	{
		mWebView->unfocus();
	}
};

/*
	CWebApi
*/
class CWebApi
: public IWebApi
{
public:
	WebCore* mWebCore;

	CWebApi(WebCore* inWebCore)
	: mWebCore(inWebCore)
	{
	}
	~CWebApi()
	{
		// CDH FIXME$$$$ temporarily commented out WebCore cleanup due to crash on exit (known Awesomium bug, fixed in SVN already so should be okay in next release)
		/*
		if (mWebCore)
			delete mWebCore;
		*/
	}

	void ApiDestroy() { delete this; }

	void ApiUpdate() { mWebCore->update(); }
	void ApiPause() { mWebCore->pause(); }
	void ApiResume() { mWebCore->resume(); }

	IWebView* ApiCreateWebView(int inWidth, int inHeight, bool inIsTransparent = false, bool inEnableAsyncRendering = false, int inMaxAsyncRenderPerSec = 70)
	{
		WebView* view = mWebCore->createWebView(inWidth, inHeight, inIsTransparent, inEnableAsyncRendering, inMaxAsyncRenderPerSec);
		if (!view)
			return NULL;
		
		return new CWebView(view);
	}

	void ApiSetBaseDirectory(const std::string& inBaseDirectory) { return mWebCore->setBaseDirectory(inBaseDirectory); }
	std::string	ApiGetBaseDirectory() { return mWebCore->getBaseDirectory(); }

	void ApiSetCustomResponsePage(int inStatusCode, const std::string& inFilePath) { mWebCore->setCustomResponsePage(inStatusCode, inFilePath); }
};

//============================================================================
//    PRIVATE DATA
//============================================================================
//============================================================================
//    GLOBAL DATA
//============================================================================
//============================================================================
//    PRIVATE FUNCTIONS
//============================================================================
//============================================================================
//    GLOBAL FUNCTIONS
//============================================================================
extern "C" {

BROWSERLIB_API IWebApi* __cdecl BL_CreateWebApi(const SWebApiConfig* inConfig)
{
	if (!inConfig)
		return NULL;
	if (inConfig->mApiVersion != BROWSERLIB_API_VERSION)
		return NULL;

	std::wstring cookieStoreFile = inConfig->mCookieStoreFileName ? inConfig->mCookieStoreFileName : L"";
	std::wstring cacheFilePath = inConfig->mCacheFilePath ? inConfig->mCacheFilePath : L"";

	/* CDH FIXME$$$$ WebCore expects to be a singleton, but because of shutdown issue, we can only do construction once.  Added reInit to work around cookie/cache implications of this.
	WebCore* core = new WebCore((inConfig->mFlags & SWebApiConfig::WACF_Verbose) ? LOG_VERBOSE : LOG_NORMAL, true, PF_BGRA, cookieStoreFile, cacheFilePath);
	return new CWebApi(core);
	*/
	static CWebPerfApi sPerfApi;
	sPerfApi.mPerfApi = inConfig->mPerfApi;

	static WebCore* sCore = NULL;
	if (!sCore)
		sCore = new WebCore((inConfig->mFlags & SWebApiConfig::WACF_Verbose) ? LOG_VERBOSE : LOG_NORMAL, true, PF_BGRA, cookieStoreFile, cacheFilePath, &sPerfApi);
	else
		sCore->reInit(cookieStoreFile, cacheFilePath);
	return new CWebApi(sCore);
}

}
//============================================================================
//    CLASS METHODS
//============================================================================

} // namespace BrowserLib
//****************************************************************************
//**
//**    END MODULE BROWSERLIB.CPP
//**
//****************************************************************************

