/*
	This file is a part of Awesomium, a library that makes it easy for 
	developers to embed web-content in their applications.

	Copyright (C) 2009 Adam J. Simmons

	Project Website:
	<http://princeofcode.com/awesomium.php>

	This library is free software; you can redistribute it and/or
	modify it under the terms of the GNU Lesser General Public
	License as published by the Free Software Foundation; either
	version 2.1 of the License, or (at your option) any later version.

	This library is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
	Lesser General Public License for more details.

	You should have received a copy of the GNU Lesser General Public
	License along with this library; if not, write to the Free Software
	Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 
	02110-1301 USA
*/
/*
	Modified by PopCap Games, Inc. for the version included with the
	BrowserLib library.  Change history:

	2009.07.21: Added POST support
	2009.07.29: Added BackgroundInfo support, as a transparency improvement
	2009.10.27: Added OpaqueRect support, as a transparency improvement
	2009.11.10: Added RenderMode support for debugging
	2010.01.14: Added onAllowNavigation for rudimentary white/blacklisting
*/

#include "WebViewProxy.h"
#include "WebCore.h"
#include "WebCoreProxy.h"
#include "WindowlessPlugin.h"
#include "WebViewEvent.h"
#include "NavigationController.h"
#include "base/path_service.h"
#include "base/string_util.h"
#include "webkit/glue/plugins/plugin_list.h"
#include "webkit/glue/webdatasource.h"
#include "webkit/glue/webresponse.h"
#include "net/base/base64.h"
#include "net/base/upload_data.h"
#include <assert.h>

#define uint32_t unsigned long
#define int32_t signed long
#include <stdint.h>
#undef LOG
#define WTF_PLATFORM_CHROMIUM 1
#include "graphics/bitmapimage.h"
#include "graphics/skia/nativeimageskia.h"
#undef LOG
#define LOG(severity) COMPACT_GOOGLE_LOG_ ## severity.stream()

extern std::string g_PopCap_Chromium_BackgroundSubStr;
extern std::vector<WebCore::BitmapImage*> g_PopCap_Chromium_BackgroundImages;
extern std::vector<std::wstring> g_PopCap_Chromium_BackgroundImageUrls;

WebViewProxy::WebViewProxy(int width, int height, bool isTransparent, bool enableAsyncRendering, int maxAsyncRenderPerSec, Awesomium::WebView* parent)
: view(0), width(width), height(height), canvas(0), isPopupsDirty(false), needsPainting(false), parent(parent), refCount(0), 
clientObject(0), mouseX(0), mouseY(0), isTransparent(isTransparent), enableAsyncRendering(enableAsyncRendering), isAsyncRenderDirty(false),
maxAsyncRenderPerSec(maxAsyncRenderPerSec), pageID(-1), nextPageID(1), renderMode(Awesomium::WebView::RENDERMODE_Default)
{
	renderBuffer = new Awesomium::RenderBuffer(width, height);
	canvas = new skia::PlatformCanvas(width, height, true);
	renderBufferLock = new LockImpl();
	refCountLock = new LockImpl();
	navController = new NavigationController(this);

	if(enableAsyncRendering)
		backBuffer = new Awesomium::RenderBuffer(width, height);
	else
		backBuffer = 0;

	buttonState.leftDown = false;
	buttonState.middleDown = false;
	buttonState.rightDown = false;
}

WebViewProxy::~WebViewProxy()
{
	if(backBuffer)
		delete backBuffer;

	delete navController;
	delete refCountLock;
	delete renderBufferLock;
	delete canvas;
	delete renderBuffer;

	LOG(INFO) << "A WebViewProxy has been destroyed.";
}

void WebViewProxy::asyncStartup()
{
	view = ::WebView::Create(this, WebPreferences());
	view->Resize(gfx::Size(width, height));
	clientObject = new ClientObject(parent);

	if(enableAsyncRendering)
	{
		// Sanity check
		if(maxAsyncRenderPerSec <= 0 || maxAsyncRenderPerSec > 300)
			maxAsyncRenderPerSec = 70;

		renderTimer.Start(base::TimeDelta::FromMilliseconds(1000 / maxAsyncRenderPerSec), this, &WebViewProxy::renderAsync);
	}

	LOG(INFO) << "A new WebViewProxy has been created.";
}

void WebViewProxy::asyncShutdown()
{
	if(enableAsyncRendering)
		renderTimer.Stop();

	closeAllPopups();

	view->GetMainFrame()->CallJSGC();
	view->GetMainFrame()->CallJSGC();

	delete clientObject;

	view->Close();

	LOG(INFO) << "In WebViewProxy::asyncShutdown, refCount is " << refCount;

	parent->setFinishShutdown();
	parent = 0;
}	

void WebViewProxy::loadURL(const std::string& url, const std::wstring& frameName, const std::string& username, const std::string& password,
						   const std::string& postData, const std::string& postSeparator)
{
	std::wstring frame = view->GetMainFrame()->GetName();
	if(frameName.length())
		if(view->GetFrameWithName(frameName))
			frame = frameName;

	navController->LoadEntry(new NavigationEntry(nextPageID++, GURL(url), std::wstring(), frame, username, password, postData, postSeparator));
}

void WebViewProxy::loadHTML(const std::string& html, const std::wstring& frameName)
{
	std::string baseDirectory = Awesomium::WebCore::Get().getBaseDirectory();

	std::wstring frame = view->GetMainFrame()->GetName();
	if(frameName.length())
		if(view->GetFrameWithName(frameName))
			frame = frameName;

	navController->LoadEntry(new NavigationEntry(nextPageID++, html, GURL("file:///" + baseDirectory), frame));
}

void WebViewProxy::loadFile(const std::string& file, const std::wstring& frameName)
{
	loadURL("file:///" + Awesomium::WebCore::Get().getBaseDirectory() + file, frameName, "", "", "", "");
}

void WebViewProxy::goToHistoryOffset(int offset)
{
	navController->GoToOffset(offset);
}

void WebViewProxy::executeJavascript(const std::string& javascript, const std::wstring& frameName)
{
	WebFrame* frame = view->GetMainFrame();

	if(frameName.length())
		frame = view->GetFrameWithName(frameName);

	if(!frame)
		return;

	scoped_ptr<WebRequest> request(WebRequest::Create(GURL("javascript:" + javascript + ";void(0);")));
	frame->LoadRequest(request.get());
}

void WebViewProxy::setProperty(const std::string& name, const Awesomium::JSValue& value)
{
	clientObject->setProperty(name, value);
}

void WebViewProxy::setCallback(const std::string& name)
{
	clientObject->setCallback(name);
}

void WebViewProxy::mayBeginRender()
{
	paint();
}

gfx::Rect WebViewProxy::render(Awesomium::WebView::BackgroundInfo* bgInfo, Awesomium::Rect* opaqueRect)
{
	extern Awesomium::WebPerfApi* gPerfApi;
	if (gPerfApi)
		gPerfApi->onPerfStart("WebViewProxy::render");

	static bool omgOldAlpha = false; // kept as a static bool for debugging; harmless otherwise

	if(dirtyArea.IsEmpty() && !needsPainting && !isPopupsDirty)
	{
		if (gPerfApi)
			gPerfApi->onPerfStop("WebViewProxy::render");
		return gfx::Rect();
	}

	Awesomium::RenderBuffer* activeBuffer = enableAsyncRendering? backBuffer : renderBuffer;

	gfx::Rect invalidArea = dirtyArea;

	if(invalidArea.x() > width || invalidArea.y() > height)
		invalidArea = gfx::Rect(width, height);

	if(invalidArea.right() > width)
		invalidArea.set_width(width - invalidArea.x());
	if(invalidArea.bottom() > height)
		invalidArea.set_height(height - invalidArea.y());

	bool useTransparent = isTransparent;
	if (useTransparent && opaqueRect && popups.empty())
	{
		gfx::Rect opaqueArea = gfx::Rect(opaqueRect->x, opaqueRect->y, opaqueRect->width, opaqueRect->height);
		if (opaqueArea.Contains(invalidArea))
			useTransparent = false;
	}	

	if(useTransparent)
	{
		if (bgInfo && (bgInfo->mBitsPerPixel == 32)) // CDH FIXME$$$ does anything special need to be done for 16-bit support? by the time we get here we might be 32-bit regardless...
		{
			//g_PopCap_Chromium_BackgroundSubStr = "lolwut";
			//executeJavascript("document.body.style.backgroundImage='url(http://ghostisland.files.wordpress.com/2009/03/lolwut.jpg)'");

			wchar_t wideBgFileUrlBuf[1024];
			_snwprintf_s(wideBgFileUrlBuf, 1024, 1024, L"%S", bgInfo->mBgFileUrl);
			std::wstring wideBgFileUrl(wideBgFileUrlBuf);

			if (bgInfo->mFlags & Awesomium::WebView::BackgroundInfo::BIF_InjectBackgroundImage)
			{
				executeJavascript(std::string("document.body.style.backgroundImage='url(") + std::string(bgInfo->mBgFileUrl) + std::string(")'"));
			}

			WebCore::BitmapImage* bgImage;
			for (int iPass=0; iPass<2; ++iPass)
			{
				g_PopCap_Chromium_BackgroundSubStr = g_PopCap_Chromium_BackgroundImages.empty() ? bgInfo->mBgSubStr : "";

				bgImage = NULL;
				int bgCount = (int)g_PopCap_Chromium_BackgroundImages.size();
				for (int iBg=0; iBg<bgCount; ++iBg)
				{
					//if (g_PopCap_Chromium_BackgroundImageUrls[iBg].find(wideBgFileUrl) != -1)
					if (g_PopCap_Chromium_BackgroundImageUrls[iBg] == wideBgFileUrl)
					{
						bgImage = g_PopCap_Chromium_BackgroundImages[iBg];
						break;
					}
				}

				if (bgImage)
				{
					if (gPerfApi)
						gPerfApi->onPerfStart("WebViewProxy::render::bgImageCopy");

					WebCore::NativeImagePtr imgPtr = bgImage->nativeImageForCurrentFrame();
					
					imgPtr->lockPixels();
					unsigned char* destBits = (unsigned char*)imgPtr->getPixels();

					if (destBits)
					{
						unsigned char* srcBits = (unsigned char*)bgInfo->mBits;
						int srcHeight = bgInfo->mHeight;
						int srcWidth = bgInfo->mWidth;
						int srcPitchBytes = bgInfo->mPitchBytes;

						int destHeight = imgPtr->height();
						int destWidth = imgPtr->width();
						int destPitchBytes = imgPtr->rowBytes();

						int minHeight = std::min(destHeight, srcHeight);
						int minWidth = std::min(destWidth, srcWidth);

						for (int y=0; y<minHeight; ++y)
						{
							unsigned long* d = (unsigned long*)&destBits[y * destPitchBytes];
							unsigned long* s = (unsigned long*)&srcBits[y * srcPitchBytes];
							memcpy(d, s, minWidth*sizeof(unsigned long));
						}
					}

					imgPtr->unlockPixels();
				
					if (gPerfApi)
						gPerfApi->onPerfStop("WebViewProxy::render::bgImageCopy");
				}

				{
					if (gPerfApi)
						gPerfApi->onPerfStart("WebViewProxy::render::Layout");
					view->Layout();
					if (gPerfApi)
						gPerfApi->onPerfStop("WebViewProxy::render::Layout");
				}
				{
					if (gPerfApi)
						gPerfApi->onPerfStart("WebViewProxy::render::Paint");
					view->Paint(canvas, invalidArea);
					if (gPerfApi)
						gPerfApi->onPerfStop("WebViewProxy::render::Paint");
				}

				if (bgImage)
					break;
			}

			needsPainting = false;
			
			{
				if (gPerfApi)
					gPerfApi->onPerfStart("WebViewProxy::render::bufferCopy");
				const SkBitmap& sourceBitmap = canvas->getTopPlatformDevice().accessBitmap(false);
				SkAutoLockPixels sourceBitmapLock(sourceBitmap);

				activeBuffer->copyArea((unsigned char*)sourceBitmap.getPixels() + invalidArea.y()*sourceBitmap.rowBytes() + invalidArea.x()*4, 
					sourceBitmap.rowBytes(), invalidArea);

				if (gPerfApi)
					gPerfApi->onPerfStop("WebViewProxy::render::bufferCopy");
			}
		}
		else
		{
			executeJavascript("document.body.style.backgroundColor = '#000000'");

			view->Layout();
			view->Paint(canvas, invalidArea);

			int rowBytes, height, size;

			{
				const SkBitmap& sourceBitmap = canvas->getTopPlatformDevice().accessBitmap(false);
				SkAutoLockPixels sourceBitmapLock(sourceBitmap);
				rowBytes = sourceBitmap.rowBytes();
				height = sourceBitmap.height();
				size = rowBytes * height;

				//activeBuffer->copyFrom((unsigned char*)sourceBitmap.getPixels(), sourceBitmap.rowBytes());
				activeBuffer->copyArea((unsigned char*)sourceBitmap.getPixels() + invalidArea.y()*sourceBitmap.rowBytes() + invalidArea.x()*4, 
				sourceBitmap.rowBytes(), invalidArea);
			}

			executeJavascript("document.body.style.backgroundColor = '#FFFFFF'");
			view->Layout();
			view->Paint(canvas, invalidArea);
			needsPainting = false;

			{
				const SkBitmap& sourceBitmap = canvas->getTopPlatformDevice().accessBitmap(false);
				SkAutoLockPixels sourceBitmapLock(sourceBitmap);

				unsigned char* buffer = (unsigned char*)sourceBitmap.getPixels();

				//for(int i = 0; i < size; i += 4)
				//	activeBuffer->buffer[i+3] = 255 - (buffer[i] - activeBuffer->buffer[i]);
	#define TRUE_COLOR_TRANSPARENCY 0

	#if TRUE_COLOR_TRANSPARENCY
				int rowOffset, offset;
				float a, b;
				for(int row = 0; row < invalidArea.height(); row++)
				{
					rowOffset = (row + invalidArea.y()) * activeBuffer->rowSpan;
					for(int col = 0; col < invalidArea.width(); col++)
					{
						offset = rowOffset + (invalidArea.x() + col) * 4;
						a = activeBuffer->buffer[offset + 3] = 255 - (buffer[offset] - activeBuffer->buffer[offset]);
						if(a > 3 && a < 252)
						{
							b = 255 - a;
							a /= 255.0f;
							activeBuffer->buffer[offset++] = (buffer[offset] - b) / a;
							activeBuffer->buffer[offset++] = (buffer[offset] - b) / a;
							activeBuffer->buffer[offset] = (buffer[offset] - b) / a;
						}
					}
				}
	#else
				if (omgOldAlpha)
				{
					int rowOffset, offset;
					for(int row = 0; row < invalidArea.height(); row++)
					{
						rowOffset = (row + invalidArea.y()) * activeBuffer->rowSpan;
						for(int col = 0; col < invalidArea.width(); col++)
						{
							offset = rowOffset + (invalidArea.x() + col) * 4;
							activeBuffer->buffer[offset + 3] = 255 - (buffer[offset] - activeBuffer->buffer[offset]);
						}
					}
				}
				else
				{
					int offset;
					int iaHeight = invalidArea.height();
					int iaWidth = invalidArea.width();
					int iaX = invalidArea.x();
					int iaY = invalidArea.y();
					
					for(int row = 0; row < iaHeight; ++row)
					{
						offset = ((row + iaY) * activeBuffer->rowSpan) + iaX * 4;
						for(int col = 0; col < iaWidth; ++col, offset += 4)
						{
							activeBuffer->buffer[offset + 3] = 255 - (buffer[offset] - activeBuffer->buffer[offset]);
						}
					}
				}
	#endif
			}
		}
	}
	else
	{
		paint();
		
		const SkBitmap& sourceBitmap = canvas->getTopPlatformDevice().accessBitmap(false);
		SkAutoLockPixels sourceBitmapLock(sourceBitmap);

		activeBuffer->copyArea((unsigned char*)sourceBitmap.getPixels() + invalidArea.y()*sourceBitmap.rowBytes() + invalidArea.x()*4, 
			sourceBitmap.rowBytes(), invalidArea);
	}

	if(popups.size())
	{
		gfx::Rect tempRect;

		for(std::vector<PopupWidget*>::iterator i = popups.begin(); i != popups.end(); i++)
		{
			(*i)->GetWindowRect(0, &tempRect);
			if(!tempRect.IsEmpty())
			{
				(*i)->renderToWebView(activeBuffer, isTransparent);
				invalidArea = invalidArea.Union(tempRect);
			}
		}
		
		invalidArea = gfx::Rect(width, height).Intersect(invalidArea);
	}

	if(enableAsyncRendering)
	{
		renderBufferLock->Lock();
		renderBuffer->copyFrom(activeBuffer->buffer, activeBuffer->rowSpan);
		isAsyncRenderDirty = true;
		parent->setAsyncDirty(true);
		renderBufferLock->Unlock();
	}

	isPopupsDirty = false;

	dirtyArea = gfx::Rect();

	if (gPerfApi)
		gPerfApi->onPerfStop("WebViewProxy::render");

	return invalidArea;
}

void WebViewProxy::copyRenderBuffer(unsigned char* destination, int destRowSpan, int destDepth)
{
	renderBufferLock->Lock();
	renderBuffer->copyTo(destination, destRowSpan, destDepth, Awesomium::WebCore::GetPointer()->getPixelFormat() == Awesomium::PF_RGBA);

	if(isAsyncRenderDirty)
	{
		parent->setAsyncDirty(false);
		isAsyncRenderDirty = false;
	}

	renderBufferLock->Unlock();
}

void WebViewProxy::renderAsync()
{
	render(NULL, NULL);
}

void WebViewProxy::renderSync(unsigned char* destination, int destRowSpan, int destDepth, Awesomium::Rect* renderedRect, Awesomium::WebView::BackgroundInfo* bgInfo, Awesomium::Rect* opaqueRect)
{
	extern Awesomium::WebPerfApi* gPerfApi;
	if (gPerfApi)
		gPerfApi->onPerfStart("WebViewProxy::renderSync");
	if (renderMode == Awesomium::WebView::RENDERMODE_OmgDirtyRect_Full)
	{
		unsigned char* pPixel = renderBuffer->buffer;
		unsigned long color = 0xFF000000 | ((rand() & 255) << 16) | ((rand() & 255) << 8) | (rand() & 255);
		for (int y=0; y<renderBuffer->height; ++y)
		{
			for (int x=0; x<renderBuffer->width; ++x, pPixel += 4)
			{
				*(unsigned long*)pPixel = color;
			}
			pPixel += renderBuffer->rowSpan - (renderBuffer->width * 4);
		}
	}

	gfx::Rect invalidArea = render(bgInfo, opaqueRect);

	if (renderMode == Awesomium::WebView::RENDERMODE_OmgDirtyRect_Frame)
	{
		unsigned long color = 0xFF000000 | ((rand() & 255) << 16) | ((rand() & 255) << 8) | (rand() & 255);

		int minX = std::max(0, invalidArea.x() - 5);
		int maxX = std::min(renderBuffer->width, invalidArea.right() + 5);
		int minY = std::max(0, invalidArea.y() - 5);
		int maxY = std::min(renderBuffer->height, invalidArea.bottom() + 5);

		for (int y=minY; y<invalidArea.y(); ++y)
		{
			unsigned char* pPixel = renderBuffer->buffer + renderBuffer->rowSpan*y + minX*4;
			for (int x=minX; x<maxX; ++x, pPixel += 4)
			{
				*(unsigned long*)pPixel = color;
			}
		}
		for (int y=invalidArea.y(); y<invalidArea.bottom(); ++y)
		{
			unsigned char* pPixel = renderBuffer->buffer + renderBuffer->rowSpan*y + minX*4;
			for (int x=minX; x<invalidArea.x(); ++x, pPixel += 4)
			{
				*(unsigned long*)pPixel = color;
			}
			pPixel += invalidArea.width()*4;
			for (int x=invalidArea.right(); x<maxX; ++x, pPixel += 4)
			{
				*(unsigned long*)pPixel = color;
			}
		}
		for (int y=invalidArea.bottom(); y<maxY; ++y)
		{
			unsigned char* pPixel = renderBuffer->buffer + renderBuffer->rowSpan*y + minX*4;
			for (int x=minX; x<maxX; ++x, pPixel += 4)
			{
				*(unsigned long*)pPixel = color;
			}
		}
	}

	{
		if (gPerfApi)
			gPerfApi->onPerfStart("WebViewProxy::renderSync::copyTo");
		renderBuffer->copyTo(destination, destRowSpan, destDepth, Awesomium::WebCore::GetPointer()->getPixelFormat() == Awesomium::PF_RGBA);
		if (gPerfApi)
			gPerfApi->onPerfStop("WebViewProxy::renderSync::copyTo");
	}

	if(renderedRect)
		*renderedRect = Awesomium::Rect(invalidArea.x(), invalidArea.y(), invalidArea.width(), invalidArea.height());

	if (gPerfApi)
		gPerfApi->onPerfStop("WebViewProxy::renderSync");

	parent->setFinishRender();
}

void WebViewProxy::paint()
{
	if(!dirtyArea.IsEmpty() && needsPainting)
	{
		view->Layout();
		view->Paint(canvas, dirtyArea);
		needsPainting = false;
	}
}

void WebViewProxy::injectMouseMove(int x, int y)
{
	mouseX = x;
	mouseY = y;

	handleMouseEvent(WebMouseEvent::MOUSE_MOVE, 0);
}

void WebViewProxy::injectMouseDown(short mouseButtonID)
{
	handleMouseEvent(WebMouseEvent::MOUSE_DOWN, mouseButtonID);
}

void WebViewProxy::injectMouseUp(short mouseButtonID)
{
	handleMouseEvent(WebMouseEvent::MOUSE_UP, mouseButtonID);
}

void WebViewProxy::injectMouseWheel(int scrollAmount)
{
	WebMouseWheelEvent event;
	event.type = WebInputEvent::MOUSE_WHEEL;
	event.x = mouseX;
	event.y = mouseY;
	event.global_x = mouseX;
	event.global_y = mouseY;
#if defined(WIN32)
	event.timestamp_sec = GetTickCount() / 1000.0;
#endif
	event.layout_test_click_count = 0;
	event.button = WebMouseEvent::BUTTON_NONE;
	event.delta_x = 0;
	event.delta_y = scrollAmount;
	event.scroll_by_page = false;

	view->HandleInputEvent(&event);
}

#if defined(WIN32)
void WebViewProxy::injectKeyboardEvent(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
	WebKeyboardEvent event(hwnd, message, wparam, lparam);

	view->HandleInputEvent(&event);

	checkKeyboardFocus();
}

#elif defined(__APPLE__)
void WebViewProxy::injectKeyboardEvent(NSEvent* keyboardEvent)
{
	WebKeyboardEvent event(keyboardEvent);
	
	view->HandleInputEvent(&event);
}
#endif

void WebViewProxy::cut()
{
	view->GetFocusedFrame()->Cut();
}

void WebViewProxy::copy()
{
	view->GetFocusedFrame()->Copy();
}

void WebViewProxy::paste()
{
	view->GetFocusedFrame()->Paste();
}

void WebViewProxy::selectAll()
{
	view->GetFocusedFrame()->SelectAll();
}

void WebViewProxy::deselectAll()
{
	view->GetFocusedFrame()->ClearSelection();
}

void WebViewProxy::getContentAsText(std::wstring* result, int maxChars)
{
	WebFrame* frame = view->GetMainFrame();

	if(frame)
		frame->GetContentAsPlainText(maxChars, result);

	parent->setFinishGetContentText();
}

void WebViewProxy::zoomIn()
{
	view->ZoomIn(false);
	invalidatePopups();
}

void WebViewProxy::zoomOut()
{
	view->ZoomOut(false);
	invalidatePopups();
}

void WebViewProxy::resetZoom()
{
	view->ResetZoom();
	invalidatePopups();
}

void WebViewProxy::resize(int width, int height)
{
	delete renderBuffer;
	delete canvas;

	this->width = width;
	this->height = height;

	renderBuffer = new Awesomium::RenderBuffer(width, height);
	canvas = new skia::PlatformCanvas(width, height, true);

	if(backBuffer)
	{
		delete backBuffer;
		backBuffer = new Awesomium::RenderBuffer(width, height);
	}

	view->Resize(gfx::Size(width, height));

	DidInvalidateRect(0, gfx::Rect(width, height));
	invalidatePopups();

	dirtyArea = gfx::Rect(width, height);

	if(enableAsyncRendering)
		renderAsync();

	parent->setFinishResize();
}

void WebViewProxy::setTransparent(bool isTransparent)
{
	this->isTransparent = isTransparent;

	DidInvalidateRect(0, gfx::Rect(width, height));
}

void WebViewProxy::setRenderMode(Awesomium::WebView::ERenderMode renderMode)
{
	this->renderMode = renderMode;

	DidInvalidateRect(0, gfx::Rect(width, height));
}

void WebViewProxy::invalidatePopups()
{
	if(parent && !isPopupsDirty)
		parent->setDirty();

	isPopupsDirty = true;
}

void WebViewProxy::AddRef()
{
	refCountLock->Lock();
	refCount++;
	refCountLock->Unlock();
}

void WebViewProxy::Release()
{
	refCountLock->Lock();
	refCount--;
	
	if(refCount == 0)
	{
		refCountLock->Unlock();
		delete this;
	}
	else
	{
		refCountLock->Unlock();
	}
}


// ::WebView additions -------------------------------------------------------

// This method is called to create a new ::WebView.  The ::WebView should not be
// made visible until the new ::WebView's Delegate has its Show method called.
// The returned ::WebView pointer is assumed to be owned by the host window,
// and the caller of CreateWebView should not release the given ::WebView.
// user_gesture is true if a user action initiated this call.
::WebView* WebViewProxy::CreateWebView(::WebView* webview, bool user_gesture)
{
	/**
	* Usually a new WebView is requested to be created when a user clicks on a link
	* that has target='_blank'. For our context, it's better to open the link
	* in the same frame and so we deny the creation of the new WebView and
	* navigate to the last URL that was hovered over.
	*/

	if(!lastTargetURL.is_empty())
		MessageLoop::current()->PostTask(FROM_HERE, NewRunnableMethod(this, &WebViewProxy::loadURL, lastTargetURL.spec(), 
			view->GetFocusedFrame()->GetName(), std::string(), std::string(), std::string(), std::string()));

	return NULL;
}

// This method is called to create a new WebWidget to act as a popup
// (like a drop-down menu).
WebWidget* WebViewProxy::CreatePopupWidget(::WebView* webview, bool focus_on_show)
{
	PopupWidget* popup = new PopupWidget(this);
	popups.push_back(popup);
	return popup->getWidget();
}

// This method is called to create a WebPluginDelegate implementation when a
// new plugin is instanced.  See webkit_glue::CreateWebPluginDelegateHelper
// for a default WebPluginDelegate implementation.
WebPluginDelegate* WebViewProxy::CreatePluginDelegate(::WebView* webview, const GURL& url, 
	const std::string& mime_type, const std::string& clsid, std::string* actual_mime_type)
{
	if(!Awesomium::WebCore::Get().arePluginsEnabled())
		return 0;

	WebPluginInfo info;
	if(!NPAPI::PluginList::Singleton()->GetPluginInfo(url, mime_type, clsid, true, &info, actual_mime_type))
		return 0;

	if(actual_mime_type && !actual_mime_type->empty())
		return WindowlessPlugin::Create(info.path, *actual_mime_type);
	else
		return WindowlessPlugin::Create(info.path, mime_type);
}

// This method is called when default plugin has been correctly created and
// initialized, and found that the missing plugin is available to install or
// user has started installation.
void WebViewProxy::OnMissingPluginStatus(WebPluginDelegate* delegate, int status)
{
}

// This method is called to open a URL in the specified manner.
void WebViewProxy::OpenURL(::WebView* webview, const GURL& url, WindowOpenDisposition disposition)
{
}

// Notifies how many matches have been found so far, for a given request_id.
// |final_update| specifies whether this is the last update (all frames have
// completed scoping).
void WebViewProxy::ReportFindInPageMatchCount(int count, int request_id, bool final_update)
{
}

// Notifies the browser what tick-mark rect is currently selected. Parameter
// |request_id| lets the recipient know which request this message belongs to,
// so that it can choose to ignore the message if it has moved on to other
// things. |selection_rect| is expected to have coordinates relative to the
// top left corner of the web page area and represent where on the screen the
// selection rect is currently located.
void WebViewProxy::ReportFindInPageSelection(int request_id, int active_match_ordinal, const gfx::Rect& selection_rect)
{
}

// This function is called to retrieve a resource bitmap from the
// renderer that was cached as a result of the renderer receiving a
// ViewMsg_Preload_Bitmap message from the browser.
const SkBitmap* WebViewProxy::GetPreloadedResourceBitmap(int resource_id)
{
	return NULL;
}

// Returns whether this ::WebView was opened by a user gesture.
bool WebViewProxy::WasOpenedByUserGesture(::WebView* webview) const
{
	return true;
}

// FrameLoaderClient -------------------------------------------------------

// Notifies the delegate that a load has begun.
void WebViewProxy::DidStartLoading(::WebView* webview)
{
	//Awesomium::WebCore::Get().queueEvent(new WebViewEvents::BeginLoad(parent));
}

// Notifies the delegate that all loads are finished.
void WebViewProxy::DidStopLoading(::WebView* webview)
{
	Awesomium::WebCore::Get().queueEvent(new WebViewEvents::FinishLoad(parent));

	DidInvalidateRect(0, gfx::Rect(width, height));
	invalidatePopups();
}

// The original version of this is WindowScriptObjectAvailable, below. This
// is a Chrome-specific version that serves the same purpose, but has been
// renamed since we haven't implemented WebScriptObject.  Our embedding
// implementation binds native objects to the window via the webframe instead.
// TODO(pamg): If we do implement WebScriptObject, we may wish to switch to
// using the original version of this function.
void WebViewProxy::WindowObjectCleared(WebFrame* webframe)
{
	clientObject->BindToJavascript(webframe, L"Client");
	clientObject->initInternalCallbacks();
}

// PolicyDelegate ----------------------------------------------------------

// This method is called to notify the delegate, and let it modify a
// proposed navigation. It will be called before loading starts, and
// on every redirect.
//
// disposition specifies what should normally happen for this
// navigation (open in current tab, start a new tab, start a new
// window, etc).  This method can return an altered disposition, and
// take any additional separate action it wants to.
//
// is_redirect is true if this is a redirect rather than user action.
WindowOpenDisposition WebViewProxy::DispositionForNavigationAction(::WebView* webview, WebFrame* frame,
	const WebRequest* request, WebNavigationType type, WindowOpenDisposition disposition, bool is_redirect)
{
	bool allow = true;
	Awesomium::WebViewListener* listener = NULL;
	if (parent)
		listener = parent->getListener();
	if (listener)
		allow = listener->onAllowNavigation(request->GetURL().spec(), frame->GetName());

	if (allow)
		Awesomium::WebCore::Get().queueEvent(new WebViewEvents::BeginNavigate(parent, request->GetURL().spec(), frame->GetName()));

	return allow ? CURRENT_TAB : SUPPRESS_OPEN;//IGNORE_ACTION
}

// FrameLoadDelegate -------------------------------------------------------

// Notifies the delegate that the provisional load of a specified frame in a
// given ::WebView has started. By the time the provisional load for a frame has
// started, we know whether or not the current load is due to a client
// redirect or not, so we pass this information through to allow us to set
// the referrer properly in those cases. The consumed_client_redirect_src is
// an empty invalid GURL in other cases.
void WebViewProxy::DidStartProvisionalLoadForFrame(::WebView* webview, WebFrame* frame, NavigationGesture gesture)
{
	WebDataSource* dataSource = frame->GetProvisionalDataSource();

	if(dataSource)
	{
		std::string url = dataSource->GetRequest().GetURL().spec();
		int statusCode = dataSource->GetResponse().GetHttpStatusCode();
		std::string mimeType = dataSource->GetResponse().GetMimeType();
	}
}

// Called when a provisional load is redirected (see GetProvisionalDataSource
// for more info on provisional loads). This happens when the server sends
// back any type of redirect HTTP response.
//
// The redirect information can be retrieved from the provisional data
// source's redirect chain, which will be updated prior to this callback.
// The last element in that vector will be the new URL (which will be the
// same as the provisional data source's current URL), and the next-to-last
// element will be the referring URL.
void WebViewProxy::DidReceiveProvisionalLoadServerRedirect(::WebView* webview, WebFrame* frame) 
{
}

//  @method webView:didFailProvisionalLoadWithError:forFrame:
//  @abstract Notifies the delegate that the provisional load has failed
//  @param webView The ::WebView sending the message
//  @param error The error that occurred
//  @param frame The frame for which the error occurred
//  @discussion This method is called after the provisional data source has
//  failed to load.  The frame will continue to display the contents of the
//  committed data source if there is one.
void WebViewProxy::DidFailProvisionalLoadWithError(::WebView* webview, const WebError& error, WebFrame* frame)
{
}

// If the provisional load fails, we try to load a an error page describing
// the user about the load failure.  |html| is the UTF8 text to display.  If
// |html| is empty, we will fall back on a local error page.
void WebViewProxy::LoadNavigationErrorPage(WebFrame* frame, const WebRequest* failed_request, const WebError& error,
	const std::string& html, bool replace)
{
}

// Notifies the delegate that the load has changed from provisional to
// committed. This method is called after the provisional data source has
// become the committed data source.
//
// In some cases, a single load may be committed more than once. This
// happens in the case of multipart/x-mixed-replace, also known as "server
// push". In this case, a single location change leads to multiple documents
// that are loaded in sequence. When this happens, a new commit will be sent
// for each document.
//
// The "is_new_navigation" flag will be true when a new session history entry
// was created for the load.  The frame's GetHistoryState method can be used
// to get the corresponding session history state.
void WebViewProxy::DidCommitLoadForFrame(::WebView* webview, WebFrame* frame, bool is_new_navigation)
{
	WebDataSource* dataSource = frame->GetDataSource();

	if(dataSource)
	{
		std::string url = dataSource->GetRequest().GetURL().spec();
		int statusCode = dataSource->GetResponse().GetHttpStatusCode();
		std::wstring mimeType = ASCIIToWide(dataSource->GetResponse().GetMimeType());

		LOG(INFO) << "Committed Load for Frame. URL: " << url << ", Status Code: " << statusCode << ", Mime-Type: " << mimeType << ", Frame Name: " << frame->GetName();

		Awesomium::WebCore::Get().queueEvent(new WebViewEvents::BeginLoad(parent, url, frame->GetName(), statusCode, mimeType));

		std::string responseFile;
		Awesomium::WebCore::Get().getCustomResponsePage(statusCode, responseFile);
		
		if(responseFile.length())
			MessageLoop::current()->PostTask(FROM_HERE, NewRunnableMethod(this, &WebViewProxy::loadFile, responseFile, frame->GetName()));

		if(frame->GetName().substr(0, 5) == L"OIFW_")
			MessageLoop::current()->PostTask(FROM_HERE, NewRunnableMethod(this, &WebViewProxy::overrideIFrameWindow, frame->GetName()));
	}

	updateForCommittedLoad(frame, is_new_navigation);
}

//  @method webView:didReceiveTitle:forFrame:
//  @abstract Notifies the delegate that the page title for a frame has been received
//  @param webView The ::WebView sending the message
//  @param title The new page title
//  @param frame The frame for which the title has been received
//  @discussion The title may update during loading; clients should be prepared for this.
//  - (void)webView:(::WebView *)sender didReceiveTitle:(NSString *)title forFrame:(WebFrame *)frame;
void WebViewProxy::DidReceiveTitle(::WebView* webview, const std::wstring& title, WebFrame* frame)
{
	Awesomium::WebCore::Get().queueEvent(new WebViewEvents::ReceiveTitle(parent, title, frame->GetName()));
}

//
//  @method webView:didFinishLoadForFrame:
//  @abstract Notifies the delegate that the committed load of a frame has completed
//  @param webView The ::WebView sending the message
//  @param frame The frame that finished loading
//  @discussion This method is called after the committed data source of a frame has successfully loaded
//  and will only be called when all subresources such as images and stylesheets are done loading.
//  Plug-In content and JavaScript-requested loads may occur after this method is called.
//  - (void)webView:(::WebView *)sender didFinishLoadForFrame:(WebFrame *)frame;
void WebViewProxy::DidFinishLoadForFrame(::WebView* webview, WebFrame* frame)
{
}

//
//  @method webView:didFailLoadWithError:forFrame:
//  @abstract Notifies the delegate that the committed load of a frame has failed
//  @param webView The ::WebView sending the message
//  @param error The error that occurred
//  @param frame The frame that failed to load
//  @discussion This method is called after a data source has committed but failed to completely load.
//  - (void)webView:(::WebView *)sender didFailLoadWithError:(NSError *)error forFrame:(WebFrame *)frame;
void WebViewProxy::DidFailLoadWithError(::WebView* webview, const WebError& error, WebFrame* forFrame)
{
}

// Notifies the delegate of a DOMContentLoaded event.
// This is called when the html resource has been loaded, but
// not necessarily all subresources (images, stylesheets). So, this is called
// before DidFinishLoadForFrame.
void WebViewProxy::DidFinishDocumentLoadForFrame(::WebView* webview, WebFrame* frame)
{
}

// This method is called when we load a resource from an in-memory cache.
// A return value of |false| indicates the load should proceed, but WebCore
// appears to largely ignore the return value.
bool WebViewProxy::DidLoadResourceFromMemoryCache(::WebView* webview, const WebRequest& request, const WebResponse& response, WebFrame* frame)
{
	return false;
}

// This is called after javascript onload handlers have been fired.
void WebViewProxy::DidHandleOnloadEventsForFrame(::WebView* webview, WebFrame* frame)
{
}

// This method is called when anchors within a page have been clicked.
// It is very similar to DidCommitLoadForFrame.
void WebViewProxy::DidChangeLocationWithinPageForFrame(::WebView* webview, WebFrame* frame, bool is_new_navigation)
{
	updateForCommittedLoad(frame, is_new_navigation);
}

// This is called when the favicon for a frame has been received.
void WebViewProxy::DidReceiveIconForFrame(::WebView* webview, WebFrame* frame)
{
}

// Notifies the delegate that a frame will start a client-side redirect. When
// this function is called, the redirect has not yet been started (it may
// not even be scheduled to happen until some point in the future). When the
// redirect has been cancelled or has succeeded, DidStopClientRedirect will
// be called.
//
// WebKit considers meta refreshes, and setting document.location (regardless
// of when called) as client redirects (possibly among others).
//
// This function is intended to continue progress feedback while a
// client-side redirect is pending. Watch out: WebKit seems to call us twice
// for client redirects, resulting in two calls of this function.
void WebViewProxy::WillPerformClientRedirect(::WebView* webview, WebFrame* frame, const GURL& src_url, const GURL& dest_url,
	unsigned int delay_seconds, unsigned int fire_date)
{
	bool allow = true;
	Awesomium::WebViewListener* listener = NULL;
	if (parent)
		listener = parent->getListener();
	if (listener)
		allow = listener->onAllowNavigation(dest_url.spec(), frame->GetName());

	if (!allow)
		frame->StopLoading();
}

// Notifies the delegate that a pending client-side redirect has been
// cancelled (for example, if the frame changes before the timeout) or has
// completed successfully. A client-side redirect is the result of setting
// document.location, for example, as opposed to a server side redirect
// which is the result of HTTP headers (see DidReceiveServerRedirect).
//
// On success, this will be called when the provisional load that the client
// side redirect initiated is committed.
//
// See the implementation of FrameLoader::clientRedirectCancelledOrFinished.
void WebViewProxy::DidCancelClientRedirect(::WebView* webview, WebFrame* frame)
{
}

// Notifies the delegate that the load about to be committed for the specified
// webview and frame was due to a client redirect originating from source URL.
// The information/notification obtained from this method is relevant until
// the next provisional load is started, at which point it becomes obsolete.
void WebViewProxy::DidCompleteClientRedirect(::WebView* webview, WebFrame* frame, const GURL& source)
{
}

//  @method webView:willCloseFrame:
//  @abstract Notifies the delegate that a frame will be closed
//  @param webView The ::WebView sending the message
//  @param frame The frame that will be closed
//  @discussion This method is called right before WebKit is done with the frame
//  and the objects that it contains.
//  - (void)webView:(::WebView *)sender willCloseFrame:(WebFrame *)frame;
void WebViewProxy::WillCloseFrame(::WebView* webview, WebFrame* frame)
{
}

// ResourceLoadDelegate ----------------------------------------------------

// Associates the given identifier with the initial resource request.
// Resource load callbacks will use the identifier throughout the life of the
// request.
void WebViewProxy::AssignIdentifierToRequest(::WebView* webview, uint32 identifier, const WebRequest& request)
{
}

// Notifies the delegate that a request is about to be sent out, giving the
// delegate the opportunity to modify the request.  Note that request is
// writable here, and changes to the URL, for example, will change the request
// to be made.
void WebViewProxy::WillSendRequest(::WebView* webview, uint32 identifier, WebRequest* request)
{
}

// Notifies the delegate that a subresource load has succeeded.
void WebViewProxy::DidFinishLoading(::WebView* webview, uint32 identifier)
{
}

// Notifies the delegate that a subresource load has failed, and why.
void WebViewProxy::DidFailLoadingWithError(::WebView* webview, uint32 identifier, const WebError& error)
{
}

// ChromeClient ------------------------------------------------------------

// Appends a line to the application's error console.  The message contains
// an error description or other information, the line_no provides a line
// number (e.g. for a JavaScript error report), and the source_id contains
// a URL or other description of the source of the message.
void WebViewProxy::AddMessageToConsole(::WebView* webview, const std::wstring& message, unsigned int line_no, const std::wstring& source_id)
{
	LOG(INFO) << "Javascript Error in " << source_id << " at line " << line_no << ": " << message; 

	// When Javascript is executed with an asynchronous result via WebView::executeJavascriptWithResult and the user
	// attempts to retrieve the FutureJSValue, the calling thread will block until the result is received. The problem
	// is that if a Javascript error is encountered during evaluation of the Javascript, the FutureJSValue is never propagated
	// from ClientObject and so we must take this extra precaution to nullify and signal all current FutureJSValue callbacks.
	// Also, Javascript executed via the method described above usually ends up interpreted as line #1, hence line_no == 1.
	if(line_no == 1)
		parent->nullifyFutureJSValueCallbacks();
}

// Notification of possible password forms to be filled/submitted by
// the password manager
void WebViewProxy::OnPasswordFormsSeen(::WebView* webview, const std::vector<PasswordForm>& forms)
{
}

//
void WebViewProxy::OnUnloadListenerChanged(::WebView* webview, WebFrame* webframe)
{
}

// UIDelegate --------------------------------------------------------------

// Asks the browser to show a modal HTML dialog.  The dialog is passed the
// given arguments as a JSON string, and returns its result as a JSON string
// through json_retval.
void WebViewProxy::ShowModalHTMLDialog(const GURL& url, int width, int height, const std::string& json_arguments, std::string* json_retval)
{
}

// Displays a JavaScript alert panel associated with the given view. Clients
// should visually indicate that this panel comes from JavaScript. The panel
// should have a single OK button.
void WebViewProxy::RunJavaScriptAlert(::WebView* webview, const std::wstring& message)
{
}

// Displays a JavaScript confirm panel associated with the given view.
// Clients should visually indicate that this panel comes
// from JavaScript. The panel should have two buttons, e.g. "OK" and
// "Cancel". Returns true if the user hit OK, or false if the user hit Cancel.
bool WebViewProxy::RunJavaScriptConfirm(::WebView* webview, const std::wstring& message)
{
	return false;
}

// Displays a JavaScript text input panel associated with the given view.
// Clients should visually indicate that this panel comes from JavaScript.
// The panel should have two buttons, e.g. "OK" and "Cancel", and an area to
// type text. The default_value should appear as the initial text in the
// panel when it is shown. If the user hit OK, returns true and fills result
// with the text in the box.  The value of result is undefined if the user
// hit Cancel.
bool WebViewProxy::RunJavaScriptPrompt(::WebView* webview, const std::wstring& message, const std::wstring& default_value, std::wstring* result)
{
	return false;
}

// Displays a "before unload" confirm panel associated with the given view.
// The panel should have two buttons, e.g. "OK" and "Cancel", where OK means
// that the navigation should continue, and Cancel means that the navigation
// should be cancelled, leaving the user on the current page.  Returns true
// if the user hit OK, or false if the user hit Cancel.
bool WebViewProxy::RunBeforeUnloadConfirm(::WebView* webview, const std::wstring& message)
{
	return true;  // OK, continue to navigate away
}

// Tells the client that we're hovering over a link with a given URL,
// if the node is not a link, the URL will be an empty GURL.
void WebViewProxy::UpdateTargetURL(::WebView* webview, const GURL& url)
{
	if(lastTargetURL != url)
	{
		lastTargetURL = url;
		Awesomium::WebCore::Get().queueEvent(new WebViewEvents::ChangeTargetURL(parent, url.spec()));
	}
}

// Called to display a file chooser prompt.  The prompt should be pre-
// populated with the given initial_filename string.  The WebViewDelegate
// will own the WebFileChooserCallback object and is responsible for
// freeing it.
void WebViewProxy::RunFileChooser(const std::wstring& initial_filename, WebFileChooserCallback* file_chooser)
{
	delete file_chooser;
}

// @abstract Shows a context menu with commands relevant to a specific
//           element on the current page.
// @param webview The ::WebView sending the delegate method.
// @param node The node(s) the context menu is being invoked on
// @param x The x position of the mouse pointer (relative to the webview)
// @param y The y position of the mouse pointer (relative to the webview)
// @param link_url The absolute URL of the link that contains the node the
// mouse right clicked on
// @param image_url The absolute URL of the image that the mouse right
// clicked on
// @param page_url The URL of the page the mouse right clicked on
// @param frame_url The URL of the subframe the mouse right clicked on
// @param selection_text The raw text of the selection that the mouse right
// clicked on
// @param misspelled_word The editable (possibily) misspelled word
// in the Editor on which dictionary lookup for suggestions will be done.
// @param edit_flags Which edit operations the renderer believes are available
// @param frame_encoding Which indicates the encoding of current focused
// sub frame.
void WebViewProxy::ShowContextMenu(::WebView* webview, ContextNode node, int x, int y, const GURL& link_url, const GURL& image_url,
	const GURL& page_url, const GURL& frame_url, const std::wstring& selection_text, const std::wstring& misspelled_word, int edit_flags, 
	const std::string& frame_encoding)
{
}

// Starts a drag session with the supplied contextual information.
// webview: The ::WebView sending the delegate method.
// drop_data: a WebDropData struct which should contain all the necessary
// information for dragging data out of the webview.
void WebViewProxy::StartDragging(::WebView* webview, const WebDropData& drop_data) 
{
	// Immediately cancel drag
	webview->DragSourceSystemDragEnded();
}

// Returns the focus to the client.
// reverse: Whether the focus should go to the previous (if true) or the next
// focusable element.
void WebViewProxy::TakeFocus(::WebView* webview, bool reverse)
{
}

// Displays JS out-of-memory warning in the infobar
void WebViewProxy::JSOutOfMemory()
{
}

// EditorDelegate ----------------------------------------------------------

// These methods exist primarily to allow a specialized executable to record
// edit events for testing purposes.  Most embedders are not expected to
// override them. In fact, by default these editor delegate methods aren't
// even called by the EditorClient, for performance reasons. To enable them,
// call ::WebView::SetUseEditorDelegate(true) for each ::WebView.

bool WebViewProxy::ShouldBeginEditing(::WebView* webview, std::wstring range)
{
	return true;
}

bool WebViewProxy::ShouldEndEditing(::WebView* webview, std::wstring range)
{
	return true;
}

bool WebViewProxy::ShouldInsertNode(::WebView* webview, std::wstring node, std::wstring range, std::wstring action)
{
	return true;
}

bool WebViewProxy::ShouldInsertText(::WebView* webview, std::wstring text, std::wstring range, std::wstring action)
{
	return true;
}

bool WebViewProxy::ShouldChangeSelectedRange(::WebView* webview, std::wstring fromRange, std::wstring toRange,
	std::wstring affinity, bool stillSelecting)
{
	return true;
}

bool WebViewProxy::ShouldDeleteRange(::WebView* webview, std::wstring range)
{
	return true;
}

bool WebViewProxy::ShouldApplyStyle(::WebView* webview, std::wstring style, std::wstring range)
{
	return true;
}

bool WebViewProxy::SmartInsertDeleteEnabled()
{
	return false;
}

void WebViewProxy::DidBeginEditing() { }
void WebViewProxy::DidChangeSelection() { }
void WebViewProxy::DidChangeContents() { }
void WebViewProxy::DidEndEditing() { }

// Notification that a user metric has occurred.
void WebViewProxy::UserMetricsRecordAction(const std::wstring& action) { }
void WebViewProxy::UserMetricsRecordComputedAction(const std::wstring& action) { }

// -------------------------------------------------------------------------

// Notification that a request to download an image has completed. |errored|
// indicates if there was a network error. The image is empty if there was
// a network error, the contents of the page couldn't by converted to an
// image, or the response from the host was not 200.
// NOTE: image is empty if the response didn't contain image data.
void WebViewProxy::DidDownloadImage(int id, const GURL& image_url, bool errored, const SkBitmap& image)
{
}


// If providing an alternate error page (like link doctor), returns the URL
// to fetch instead.  If an invalid url is returned, just fall back on local
// error pages. |error_name| tells the delegate what type of error page we
// want (e.g., 404 vs dns errors).
GURL WebViewProxy::GetAlternateErrorPageURL(const GURL& failedURL, ErrorPageType error_type)
{
	return GURL();
}

// History Related ---------------------------------------------------------

// Returns the session history entry at a distance |offset| relative to the
// current entry.  Returns NULL on failure.
WebHistoryItem* WebViewProxy::GetHistoryEntryAtOffset(int offset)
{
	NavigationEntry* entry = static_cast<NavigationEntry*>(navController->GetEntryAtOffset(offset));
	
	if(!entry)
		return NULL;

	return entry->GetHistoryItem();
}

// Asynchronously navigates to the history entry at the given offset.
void WebViewProxy::GoToEntryAtOffsetAsync(int offset)
{
	navController->GoToOffset(offset);
}

// Returns how many entries are in the back and forward lists, respectively.
int WebViewProxy::GetHistoryBackListCount()
{
	return navController->GetLastCommittedEntryIndex();
}

int WebViewProxy::GetHistoryForwardListCount()
{
	return navController->GetEntryCount() - navController->GetLastCommittedEntryIndex() - 1;
}

// Notification that the form state of an element in the document, scroll
// position, or possibly something else has changed that affects session
// history (HistoryItem). This function will be called frequently, so the
// implementor should not perform intensive operations in this notification.
void WebViewProxy::OnNavStateChanged(::WebView* webview)
{
}

// -------------------------------------------------------------------------

// Tell the delegate the tooltip text for the current mouse position.
void WebViewProxy::SetTooltipText(::WebView* webview, const std::wstring& tooltip_text)
{
	if(curTooltip != tooltip_text)
	{
		Awesomium::WebCore::Get().queueEvent(new WebViewEvents::ChangeTooltip(parent, tooltip_text));
		curTooltip = tooltip_text;
	}
}

// Downloading -------------------------------------------------------------

void WebViewProxy::DownloadUrl(const GURL& url, const GURL& referrer)
{
}

// Editor Client -----------------------------------------------------------

// Returns true if the word is spelled correctly. The word may begin or end
// with whitespace or punctuation, so the implementor should be sure to handle
// these cases.
//
// If the word is misspelled (returns false), the given first and last
// indices (inclusive) will be filled with the offsets of the boundary of the
// word within the given buffer. The out pointers must be specified. If the
// word is correctly spelled (returns true), they will be set to (0,0).
void WebViewProxy::SpellCheck(const std::wstring& word, int& misspell_location, int& misspell_length)
{
	misspell_location = misspell_length = 0;
}

// Changes the state of the input method editor.
void WebViewProxy::SetInputMethodState(bool enabled)
{
}

// Asks the user to print the page or a specific frame. Called in response to
// a window.print() call.
void WebViewProxy::ScriptedPrint(WebFrame* frame)
{
}

void WebViewProxy::WebInspectorOpened(int num_resources)
{
}

// Called when the FrameLoader goes into a state in which a new page load
// will occur.
void WebViewProxy::TransitionToCommittedForNewPage()
{
}

// BEGIN WEBWIDGET_DELEGATE

// Returns the view in which the WebWidget is embedded.
gfx::NativeViewId WebViewProxy::GetContainingView(WebWidget* webwidget)
{
	return NULL;
}

// Called when a region of the WebWidget needs to be re-painted.
void WebViewProxy::DidInvalidateRect(WebWidget* webwidget, const gfx::Rect& rect)
{
	gfx::Rect clientRect(width, height);
	dirtyArea = clientRect.Intersect(dirtyArea.Union(rect));

	if(parent && !dirtyArea.IsEmpty())
	{
		parent->setDirty();
		needsPainting = true;
	}
}

// Called when a region of the WebWidget, given by clip_rect, should be
// scrolled by the specified dx and dy amounts.
void WebViewProxy::DidScrollRect(WebWidget* webwidget, int dx, int dy, const gfx::Rect& clip_rect)
{
	if(parent && dirtyArea.IsEmpty() && !isPopupsDirty)
		parent->setDirty();

	dirtyArea = gfx::Rect(width, height);
	needsPainting = true;

	for(std::vector<PopupWidget*>::iterator i = popups.begin(); i != popups.end(); i++)
		(*i)->DidScrollWebView(dx, dy);

	isPopupsDirty = true;
}

// This method is called to instruct the window containing the WebWidget to
// show itself as the topmost window.  This method is only used after a
// successful call to CreateWebWidget.  |disposition| indicates how this new
// window should be displayed, but generally only means something for WebViews.
void WebViewProxy::Show(WebWidget* webwidget, WindowOpenDisposition disposition)
{
}

// This method is called to instruct the window containing the WebWidget to
// close.  Note: This method should just be the trigger that causes the
// WebWidget to eventually close.  It should not actually be destroyed until
// after this call returns.
void WebViewProxy::CloseWidgetSoon(WebWidget* webwidget)
{
}

// This method is called to focus the window containing the WebWidget so
// that it receives keyboard events.
void WebViewProxy::Focus(WebWidget* webwidget)
{
	view->SetFocus(true);
	checkKeyboardFocus();
}

// This method is called to unfocus the window containing the WebWidget so that
// it no longer receives keyboard events.
void WebViewProxy::Blur(WebWidget* webwidget)
{
	view->SetFocus(false);
	checkKeyboardFocus();
}

void WebViewProxy::SetCursor(WebWidget* webwidget, const WebCursor& cursor)
{
	if(!cursor.IsEqual(curCursor))
	{
		curCursor = cursor;
#if defined(WIN32)
		Awesomium::WebCore::Get().queueEvent(new WebViewEvents::ChangeCursor(parent, curCursor.GetCursor(GetModuleHandle(0))));
#endif
	}
}

// Returns the rectangle of the WebWidget in screen coordinates.
void WebViewProxy::GetWindowRect(WebWidget* webwidget, gfx::Rect* rect)
{
	*rect = gfx::Rect(width, height);
}

// This method is called to re-position the WebWidget on the screen.  The given
// rect is in screen coordinates.  The implementation may choose to ignore
// this call or modify the given rect.  This method may be called before Show
// has been called.
// TODO(darin): this is more of a request; does this need to take effect
// synchronously?
void WebViewProxy::SetWindowRect(WebWidget* webwidget, const gfx::Rect& rect)
{
}

// Returns the rectangle of the window in which this WebWidget is embeded in.
void WebViewProxy::GetRootWindowRect(WebWidget* webwidget, gfx::Rect* rect)
{
	*rect = gfx::Rect(width, height);
}

void WebViewProxy::GetRootWindowResizerRect(WebWidget* webwidget, gfx::Rect* rect)
{
}

// Keeps track of the necessary window move for a plugin window that resulted
// from a scroll operation.  That way, all plugin windows can be moved at the
// same time as each other and the page.
void WebViewProxy::DidMove(WebWidget* webwidget, const WebPluginGeometry& move)
{
}

// Suppress input events to other windows, and do not return until the widget
// is closed.  This is used to support |window.showModalDialog|.
void WebViewProxy::RunModal(WebWidget* webwidget)
{
}

bool WebViewProxy::IsHidden()
{
	return false;
}

void WebViewProxy::handleMouseEvent(WebInputEvent::Type type, short buttonID)
{
	int x = mouseX;
	int y = mouseY;

	WebMouseEvent event;
	event.type = type;
	event.x = x;
	event.y = y;
	event.global_x = x;
	event.global_y = y;
#if defined(WIN32)
	event.timestamp_sec = GetTickCount() / 1000.0;
#endif
	event.layout_test_click_count = 0;
	event.button = WebMouseEvent::BUTTON_NONE;

	if(type == WebInputEvent::MOUSE_MOVE)
	{
		if(buttonState.leftDown)
			event.button = WebMouseEvent::BUTTON_LEFT;
		else if(buttonState.middleDown)
			event.button = WebMouseEvent::BUTTON_MIDDLE;
		else if(buttonState.rightDown)
			event.button = WebMouseEvent::BUTTON_RIGHT;
	}
	else if(type == WebInputEvent::MOUSE_DOWN || type == WebInputEvent::MOUSE_UP)
	{
		bool buttonChange = type == WebInputEvent::MOUSE_DOWN;

		switch(buttonID)
		{
		case Awesomium::LEFT_MOUSE_BTN:
			buttonState.leftDown = buttonChange;
			event.button = WebMouseEvent::BUTTON_LEFT;
			break;
		case Awesomium::MIDDLE_MOUSE_BTN:
			buttonState.middleDown = buttonChange;
			event.button = WebMouseEvent::BUTTON_MIDDLE;
			break;
		case Awesomium::RIGHT_MOUSE_BTN:
			buttonState.rightDown = buttonChange;
			event.button = WebMouseEvent::BUTTON_RIGHT;
			break;
		}
	}

	for(std::vector<PopupWidget*>::reverse_iterator i = popups.rbegin(); i != popups.rend(); i++)
	{
		if((*i)->isVisible())
		{
			int localX = mouseX;
			int localY = mouseY;
			(*i)->translatePointRelative(localX, localY);

			event.x = localX;
			event.y = localY;

			if((*i)->getWidget()->HandleInputEvent(&event))
				return;
		}
	}

	view->HandleInputEvent(&event);

	if(type != WebInputEvent::MOUSE_MOVE)
		checkKeyboardFocus();
}

void WebViewProxy::closeAllPopups()
{
	for(std::vector<PopupWidget*>::iterator i = popups.begin(); i != popups.end();)
	{
		(*i)->Release();
		i = popups.erase(i);	
	}

	invalidatePopups();
}

void WebViewProxy::closePopup(PopupWidget* popup)
{
	for(std::vector<PopupWidget*>::iterator i = popups.begin(); i != popups.end(); i++)
	{
		if((*i) == popup)
		{
			gfx::Rect winRect;
			(*i)->GetWindowRect(0, &winRect);
			DidInvalidateRect(0, winRect);

			(*i)->Release();
			popups.erase(i);
			invalidatePopups();

			break;
		}
	}
}

void WebViewProxy::checkKeyboardFocus()
{
	executeJavascript("Client.____checkKeyboardFocus(document.activeElement != document.body)");
}

void WebViewProxy::overrideIFrameWindow(const std::wstring& frameName)
{
	WebFrame* frame = view->GetFrameWithName(frameName);

	if(frame)
	{
		scoped_ptr<WebRequest> request(WebRequest::Create(GURL("javascript:window.self = window.top;void(0);")));
		frame->LoadRequest(request.get());
	}
}

bool WebViewProxy::navigate(NavigationEntry *entry, bool reload)
{
	g_PopCap_Chromium_BackgroundImages.clear();
	g_PopCap_Chromium_BackgroundImageUrls.clear();

	WebRequestCachePolicy cache_policy;
	if (reload) {
	  cache_policy = WebRequestReloadIgnoringCacheData;
	  /* CDH commented out: don't get me started
	} else if (entry->GetPageID() != -1) {
	  cache_policy = WebRequestReturnCacheDataElseLoad; */
	} else {
	  cache_policy = WebRequestUseProtocolCachePolicy;
	}

	scoped_ptr<WebRequest> request(WebRequest::Create(entry->GetURL()));
	request->SetCachePolicy(cache_policy);
	// If we are reloading, then WebKit will use the state of the current page.
	// Otherwise, we give it the state to navigate to.
	if (!reload)
	  request->SetHistoryState(entry->GetContentState());

	request->SetExtraData(new NavigationExtraRequestData(entry->GetPageID()));

	std::string username, password;
	entry->GetAuthorizationCredentials(username, password);

	if(username.length() || password.length())
	{
		std::string encodedCredentials;
		if(net::Base64Encode(username + ":" + password, &encodedCredentials))
			request->SetHttpHeaderValue("Authorization", "Basic " + encodedCredentials);
	}

	// Get the right target frame for the entry.
	WebFrame* frame = view->GetMainFrame();
	if (!entry->GetTargetFrame().empty())
		frame = view->GetFrameWithName(entry->GetTargetFrame());
	// TODO(mpcomplete): should we clear the target frame, or should
	// back/forward navigations maintain the target frame?

	if(entry->GetHTMLString().length())
		frame->LoadHTMLString(entry->GetHTMLString(), entry->GetURL());
	else
	{
		const std::string& postData = entry->GetPostData();
		const std::string& postSeparator = entry->GetPostSeparator();
		if (!postData.empty())
		{
			request->SetHttpMethod("POST");
			if (postSeparator.empty())
				request->SetHttpHeaderValue("Content-Type", "application/x-www-form-urlencoded");
			else
				request->SetHttpHeaderValue("Content-Type", "multipart/form-data; boundary=" + postSeparator);

			net::UploadData* ud = new net::UploadData;
			ud->AppendBytes(postData.c_str(), (int)postData.length());
			request->SetUploadData(*ud);
			ud->Release();
		}

		frame->LoadRequest(request.get());
	}

	view->SetFocusedFrame(frame);

	return true;
}

void WebViewProxy::updateForCommittedLoad(WebFrame* frame, bool is_new_navigation)
{
	// Code duplicated from RenderView::DidCommitLoadForFrame.
	const WebRequest& request = view->GetMainFrame()->GetDataSource()->GetRequest();
	NavigationExtraRequestData* extra_data = static_cast<NavigationExtraRequestData*>(request.GetExtraData());

	if(is_new_navigation) 
	{
		// New navigation.
		updateSessionHistory(frame);
		pageID = nextPageID++;
	}
	else if (extra_data && extra_data->pending_page_id != -1 && !extra_data->request_committed)
	{
		// This is a successful session history navigation!
		updateSessionHistory(frame);
		pageID = extra_data->pending_page_id;
	}

	// Don't update session history multiple times.
	if(extra_data)
		extra_data->request_committed = true;

	updateURL(frame);
}

void WebViewProxy::updateURL(WebFrame* frame)
{
	WebDataSource* ds = frame->GetDataSource();
	DCHECK(ds);

	const WebRequest& request = ds->GetRequest();

	// Type is unused.
	scoped_ptr<NavigationEntry> entry(new NavigationEntry);

	// Bug 654101: the referrer will be empty on https->http transitions. It
	// would be nice if we could get the real referrer from somewhere.
	entry->SetPageID(pageID);
	if (ds->HasUnreachableURL()) {
		entry->SetURL(GURL(ds->GetUnreachableURL()));
	} else {
		entry->SetURL(GURL(request.GetURL()));
	}

	navController->DidNavigateToEntry(entry.release());
}

void WebViewProxy::updateSessionHistory(WebFrame* frame)
{
	// If we have a valid page ID at this point, then it corresponds to the page
	// we are navigating away from.  Otherwise, this is the first navigation, so
	// there is no past session history to record.
	if (pageID == -1)
	return;

	NavigationEntry* entry = static_cast<NavigationEntry*>(
	navController->GetEntryWithPageID(pageID));

	if(!entry)
		return;

	std::string state;
	if(!view->GetMainFrame()->GetPreviousHistoryState(&state))
		return;

	entry->SetContentState(state);
}