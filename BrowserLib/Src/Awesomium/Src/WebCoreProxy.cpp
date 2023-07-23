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

	2009.07.21: Added cookieStoreFile argument, for persistent cookie store
	2009.07.29: Added cacheFilePath argument, for web cache dir support
	2009.08.04: Added reInit support to work around shutdown crash issues
*/

#include "WebCoreProxy.h"
#include "base/thread.h"
#include "ResourceLoaderBridge.h"
#include "RequestContext.h"
#include "base/icu_util.h"
#include <assert.h>
#include "base/string_util.h"
#if defined(WIN32)
#include "base/resource_util.h"
#include "net/base/net_module.h"
StringPiece NetResourceProvider(int key);
#elif defined(__APPLE__)
#include <Carbon/Carbon.h>
void initMacApplication();
#endif

WebCoreProxy::WebCoreProxy(base::Thread* coreThread, bool pluginsEnabled, const std::wstring& cookieStoreFile, const std::wstring& cacheFilePath): coreThread(coreThread),
	pluginsEnabled(pluginsEnabled), pauseRequestEvent(false, false), threadWaitEvent(false, false), mCookieStoreFile(cookieStoreFile), mCacheFilePath(cacheFilePath)
{
	static bool icuLoaded = false;

	// This is necessary if the WebCoreProxy is created twice during a program's lifecycle
	// because ICU will assert if it is initialized twice.
	if(!icuLoaded)
	{
		if(icu_util::Initialize())
		{
			LOG(INFO) << "ICU successfully initialized.";
			icuLoaded = true;
		}
		else
		{
			LOG(ERROR) << "ICU failed initialization! Have you forgotten to include the DLL?";
		}
	}

	WebKit::initialize(this);
}

WebCoreProxy::~WebCoreProxy()
{
	LOG(INFO) << "Destroying the WebCore.";
	
#if defined(WIN32)
	if(pluginsEnabled)
	{
		throttledMessages.clear();

		MSG msg;
		while(PeekMessage(&msg, 0, NULL, NULL, PM_REMOVE)) {}

		pluginMessageTimer.Stop();
	}
#endif

	LOG(INFO) << "Shutting down Resource Loader Bridge.";
	SimpleResourceLoaderBridge::Shutdown();
}

void WebCoreProxy::reInit(const std::wstring& cookieStoreFile, const std::wstring& cacheFilePath)
{
	LOG(INFO) << "Shutting down Resource Loader Bridge for reinitialization.";
	SimpleResourceLoaderBridge::Shutdown();

	mCookieStoreFile = cookieStoreFile;
	mCacheFilePath = cacheFilePath;

	SimpleResourceLoaderBridge::Init(new TestShellRequestContext(mCacheFilePath, net::HttpCache::NORMAL, false, mCookieStoreFile));
	LOG(INFO) << "Reinitialization of Resource Loader Bridge complete.";
}

void WebCoreProxy::startup()
{
	coreThread->message_loop()->PostTask(FROM_HERE, NewRunnableMethod(this, &WebCoreProxy::asyncStartup));
}

void WebCoreProxy::pause()
{
	coreThread->message_loop()->PostTask(FROM_HERE, NewRunnableMethod(this, &WebCoreProxy::asyncPause));
	pauseRequestEvent.Wait();
}

void WebCoreProxy::asyncPause()
{
	//PlatformThread::Sleep(5);
	pauseRequestEvent.Signal();
	threadWaitEvent.Wait();
}

void WebCoreProxy::resume()
{
	threadWaitEvent.Signal();
}

WebKit::WebMimeRegistry* WebCoreProxy::mimeRegistry()
{
    return &mime_registry_;
}

WebKit::WebSandboxSupport* WebCoreProxy::sandboxSupport()
{
    return NULL;
}

uint64_t WebCoreProxy::visitedLinkHash(const char* canonicalURL, size_t length)
{
    return 0;
}

bool WebCoreProxy::isLinkVisited(uint64_t linkHash)
{
    return false;
}

void WebCoreProxy::setCookies(const WebKit::WebURL& url, const WebKit::WebURL& policy_url,
				const WebKit::WebString& value)
{
    SimpleResourceLoaderBridge::SetCookie(url, policy_url, UTF16ToUTF8(value));
}

WebKit::WebString WebCoreProxy::cookies(const WebKit::WebURL& url, const WebKit::WebURL& policy_url)
{
    return UTF8ToUTF16(SimpleResourceLoaderBridge::GetCookies(url, policy_url));
}

void WebCoreProxy::prefetchHostName(const WebKit::WebString&)
{
}

WebKit::WebCString WebCoreProxy::loadResource(const char* name)
{
    if (!strcmp(name, "deleteButton")) {
		// Create a red 30x30 square.
		const char red_square[] =
		"\x89\x50\x4e\x47\x0d\x0a\x1a\x0a\x00\x00\x00\x0d\x49\x48\x44\x52"
		"\x00\x00\x00\x1e\x00\x00\x00\x1e\x04\x03\x00\x00\x00\xc9\x1e\xb3"
		"\x91\x00\x00\x00\x30\x50\x4c\x54\x45\x00\x00\x00\x80\x00\x00\x00"
		"\x80\x00\x80\x80\x00\x00\x00\x80\x80\x00\x80\x00\x80\x80\x80\x80"
		"\x80\xc0\xc0\xc0\xff\x00\x00\x00\xff\x00\xff\xff\x00\x00\x00\xff"
		"\xff\x00\xff\x00\xff\xff\xff\xff\xff\x7b\x1f\xb1\xc4\x00\x00\x00"
		"\x09\x70\x48\x59\x73\x00\x00\x0b\x13\x00\x00\x0b\x13\x01\x00\x9a"
		"\x9c\x18\x00\x00\x00\x17\x49\x44\x41\x54\x78\x01\x63\x98\x89\x0a"
		"\x18\x50\xb9\x33\x47\xf9\xa8\x01\x32\xd4\xc2\x03\x00\x33\x84\x0d"
		"\x02\x3a\x91\xeb\xa5\x00\x00\x00\x00\x49\x45\x4e\x44\xae\x42\x60"
		"\x82";
		return WebKit::WebCString(red_square, arraysize(red_square));
    }
    return webkit_glue::WebKitClientImpl::loadResource(name);
}

WebKit::WebString WebCoreProxy::defaultLocale()
{
    return ASCIIToUTF16("en-US");
}

void WebCoreProxy::asyncStartup()
{
	SimpleResourceLoaderBridge::Init(new TestShellRequestContext(mCacheFilePath, net::HttpCache::NORMAL, false, mCookieStoreFile));

#if defined(WIN32)
	net::NetModule::SetResourceProvider(NetResourceProvider);
#elif defined(__APPLE__)
	initMacApplication();
#endif

	if(pluginsEnabled)
		pluginMessageTimer.Start(base::TimeDelta::FromMilliseconds(10), this, &WebCoreProxy::pumpPluginMessages);

	LOG(INFO) << "The WebCore is now online.";
}

void WebCoreProxy::pumpPluginMessages()
{
#if defined(WIN32)
	MSG msg;

	while(PeekMessage(&msg, 0, NULL, NULL, PM_REMOVE))
	{
		if(msg.message == WM_USER + 1)
		{
			throttledMessages.push_back(msg);
			
			if(throttledMessages.size() == 1)
				coreThread->message_loop()->PostDelayedTask(FROM_HERE, NewRunnableMethod(this, &WebCoreProxy::pumpThrottledMessages), 5);
		}
		else
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}
#elif defined(__APPLE__)
	/*
	// OSX Message Pump
	EventRef event = NULL;
	EventTargetRef targetWindow;
	targetWindow = GetEventDispatcherTarget();
		
	// If we are unable to get the target then we no longer care about events.
	if( !targetWindow ) return;
		
	// Grab the next event, process it if it is a window event
	if( ReceiveNextEvent( 0, NULL, kEventDurationNoWait, true, &event ) == noErr )
	{
		printf("OSM> Class: %d, Kind: %d\n", GetEventClass(event), GetEventKind(event));
		// Dispatch the event
		SendEventToEventTarget( event, targetWindow );
		ReleaseEvent( event );
	}
	 */
#endif
}

void WebCoreProxy::pumpThrottledMessages()
{
#if defined(WIN32)
	if(!throttledMessages.size())
		return;

	MSG msg = throttledMessages.back();
	throttledMessages.pop_back();

	TranslateMessage(&msg);
	DispatchMessage(&msg);

	if(throttledMessages.size())
		coreThread->message_loop()->PostDelayedTask(FROM_HERE, NewRunnableMethod(this, &WebCoreProxy::pumpThrottledMessages), 5);
#endif
}

void WebCoreProxy::purgePluginMessages()
{
#if defined(WIN32)
	throttledMessages.clear();

	MSG msg;
	while(PeekMessage(&msg, 0, NULL, NULL, PM_REMOVE))
	{
	}
#endif
}

#if defined(WIN32)
StringPiece NetResourceProvider(int key)
{
	void* data_ptr;
	size_t data_size;

	return base::GetDataResourceFromModule(::GetModuleHandle(0), key, &data_ptr, &data_size) ?
		std::string(static_cast<char*>(data_ptr), data_size) : std::string();
}
#endif