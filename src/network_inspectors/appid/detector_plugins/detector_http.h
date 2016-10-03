//--------------------------------------------------------------------------
// Copyright (C) 2014-2016 Cisco and/or its affiliates. All rights reserved.
// Copyright (C) 2005-2013 Sourcefire, Inc.
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License Version 2 as published
// by the Free Software Foundation.  You may not use, modify or distribute
// this program under any other version of the GNU General Public License.
//
// This program is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
//--------------------------------------------------------------------------

// detector_http.h author Sourcefire Inc.

#ifndef DETECTOR_HTTP_H
#define DETECTOR_HTTP_H

#include "detector_api.h"
#include "http_common.h"

#include "utils/util.h"

struct httpSession;
struct fflow_info;
struct CHPAction;
struct CHPApp;
class AppIdConfig;

#define MAX_VERSION_SIZE    64

enum httpPatternType
{
    HTTP_PAYLOAD    = 1,
    HTTP_USER_AGENT = 2,
    HTTP_URL        = 3
};

enum HttpId
{
	/* Only store Content-Type, Server, User-Agent & Via headers now. */
	HTTP_ID_CONTENT_TYPE,
	HTTP_ID_SERVER,
	/* HTTP Responses */
	HTTP_ID_COPY,
	HTTP_ID_DELETE,
	HTTP_ID_GET,
	HTTP_ID_HEAD,
	HTTP_ID_OPTIONS,
	HTTP_ID_PROPFIND,
	HTTP_ID_PROPPATCH,
	HTTP_ID_MKCOL,
	HTTP_ID_LOCK,
	HTTP_ID_MOVE,
	HTTP_ID_PUT,
	HTTP_ID_POST,
	HTTP_ID_TRACE,
	HTTP_ID_UNLOCK,
	HTTP_ID_X_WORKING_WITH,
	/* When others are needed, move value to be above HTTP_ID_LEN */
	HTTP_ID_ACCEPT,
	HTTP_ID_ACCEPT_CHARSET,
	HTTP_ID_ACCEPT_ENCODING,
	HTTP_ID_ACCEPT_LANGUAGE,
	HTTP_ID_ACCEPT_RANGES,
	HTTP_ID_AGE,
	HTTP_ID_ALLOW,
	HTTP_ID_AUTHORIZATION,
	HTTP_ID_CACHE_CONTROL,
	HTTP_ID_CONNECTION,
	HTTP_ID_COOKIE,
	HTTP_ID_CONTENT_DISPOSITION,
	HTTP_ID_CONTENT_ENCODING,
	HTTP_ID_CONTENT_LANGUAGE,
	HTTP_ID_CONTENT_LENGTH,
	HTTP_ID_CONTENT_LOCATION,
	HTTP_ID_CONTENT_MD5,
	HTTP_ID_CONTENT_RANGE,
	HTTP_ID_DATE,
	HTTP_ID_ETAG,
	HTTP_ID_EXPECT,
	HTTP_ID_EXPIRES,
	HTTP_ID_FROM,
	HTTP_ID_HOST,
	HTTP_ID_IF_MATCH,
	HTTP_ID_IF_MODIFIED_SINCE,
	HTTP_ID_IF_NONE_MATCH,
	HTTP_ID_IF_RANGE,
	HTTP_ID_IF_UNMODIFIED_SINCE,
	HTTP_ID_LAST_MODIFIED,
	HTTP_ID_LINK,
	HTTP_ID_LOCATION,
	HTTP_ID_MAX_FORWARDS,
	HTTP_ID_P3P,
	HTTP_ID_PRAGMA,
	HTTP_ID_PROXY_AUTHORIZATION,
	HTTP_ID_PROXY_AUTHENTICATE,
	HTTP_ID_RANGE,
	HTTP_ID_REFERER,
	HTTP_ID_REFRESH,
	HTTP_ID_RETRY_AFTER,
	HTTP_ID_SET_COOKIE,
	HTTP_ID_STRICT_TRANSPORT_SECURITY,
	HTTP_ID_TE,
	HTTP_ID_TRAILER,
	HTTP_ID_TRANSFER_ENCODING,
	HTTP_ID_UPGRADE,
	HTTP_ID_USER_AGENT,
	HTTP_ID_VARY,
	HTTP_ID_VIA,
	HTTP_ID_WARNING,
	HTTP_ID_WWW_AUTHENTICATE,
	HTTP_ID_LEN
};

struct HTTPHeaderIndices
{
	int start;
	int end;
};

struct HeaderMatchedPatterns
{
	HTTPHeaderIndices headers[HTTP_ID_LEN];
	int last_match;
	int last_index_end;
	int searched;
};

struct UrlUserData
{
    uint32_t service_id;
    uint32_t client_app;
    uint32_t payload;
    AppId appId;
    tMlpPattern query;
};

struct DetectorAppUrlPattern
{
    struct
    {
        tMlpPattern host;
        tMlpPattern path;
        tMlpPattern scheme;
    } patterns;

    UrlUserData userData;
};

struct DetectorAppUrlList
{
    DetectorAppUrlPattern** urlPattern = nullptr;
    size_t usedCount = 0;
    size_t allocatedCount = 0;
};

void init_http_detector();
int finalize_http_detector();
void clean_http_detector();
void insert_chp_pattern(CHPListElement* chpa);
void insert_http_pattern_element(enum httpPatternType pType, HTTPListElement* element);
void insert_content_type_pattern(HTTPListElement* element);
void insert_url_pattern(DetectorAppUrlPattern* pattern);
void insert_rtmp_url_pattern(DetectorAppUrlPattern* pattern);
void insert_app_url_pattern(DetectorAppUrlPattern* pattern);
DetectorAppUrlList* getAppUrlList();

int geAppidByViaPattern(const uint8_t*, unsigned, char**);
int getHTTPHeaderLocation(const uint8_t*, unsigned, HttpId, int*, int*, HeaderMatchedPatterns*);
inline void FreeMatchedCHPActions(MatchedCHPAction* ma)
{
	MatchedCHPAction* tmp;

	while (ma)
	{
		tmp = ma;
		ma = ma->next;
		snort_free(tmp);
	}
}

int scanKeyCHP(PatternType, char*, int, CHPMatchTally**, MatchedCHPAction**);
AppId scanCHP(PatternType, char*, int, MatchedCHPAction*, char**, char**, char**, int*,
        httpSession*, Packet*);
AppId getAppIdFromUrl(char*, char*, char**, char*, AppId*, AppId*, AppId*, AppId*, unsigned);
AppId geAppidByContentType(const uint8_t*, int);
AppId scan_header_x_working_with(const uint8_t*, uint32_t, char**);
void identifyUserAgent(const uint8_t*, int, AppId*, AppId*, char**);
void getServerVendorVersion(const uint8_t*, int, char**, char**, RNAServiceSubtype**);
int webdav_found(HeaderMatchedPatterns*);

void finalizeFflow(fflow_info*, unsigned app_type_flags, AppId, Packet* );

#endif

