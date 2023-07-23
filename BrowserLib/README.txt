_____Overview

BrowserLib is a library used to embed a web browser into other
applications.  It has two primary goals:

A) Provide web browser capability without having the interface
   depend on any specific browser implementation, and
B) Allow the browser to be linked in dynamically, so that
   varying implementations could be allowed without rebuilding
   a dependent application.

The BrowserLib interface design is based on some of the
interfaces from the Awesomium library.  The current implementation
is based primarily on a modified version of Awesomium as well,
which we have included with this distribution in accordance with
its LGPL license.

The BrowserLib code itself is also governed under the LGPL license;
see the Legal Disclaimer section below for details.


_____Awesomium Modifications

Since BrowserLib currently looks mostly like a thin wrapper around
Awesomium, a number of BrowserLib's improvements over Awesomium
exist as modifications to the Awesomium codebase, which we have
provided.

The modifications are based on Awesomium v1.08 SDK for Win32 MSVC8.
We have removed all ancillary files not required to build our
version of Awesomium (such as demo files, files not required to
build on the Win32 platform, etc).  You can find these files in
an authoritative distribution of Awesomium, at the link provided
in the included Src\Awesomium\README.txt file.

We have made brief comments at the top of each modified source file
indicating the kinds of changes made to the file, however here is
a summary of the many improvements that have been made.

* Added POST request support (previously only GET requests appeared
  to be supported).  The loadURL method now takes postData and
  postSeparator arguments, allowing support for posting large blocks
  of data.
* Added support for a persistent cookie store.  Previously, the
  TestShellRequestContext was using a CookieMonster without a store;
  we're now providing an implementation of PersistentCookieStore
  based on a cookie file stored on disk.
* Got disk-based caching to work.  The previous code almost supported
  this, but a few small pieces were missing and a few settings needed
  alteration.
* Added "re-init" support to reset the WebCore's cookie and cache
  settings after initialization.  Necessary because WebCore had an
  implementation requirement that it only be constructed once, but we
  wanted to be able to change these settings on the fly.
* Added onAllowNavigation listener method, for simple white/blacklisting
  of navigated URLs (white/blacklisting was previously a TODO in the
  DispositionForNavigationAction implementation).
* Added "render mode" setting for things like debugging dirty rects.
* Added "opaque rect" support to transparent rendering code.  For views
  which have transparent edges but an opaque interior, the caller can
  set an opaque rect such that during any given render, if the dirtyrect
  is entirely contained within the opaquerect, transparent rendering can
  be avoided.
* Added "background info" support to transparent rendering code.
  For views that require full transparent rendering but which already have
  the contents of the background being rendered on, background info can be
  set which utilizes the image memory of an agreed-upon background image on
  the web page, replacing it with the actual background contents prior to
  rendering.  This reduces the two-pass transparent rendering approach back
  to one pass.  Note that this specific optimization required a hack to
  the underlying Chromium r11619 rendering code, which has been provided
  under the Src/chromiumtrunk folder; someone with more familiarity with
  the Chromium code is welcome to try and figure out a cleaner way of
  supporting something like this, as we only had time to take the approach
  we did.
* Changed the interface slightly to not use std::vector in DLL calls,
  such as with onCallback.  This allows VS2005-built Awesomium/BrowserLib
  DLLs to be used with VS2008 callers, which previously weren't
  compatible due to STL differences.


_____Legal Disclaimer

BrowserLib Copyright (c) 2010 PopCap Games, Inc.  All rights reserved.

The BrowserLib library ("the Library") is owned and distributed by
PopCap Games, Inc. ("PopCap") under the terms and conditions of the
GNU Lesser General Public License Version 2.1, February 1991 (the
"LGPL License"), a copy of which is available along with the Library
in the LICENSE.txt file or by writing to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
or at http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html

THE LIBRARY IS DISTRIBUTED AS-IS, WITHOUT WARRANTY OF ANY KIND,
AND THE IMPLIED WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A
PARTICULAR PURPOSE ARE EXPRESSELY DISCLAIMED.  The LGPL License
provides additional details about this warranty disclaimer.
