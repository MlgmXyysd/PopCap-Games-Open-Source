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

	2009.07.21: Added cookieStoreFile argument and mCookieStoreFile field,
	            for persistent cookie store
	2009.07.29: Added cacheFilePath argument and mCacheFilePath field,
	            for web cache dir support
	2009.08.04: Added reInit support to work around shutdown crash issues
*/

#ifndef __WEBCOREPROXY_H__
#define __WEBCOREPROXY_H__

#include "base/ref_counted.h"
#include "base/timer.h"
#include "webkit/glue/webkit_glue.h"
#include "webkit/glue/webkitclient_impl.h"
#include "WebCString.h"
#include "WebKit.h"
#include "WebString.h"
#include "WebURL.h"
#include "webkit/glue/simple_webmimeregistry_impl.h"
#include "base/waitable_event.h"
#include <string>
#include <list>

namespace base { class Thread; }
namespace Awesomium { class WebCore; }

class WebCoreProxy : public base::RefCountedThreadSafe<WebCoreProxy>,
	public webkit_glue::WebKitClientImpl
{
public:
	WebCoreProxy(base::Thread* coreThread, bool pluginsEnabled, const std::wstring& cookieStoreFile, const std::wstring& cacheFilePath);

	void reInit(const std::wstring& cookieStoreFile, const std::wstring& cacheFilePath);

	void startup();

	~WebCoreProxy();
	
	void pause();
	void resume();
	
	WebKit::WebMimeRegistry* mimeRegistry();
	
	WebKit::WebSandboxSupport* sandboxSupport();
	
	uint64_t visitedLinkHash(const char* canonicalURL, size_t length);
	
	bool isLinkVisited(uint64_t linkHash);
	
	void setCookies(const WebKit::WebURL& url, const WebKit::WebURL& policy_url,
					const WebKit::WebString& value);
	
	WebKit::WebString cookies(const WebKit::WebURL& url, const WebKit::WebURL& policy_url);
	
	void prefetchHostName(const WebKit::WebString&);
	
	WebKit::WebCString loadResource(const char* name);
	
	WebKit::WebString defaultLocale();
protected:
	void asyncStartup();
	
	void asyncPause();

	void pumpPluginMessages();
	void pumpThrottledMessages();
	void purgePluginMessages();

	base::Thread* coreThread;
	bool pluginsEnabled;
	base::RepeatingTimer<WebCoreProxy> pluginMessageTimer;
	webkit_glue::SimpleWebMimeRegistryImpl mime_registry_;
	base::WaitableEvent threadWaitEvent, pauseRequestEvent;
	std::wstring mCookieStoreFile;
	std::wstring mCacheFilePath;

#if defined(WIN32)
	std::list<MSG> throttledMessages;
#endif

	friend class Awesomium::WebCore;
};

#endif