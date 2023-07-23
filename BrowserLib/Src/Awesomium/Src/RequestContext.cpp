// Copyright (c) 2006-2008 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
/*
	Modified by PopCap Games, Inc. for the version included with the
	BrowserLib library.  Change history:

	2009.07.21: Added SexyCookieStore class and usage via mSexyCookieStore
	            field and inCookieStoreFile arguments, for persistent cookie
	            store support.
*/

#include "RequestContext.h"

#include "net/base/cookie_monster.h"
#include "net/proxy/proxy_service.h"
#include "webkit/glue/webkit_glue.h"

class SexyCookieStore : public net::CookieMonster::PersistentCookieStore
{
public:
	enum CookieFlags
	{
		CF_Secure		= (1 << 0),
		CF_HttpOnly		= (1 << 1),
		CF_HasExpires	= (1 << 2),
	};
	
	std::wstring mCookieFileName;
	net::CookieMonster::CookieMap mCookieMap;

	bool WriteMapToDisk()
	{
		struct Local
		{
			static void WriteLong(FILE* fp, unsigned long value)
			{
				fwrite(&value, sizeof(unsigned long), 1, fp);
			}
			static void WriteInt64(FILE* fp, __int64 value)
			{
				fwrite(&value, sizeof(__int64), 1, fp);
			}
			static void WriteString(FILE* fp, const std::string& s)
			{
				unsigned long len = s.length();
				WriteLong(fp, len);
				if (len)
					fwrite(&s[0], 1, len, fp);
			}
		};

		if (mCookieFileName.empty())
			return true;
	
		FILE* fp = _wfopen(mCookieFileName.c_str(), L"wb");
		if (fp)
		{
			unsigned long cookieCount = 0;
			for (net::CookieMonster::CookieMap::iterator it = mCookieMap.begin(); it != mCookieMap.end(); ++it)
				++cookieCount;

			Local::WriteLong(fp, cookieCount);
			for (net::CookieMonster::CookieMap::iterator it = mCookieMap.begin(); it != mCookieMap.end(); ++it)
			{
				std::string key = it->first;
				Local::WriteString(fp, key);

				net::CookieMonster::CanonicalCookie* cc = it->second;
				Local::WriteString(fp, cc->Name());
				Local::WriteString(fp, cc->Value());
				Local::WriteString(fp, cc->Path());
				unsigned long ccFlags = 0;
				if (cc->IsSecure())
					ccFlags |= CF_Secure;
				if (cc->IsHttpOnly())
					ccFlags |= CF_HttpOnly;
				if (cc->DoesExpire())
					ccFlags |= CF_HasExpires;
				Local::WriteLong(fp, ccFlags);
				Local::WriteInt64(fp, cc->CreationDate().ToInternalValue());
				Local::WriteInt64(fp, cc->LastAccessDate().ToInternalValue());
				Local::WriteInt64(fp, cc->ExpiryDate().ToInternalValue());
			}
		
			fclose(fp);
		}

		return true;
	}

	SexyCookieStore(const std::wstring& inFileName)
	: mCookieFileName(inFileName)
	{
	}
	virtual ~SexyCookieStore()
	{
		net::CookieMonster::CookieMap::iterator curIt;
		for (net::CookieMonster::CookieMap::iterator it = mCookieMap.begin(); it != mCookieMap.end(); )
		{
			curIt = it;
			++it;

			delete curIt->second;
			mCookieMap.erase(curIt);
		}
	}

	virtual bool Load(std::vector<net::CookieMonster::KeyedCanonicalCookie>* outKCCs) override
	{
		struct Local
		{
			static unsigned long ReadLong(FILE* fp)
			{
				unsigned long value = 0;
				fread(&value, sizeof(unsigned long), 1, fp);
				return value;
			}
			static __int64 ReadInt64(FILE* fp)
			{
				__int64 value = 0;
				fread(&value, sizeof(__int64), 1, fp);
				return value;
			}
			static std::string ReadString(FILE* fp)
			{
				unsigned long len = ReadLong(fp);
				if (!len)
					return "";
				char* buf = new char[len+1];
				fread(buf, 1, len, fp);
				buf[len] = '\0';
				std::string s = buf;
				delete [] buf;
				return s;
			}
		};

		if (mCookieFileName.empty())
			return true;

		FILE* fp = _wfopen(mCookieFileName.c_str(), L"rb");
		if (fp)
		{
			unsigned long cookieCount = Local::ReadLong(fp);
			for (int i=0; i<cookieCount; ++i)
			{
				std::string key = Local::ReadString(fp);

				std::string ccName = Local::ReadString(fp);
				std::string ccValue = Local::ReadString(fp);
				std::string ccPath = Local::ReadString(fp);
				unsigned long ccFlags = Local::ReadLong(fp);
				base::Time ccCreationTime = base::Time::FromInternalValue(Local::ReadInt64(fp));
				base::Time ccLastAccess = base::Time::FromInternalValue(Local::ReadInt64(fp));
				base::Time ccExpires = base::Time::FromInternalValue(Local::ReadInt64(fp));

				if (outKCCs)
				{
					net::CookieMonster::CanonicalCookie* cc = new net::CookieMonster::CanonicalCookie(
					  ccName, ccValue, ccPath, (ccFlags & CF_Secure)!=0, (ccFlags & CF_HttpOnly)!=0, ccCreationTime, ccLastAccess, (ccFlags & CF_HasExpires)!=0, ccExpires);

					outKCCs->push_back(net::CookieMonster::KeyedCanonicalCookie(key, cc));
				}

				// we can't reuse the allocated cc above; that one will be owned/deleted by the cookiemonster.  so we must allocate another one for us.
				net::CookieMonster::CanonicalCookie* cc = new net::CookieMonster::CanonicalCookie(
				  ccName, ccValue, ccPath, (ccFlags & CF_Secure)!=0, (ccFlags & CF_HttpOnly)!=0, ccCreationTime, ccLastAccess, (ccFlags & CF_HasExpires)!=0, ccExpires);

				mCookieMap.insert(net::CookieMonster::CookieMap::value_type(key, cc));

			}

			fclose(fp);
		}

		return true;
	}

	virtual void AddCookie(const std::string& key, const net::CookieMonster::CanonicalCookie& cc) override
	{
		mCookieMap.insert(net::CookieMonster::CookieMap::value_type(key, new net::CookieMonster::CanonicalCookie(cc)));
	
		WriteMapToDisk();
	}
	virtual void UpdateCookieAccessTime(const net::CookieMonster::CanonicalCookie& cc) override
	{
		for (net::CookieMonster::CookieMap::iterator it = mCookieMap.begin(); it != mCookieMap.end(); ++it)
		{
			if ((it->second->CreationDate() == cc.CreationDate())
			 && (it->second->Name() == cc.Name()))
			{
				it->second->SetLastAccessDate(cc.LastAccessDate());
				break;
			}
		}
	
		WriteMapToDisk();
	}
	virtual void DeleteCookie(const net::CookieMonster::CanonicalCookie& cc) override
	{
		net::CookieMonster::CookieMap::iterator curIt;
		for (net::CookieMonster::CookieMap::iterator it = mCookieMap.begin(); it != mCookieMap.end(); )
		{
			curIt = it;
			++it;

			if ((curIt->second->CreationDate() == cc.CreationDate())
			 && (curIt->second->Name() == cc.Name()))
			{
				delete curIt->second;
				mCookieMap.erase(curIt);
				break;
			}
		}

		WriteMapToDisk();
	}

 private:
  DISALLOW_COPY_AND_ASSIGN(SexyCookieStore);
};

TestShellRequestContext::TestShellRequestContext(const std::wstring& inCookieStoreFile) {
	Init(std::wstring(), net::HttpCache::NORMAL, false, inCookieStoreFile);
}

TestShellRequestContext::TestShellRequestContext(
												 const std::wstring& cache_path,
												 net::HttpCache::Mode cache_mode,
												 bool no_proxy,
												 const std::wstring& inCookieStoreFile) {
	Init(cache_path, cache_mode, no_proxy, inCookieStoreFile);
}

void TestShellRequestContext::Init(
								   const std::wstring& cache_path,
								   net::HttpCache::Mode cache_mode,
								   bool no_proxy,
								   const std::wstring& inCookieStoreFile) {
    mSexyCookieStore = new SexyCookieStore(inCookieStoreFile);
	cookie_store_ = new net::CookieMonster(mSexyCookieStore);
	
	// hard-code A-L and A-C for test shells
	accept_language_ = "en-us,en";
	accept_charset_ = "iso-8859-1,*,utf-8";
	
	net::ProxyInfo proxy_info;
	proxy_info.UseDirect();
	proxy_service_ = net::ProxyService::Create(no_proxy ? &proxy_info : NULL);
	
	net::HttpCache *cache;
	if (cache_path.empty()) {
		cache = new net::HttpCache(proxy_service_, 0);
	} else {
		cache = new net::HttpCache(proxy_service_, cache_path, 0);
	}
	cache->set_mode(cache_mode);
	http_transaction_factory_ = cache;
}

TestShellRequestContext::~TestShellRequestContext() {
	delete cookie_store_;
	delete http_transaction_factory_;
	delete proxy_service_;
	delete mSexyCookieStore;
}

const std::string& TestShellRequestContext::GetUserAgent(
														 const GURL& url) const {
	return webkit_glue::GetUserAgent(url);
}
