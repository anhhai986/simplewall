// Wfp Tool
// Copyright (c) 2016 Henry++

#include <winsock2.h>
#include <ws2ipdef.h>
#include <windns.h>
#include <mstcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <subauth.h>
#include <fwpmu.h>
#include <dbt.h>

#include "main.h"
#include "rapp.h"
#include "routine.h"

#include "pugixml\pugixml.hpp"

#include "resource.h"

CONST UINT WM_FINDMSGSTRING = RegisterWindowMessage (FINDMSGSTRING);

rapp app (APP_NAME, APP_NAME_SHORT, APP_VERSION, APP_COPYRIGHT);

std::unordered_map<size_t, ITEM_APPLICATION> applications;

std::vector<ITEM_COLOR> colors;
std::vector<ITEM_PROCESS> processes;
std::vector<ITEM_RULE> rules;
std::vector<ITEM_RULE_EXT> rules_ext;

STATIC_DATA config;

#define MPS_SERVICE1 L"mpssvc"
#define MPS_SERVICE2 L"mpsdrv"

DWORD changeservicestate (LPCWSTR name, BOOL is_changeconfig, BOOL is_stop)
{
	DWORD result = 0;
	SC_HANDLE scm = OpenSCManager (nullptr, nullptr, SC_MANAGER_ALL_ACCESS);

	if (!scm)
	{
		WDBG (L"OpenSCManager failed. Return value: 0x%.8lx.", GetLastError ());
	}
	else
	{
		SC_HANDLE sc = OpenService (scm, name, is_changeconfig ? SERVICE_CHANGE_CONFIG : SERVICE_QUERY_CONFIG);

		if (!sc)
		{
			WDBG (L"OpenService failed. Return value: 0x%.8lx.", GetLastError ());
		}
		else
		{
			if (is_changeconfig)
			{
				if (!ChangeServiceConfig (sc, SERVICE_NO_CHANGE, is_stop ? SERVICE_DISABLED : SERVICE_AUTO_START, SERVICE_NO_CHANGE, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr))
				{
					WDBG (L"ChangeServiceConfig failed. Return value: 0x%.8lx.", GetLastError ());
				}
			}
			else
			{
				LPVOID buff = nullptr;
				DWORD size = 0;
				DWORD needed = 0;

				while (TRUE)
				{
					if (QueryServiceConfig (sc, (LPQUERY_SERVICE_CONFIG)buff, size, &needed))
					{
						LPQUERY_SERVICE_CONFIG ca = (LPQUERY_SERVICE_CONFIG)buff;

						result = ca->dwStartType;

						break;
					}

					if (GetLastError () != ERROR_INSUFFICIENT_BUFFER)
					{
						free (buff);
						break;
					}

					size += needed;
					buff = realloc (buff, size);
				}
			}

			CloseServiceHandle (sc);
		}

		CloseServiceHandle (scm);
	}

	return result;
}

VOID _app_refreshstatus (HWND hwnd)
{
	_r_status_settext (hwnd, IDC_STATUSBAR, 0, _r_fmt (I18N (&app, IDS_STATUS_TOTAL, 0), applications.size ()));

	switch (app.ConfigGet (L"Mode", Whitelist).AsUint ())
	{
		case Whitelist:
		{
			_r_status_settext (hwnd, IDC_STATUSBAR, 1, I18N (&app, IDS_MODE_WHITELIST, 0));
			break;
		}

		case Blacklist:
		{
			_r_status_settext (hwnd, IDC_STATUSBAR, 1, I18N (&app, IDS_MODE_BLACKLIST, 0));
			break;
		}

		case TrustNoOne:
		{
			_r_status_settext (hwnd, IDC_STATUSBAR, 1, I18N (&app, IDS_MODE_TRUSTNOONE, 0));
			break;
		}
	}
}

VOID _app_getinfo (ITEM_APPLICATION* ptr)
{
	if (ptr)
	{
		SHFILEINFO shfi = {0};
		SHGetFileInfo (ptr->full_path, 0, &shfi, sizeof (shfi), SHGFI_SYSICONINDEX);

		ptr->icon_id = shfi.iIcon;

		HINSTANCE h = LoadLibraryEx (ptr->full_path, nullptr, DONT_RESOLVE_DLL_REFERENCES | LOAD_LIBRARY_AS_DATAFILE);

		if (h)
		{
			HRSRC hv = FindResource (h, MAKEINTRESOURCE (VS_VERSION_INFO), RT_VERSION);

			if (hv)
			{
				HGLOBAL hg = LoadResource (h, hv);

				if (hg)
				{
					LPVOID versionInfo = LockResource (hg);

					if (versionInfo)
					{
						UINT vLen = 0, langD = 0;
						LPVOID retbuf = nullptr;

						WCHAR author_entry[MAX_PATH] = {0};
						WCHAR description_entry[MAX_PATH] = {0};
						WCHAR version_entry[MAX_PATH] = {0};

						BOOL result = VerQueryValue (versionInfo, L"\\VarFileInfo\\Translation", &retbuf, &vLen);

						if (result && vLen == 4)
						{
							memcpy (&langD, retbuf, 4);
							StringCchPrintf (author_entry, _countof (author_entry), L"\\StringFileInfo\\%02X%02X%02X%02X\\CompanyName", (langD & 0xff00) >> 8, langD & 0xff, (langD & 0xff000000) >> 24, (langD & 0xff0000) >> 16);
							StringCchPrintf (description_entry, _countof (description_entry), L"\\StringFileInfo\\%02X%02X%02X%02X\\FileDescription", (langD & 0xff00) >> 8, langD & 0xff, (langD & 0xff000000) >> 24, (langD & 0xff0000) >> 16);
							StringCchPrintf (version_entry, _countof (version_entry), L"\\StringFileInfo\\%02X%02X%02X%02X\\FileVersion", (langD & 0xff00) >> 8, langD & 0xff, (langD & 0xff000000) >> 24, (langD & 0xff0000) >> 16);
						}
						else
						{
							StringCchPrintf (author_entry, _countof (author_entry), L"\\StringFileInfo\\%04X04B0\\CompanyName", GetUserDefaultLangID ());
							StringCchPrintf (description_entry, _countof (description_entry), L"\\StringFileInfo\\%04X04B0\\FileDescription", GetUserDefaultLangID ());
							StringCchPrintf (version_entry, _countof (version_entry), L"\\StringFileInfo\\%04X04B0\\FileVersion", GetUserDefaultLangID ());
						}

						if (VerQueryValue (versionInfo, author_entry, &retbuf, &vLen))
						{
							StringCchCopy (ptr->author, _countof (ptr->author), static_cast<LPCWSTR>(retbuf));
						}

						if (VerQueryValue (versionInfo, description_entry, &retbuf, &vLen))
						{
							StringCchCopy (ptr->description, _countof (ptr->description), static_cast<LPCWSTR>(retbuf));
						}

						if (VerQueryValue (versionInfo, version_entry, &retbuf, &vLen))
						{
							StringCchCopy (ptr->version, _countof (ptr->version), static_cast<LPCWSTR>(retbuf));
						}
					}
				}

				// free memory
				UnlockResource (hg);
				FreeResource (hg);
			}

			FreeLibrary (h); // free memory
		}
	}
}

size_t _app_getposition (HWND hwnd, size_t hash)
{
	for (size_t i = 0; i < _r_listview_getitemcount (hwnd, IDC_LISTVIEW); i++)
	{
		if ((size_t)_r_listview_getlparam (hwnd, IDC_LISTVIEW, i) == hash)
			return i;
	}

	return LAST_VALUE;
}

size_t _app_addapplication (HWND hwnd, rstring path, BOOL is_checked)
{
	if (path.IsEmpty ())
		return 0;

	_R_SPINLOCK (config.lock_add);

	const size_t hash = path.Hash ();

	if (applications.find (hash) == applications.end ())
	{
		ITEM_APPLICATION* ptr = &applications[hash]; // application pointer

		config.is_firstapply = FALSE; // lock checkbox notifications

		// save config
		ptr->is_checked = is_checked;
		ptr->is_system = (_wcsnicmp (path, config.windows_dir, config.wd_length) == 0);

		StringCchCopy (ptr->full_path, _countof (ptr->full_path), path);
		StringCchCopy (ptr->file_dir, _countof (ptr->file_dir), path);
		StringCchCopy (ptr->file_name, _countof (ptr->file_name), PathFindFileName (path));

		PathRemoveFileSpec (ptr->file_dir);

		_app_getinfo (ptr); // read file information

		size_t item = _r_listview_getitemcount (hwnd, IDC_LISTVIEW);

		_r_listview_additem (hwnd, IDC_LISTVIEW, path, item, 0, ptr->icon_id, LAST_VALUE, hash);
		_r_listview_setcheckstate (hwnd, IDC_LISTVIEW, item, is_checked);

		config.is_firstapply = TRUE; // unlock checkbox notifications
	}

	_R_SPINUNLOCK (config.lock_add);

	return hash;
}

VOID _wfp_destroyfilters ()
{
	HANDLE henum = nullptr;
	DWORD result = 0;

	if (!config.hengine || !config.is_admin)
		return;

	result = FwpmFilterCreateEnumHandle (config.hengine, nullptr, &henum);

	if (result != ERROR_SUCCESS)
	{
		WDBG (L"FwpmFilterCreateEnumHandle failed. Return value: 0x%.8lx.", result);
	}
	else
	{
		UINT32 count = 0;
		FWPM_FILTER** matchingFwpFilter = nullptr;

		result = FwpmFilterEnum (config.hengine, henum, 0xFFFFFFFF, &matchingFwpFilter, &count);

		if (result != ERROR_SUCCESS)
		{
			WDBG (L"FwpmFilterEnum failed. Return value: 0x%.8lx.", result);
		}
		else
		{
			if (matchingFwpFilter)
			{
				for (UINT32 i = 0; i < count; i++)
				{
					if (matchingFwpFilter[i]->providerKey && memcmp (matchingFwpFilter[i]->providerKey, &GUID_WfpProvider, sizeof (GUID)) == 0)
						FwpmFilterDeleteById (config.hengine, matchingFwpFilter[i]->filterId);
				}

				FwpmFreeMemory ((LPVOID*)&matchingFwpFilter);
			}
		}
	}

	if (henum)
		FwpmFilterDestroyEnumHandle (config.hengine, henum);
}

VOID _wfp_createfilter (LPCWSTR name, FWPM_FILTER_CONDITION* lpcond, UINT32 const count, UINT8 weight, GUID layer, GUID callout, FWP_ACTION_TYPE type)
{
	if (!config.is_admin || !config.hengine)
		return;

	FWPM_FILTER filter = {0};

	filter.flags = FWPM_FILTER_FLAG_PERSISTENT;

	WCHAR nameptr[MAX_PATH] = {0};
	StringCchCopy (nameptr, _countof (nameptr), name ? name : L"<unnamed>");

	filter.displayData.name = APP_NAME;
	filter.displayData.description = nameptr;

	filter.providerKey = (LPGUID)&GUID_WfpProvider;
	filter.layerKey = layer;
	filter.subLayerKey = GUID_WfpSublayer;

	filter.numFilterConditions = count;
	filter.filterCondition = lpcond;
	filter.action.type = type;
	filter.action.calloutKey = callout;

	filter.weight.type = FWP_UINT8;
	filter.weight.uint8 = weight;

	UINT64 filter_id = 0;
	DWORD result = FwpmFilterAdd (config.hengine, &filter, nullptr, &filter_id);

	if (result != ERROR_SUCCESS)
		WDBG (L"FwpmFilterAdd failed. Return value: 0x%.8lx.", result);
}

INT CALLBACK CompareFunc (LPARAM lp1, LPARAM lp2, LPARAM sortParam)
{
	BOOL isAsc = HIWORD (sortParam);
	BOOL isByFN = LOWORD (sortParam);

	size_t item1 = static_cast<size_t>(lp1);
	size_t item2 = static_cast<size_t>(lp2);

	INT result = 0;

	if (applications.find (item1) == applications.end () || applications.find (item2) == applications.end ())
		return 0;

	const ITEM_APPLICATION* app1 = &applications[item1];
	const ITEM_APPLICATION* app2 = &applications[item2];

	if (app1->is_checked && !app2->is_checked)
	{
		result = -1;
	}
	else if (!app1->is_checked && app2->is_checked)
	{
		result = 1;
	}
	else
	{
		result = _wcsicmp (isByFN ? app1->file_name : app1->file_dir, isByFN ? app2->file_name : app2->file_dir);
	}

	return isAsc ? -result : result;
}

VOID _app_listviewsort (HWND hwnd)
{
	LPARAM lparam = MAKELPARAM (app.ConfigGet (L"SortMode", 1).AsUint (), app.ConfigGet (L"IsSortDescending", FALSE).AsBool ());

	CheckMenuRadioItem (GetMenu (hwnd), IDM_SORTBYFNAME, IDM_SORTBYFDIR, (LOWORD (lparam) ? IDM_SORTBYFNAME : IDM_SORTBYFDIR), MF_BYCOMMAND);
	CheckMenuItem (GetMenu (hwnd), IDM_SORTISDESCEND, MF_BYCOMMAND | (HIWORD (lparam) ? MF_CHECKED : MF_UNCHECKED));

	SendDlgItemMessage (hwnd, IDC_LISTVIEW, LVM_SORTITEMS, lparam, (LPARAM)&CompareFunc);
}

ADDRESS_FAMILY _wfp_parseip (LPCWSTR ip, FWPM_FILTER_CONDITION* pcond, FWP_V4_ADDR_AND_MASK* addrmask4, FWP_V6_ADDR_AND_MASK* addrmask6)
{
	if (pcond)
	{
		NET_ADDRESS_INFO ni;
		SecureZeroMemory (&ni, sizeof (ni));

		BYTE prefix = 0;
		DWORD result = ParseNetworkString (ip, NET_STRING_IP_ADDRESS | NET_STRING_IP_NETWORK | NET_STRING_IP_ADDRESS_NO_SCOPE, &ni, nullptr, &prefix);

		if (result == ERROR_SUCCESS)
		{
			pcond->fieldKey = FWPM_CONDITION_IP_REMOTE_ADDRESS;
			pcond->matchType = FWP_MATCH_EQUAL;

			if (ni.IpAddress.sa_family == AF_INET && addrmask4)
			{
				pcond->conditionValue.type = FWP_V4_ADDR_MASK;
				pcond->conditionValue.v4AddrMask = addrmask4;

				addrmask4->addr = ntohl (ni.Ipv4Address.sin_addr.S_un.S_addr);

				ConvertLengthToIpv4Mask (prefix, (PULONG)&addrmask4->mask);

				addrmask4->mask = ntohl (addrmask4->mask);

				return AF_INET;
			}
			else if (ni.IpAddress.sa_family == AF_INET6 && addrmask6)
			{
				pcond->conditionValue.type = FWP_V6_ADDR_MASK;
				pcond->conditionValue.v6AddrMask = addrmask6;

				memcpy (addrmask6->addr, ni.Ipv6Address.sin6_addr.u.Byte, FWP_V6_ADDR_SIZE);
				addrmask6->prefixLength = prefix;

				return AF_INET6;
			}
		}
		else
		{
			WDBG (L"ParseNetworkString failed. Return value: 0x%.8lx.", result);
		}
	}

	return AF_UNSPEC;
}

LPWSTR LoadTextFromResource (UINT rc_id, LPDWORD size)
{
	HRSRC res = FindResource (nullptr, MAKEINTRESOURCE (rc_id), RT_RCDATA);

	if (res)
	{
		HGLOBAL res_data = LoadResource (nullptr, res);

		if (res_data)
		{
			LPVOID ptr = LockResource (res_data);

			if (ptr)
			{
				if (size)
					*size = SizeofResource (nullptr, res);

				return (LPWSTR)ptr;
			}
		}
	}

	return nullptr;
}

VOID _app_loadrules ()
{
	pugi::xml_document doc;

	rules_ext.clear ();

	// load default rules
	if (app.ConfigGet (L"UseBlocklist", TRUE).AsBool ())
	{
		DWORD size = 0;
		LPWSTR ptr = LoadTextFromResource (1, &size);

		if (ptr)
		{
			if (doc.load_buffer (ptr, size, pugi::parse_escapes, pugi::encoding_utf16_le))
			{
				pugi::xml_node root = doc.child (L"root");

				if (root)
				{
					for (pugi::xml_node item = root.child (L"item"); item; item = item.next_sibling (L"item"))
					{
						ITEM_RULE_EXT rule;

						SecureZeroMemory (&rule, sizeof (rule));

						StringCchCopy (rule.name, _countof (rule.name), item.attribute (L"name").as_string ());
						StringCchCopy (rule.rule, _countof (rule.rule), item.attribute (L"rule").as_string ());

						rule.direction = (EnumDirection)item.attribute (L"direction").as_uint ();
						rule.is_block = item.attribute (L"is_block").as_bool ();
						rule.is_port = item.attribute (L"is_port").as_bool ();
						rule.is_enabled = item.attribute (L"is_enabled").as_bool ();
						rule.is_default = TRUE;

						rules_ext.push_back (rule);
					}
				}

				doc.reset ();
			}
		}
	}

	// load user rules
	if (doc.load_file (config.rules_path, pugi::parse_escapes, pugi::encoding_utf16_le))
	{
		pugi::xml_node root = doc.child (L"root");

		if (root)
		{
			for (pugi::xml_node item = root.child (L"item"); item; item = item.next_sibling (L"item"))
			{
				ITEM_RULE_EXT rule;

				SecureZeroMemory (&rule, sizeof (rule));

				StringCchCopy (rule.name, _countof (rule.name), item.attribute (L"name").as_string ());
				StringCchCopy (rule.rule, _countof (rule.rule), item.attribute (L"rule").as_string ());

				rule.direction = (EnumDirection)item.attribute (L"direction").as_uint ();
				rule.is_block = item.attribute (L"is_block").as_bool ();
				rule.is_port = item.attribute (L"is_port").as_bool ();
				rule.is_enabled = item.attribute (L"is_enabled").as_bool ();
				rule.is_default = FALSE;

				rules_ext.push_back (rule);
			}
		}

		doc.reset ();
	}
}

VOID _app_profilesave (HWND hwnd)
{
	pugi::xml_document doc_apps;
	pugi::xml_node root_apps = doc_apps.append_child (L"root");

	if (root_apps)
	{
		for (size_t i = 0; i < _r_listview_getitemcount (hwnd, IDC_LISTVIEW); i++)
		{
			const size_t hash = (size_t)_r_listview_getlparam (hwnd, IDC_LISTVIEW, i);

			if (!hash || applications.find (hash) == applications.end ())
				continue;

			ITEM_APPLICATION const* ptr = &applications[hash];

			pugi::xml_node item = root_apps.append_child (L"item");

			item.append_attribute (L"path").set_value (ptr->full_path);
			item.append_attribute (L"is_enabled").set_value (ptr->is_checked);
		}

		doc_apps.save_file (config.config_path, L"\t", pugi::format_no_escapes | pugi::format_indent | pugi::format_write_bom, pugi::encoding_utf16_le);
	}

	pugi::xml_document doc_rules;
	pugi::xml_node root_rules = doc_rules.append_child (L"root");

	if (root_rules)
	{
		for (size_t i = 0; i < rules_ext.size (); i++)
		{
			ITEM_RULE_EXT const* ptr = &rules_ext.at (i);

			if (ptr->is_default)
				continue;

			pugi::xml_node item = root_rules.append_child (L"item");

			item.append_attribute (L"name").set_value (ptr->name);
			item.append_attribute (L"rule").set_value (ptr->rule);
			item.append_attribute (L"direction").set_value (ptr->direction);
			item.append_attribute (L"is_block").set_value (ptr->is_block);
			item.append_attribute (L"is_port").set_value (ptr->is_port);
			item.append_attribute (L"is_enabled").set_value (ptr->is_enabled);
		}

		doc_rules.save_file (config.rules_path, L"\t", pugi::format_no_escapes | pugi::format_indent | pugi::format_write_bom, pugi::encoding_utf16_le);
	}
}

VOID _wfp_createportfilter (FWP_BYTE_BLOB* blob, LPCWSTR name, UINT8 proto, GUID portdir, UINT16 port, UINT8 weight, GUID layer, GUID callout, FWP_ACTION_TYPE action)
{
	FWPM_FILTER_CONDITION fwfc[3] = {0};
	UINT32 count = 0;

	if (blob)
	{
		fwfc[count].fieldKey = FWPM_CONDITION_ALE_APP_ID;
		fwfc[count].matchType = FWP_MATCH_EQUAL;
		fwfc[count].conditionValue.type = FWP_BYTE_BLOB_TYPE;
		fwfc[count].conditionValue.byteBlob = blob;

		count += 1;
	}

	if (proto)
	{
		fwfc[count].fieldKey = FWPM_CONDITION_IP_PROTOCOL;
		fwfc[count].matchType = FWP_MATCH_EQUAL;
		fwfc[count].conditionValue.type = FWP_UINT8;
		fwfc[count].conditionValue.uint8 = proto;

		count += 1;
	}

	if (port)
	{
		fwfc[count].fieldKey = portdir;
		fwfc[count].matchType = FWP_MATCH_EQUAL;
		fwfc[count].conditionValue.type = FWP_UINT16;
		fwfc[count].conditionValue.uint16 = port;

		count += 1;
	}

	if (count)
		_wfp_createfilter (name, fwfc, count, weight, layer, callout, action);
}

VOID _wfp_applypath (LPCWSTR path, FWP_ACTION_TYPE action, BOOL is_service)
{
	FWP_BYTE_BLOB* blob = nullptr;

	DWORD result = FwpmGetAppIdFromFileName (path, &blob);

	if (result != ERROR_SUCCESS)
	{
		WDBG (L"FwpmGetAppIdFromFileName failed. Return value: 0x%.8lx. (%s)", result, path);
	}
	else
	{
		if (!is_service)
		{
			FWPM_FILTER_CONDITION fwfc[1] = {0};

			fwfc[0].fieldKey = FWPM_CONDITION_ALE_APP_ID;
			fwfc[0].matchType = FWP_MATCH_EQUAL;
			fwfc[0].conditionValue.type = FWP_BYTE_BLOB_TYPE;
			fwfc[0].conditionValue.byteBlob = blob;

			// set outbound connections
			_wfp_createfilter (path, fwfc, 1, FILTER_WEIGHT_APPLICATION, FWPM_LAYER_ALE_AUTH_CONNECT_V4, GUID_WfpOutboundCallout4, action);
			_wfp_createfilter (path, fwfc, 1, FILTER_WEIGHT_APPLICATION, FWPM_LAYER_ALE_AUTH_CONNECT_V6, GUID_WfpOutboundCallout6, action);

			// set inbound connections
			_wfp_createfilter (path, fwfc, 1, FILTER_WEIGHT_APPLICATION, FWPM_LAYER_ALE_AUTH_RECV_ACCEPT_V4, GUID_WfpInboundCallout4, action);
			_wfp_createfilter (path, fwfc, 1, FILTER_WEIGHT_APPLICATION, FWPM_LAYER_ALE_AUTH_RECV_ACCEPT_V6, GUID_WfpInboundCallout6, action);
		}
		else
		{
			// allow dhcp service
			if (app.ConfigGet (L"AllowDhcpService", TRUE).AsBool ())
			{
				_wfp_createportfilter (blob, path, IPPROTO_UDP, FWPM_CONDITION_IP_REMOTE_PORT, 67, FILTER_WEIGHT_APPLICATION, FWPM_LAYER_ALE_AUTH_CONNECT_V4, GUID_WfpOutboundCallout4, action);
				_wfp_createportfilter (blob, path, IPPROTO_UDP, FWPM_CONDITION_IP_LOCAL_PORT, 68, FILTER_WEIGHT_APPLICATION, FWPM_LAYER_ALE_AUTH_RECV_ACCEPT_V4, GUID_WfpInboundCallout4, action);

				_wfp_createportfilter (blob, path, IPPROTO_UDP, FWPM_CONDITION_IP_LOCAL_PORT, 546, FILTER_WEIGHT_APPLICATION, FWPM_LAYER_ALE_AUTH_RECV_ACCEPT_V6, GUID_WfpInboundCallout6, action);
				_wfp_createportfilter (blob, path, IPPROTO_UDP, FWPM_CONDITION_IP_REMOTE_PORT, 547, FILTER_WEIGHT_APPLICATION, FWPM_LAYER_ALE_AUTH_CONNECT_V6, GUID_WfpOutboundCallout6, action);
			}

			// allow dns service
			if (app.ConfigGet (L"AllowDnsService", TRUE).AsBool ())
			{
				_wfp_createportfilter (blob, path, IPPROTO_UDP, FWPM_CONDITION_IP_REMOTE_PORT, 53, FILTER_WEIGHT_APPLICATION, FWPM_LAYER_ALE_AUTH_CONNECT_V4, GUID_WfpOutboundCallout4, action);
				_wfp_createportfilter (blob, path, IPPROTO_TCP, FWPM_CONDITION_IP_REMOTE_PORT, 53, FILTER_WEIGHT_APPLICATION, FWPM_LAYER_ALE_AUTH_CONNECT_V4, GUID_WfpOutboundCallout4, action);
			}

			// allow ntp service
			if (app.ConfigGet (L"AllowNtpService", TRUE).AsBool ())
			{
				_wfp_createportfilter (blob, path, IPPROTO_UDP, FWPM_CONDITION_IP_REMOTE_PORT, 123, FILTER_WEIGHT_APPLICATION, FWPM_LAYER_ALE_AUTH_CONNECT_V4, GUID_WfpOutboundCallout4, action);
			}

			// allow snmp service
			if (app.ConfigGet (L"AllowSnmpService", FALSE).AsBool ())
			{
				_wfp_createportfilter (blob, path, IPPROTO_UDP, FWPM_CONDITION_IP_REMOTE_PORT, 161, FILTER_WEIGHT_APPLICATION, FWPM_LAYER_ALE_AUTH_CONNECT_V4, GUID_WfpOutboundCallout4, action);
				_wfp_createportfilter (blob, path, IPPROTO_UDP, FWPM_CONDITION_IP_LOCAL_PORT, 162, FILTER_WEIGHT_APPLICATION, FWPM_LAYER_ALE_AUTH_RECV_ACCEPT_V4, GUID_WfpInboundCallout4, action);

				_wfp_createportfilter (blob, path, IPPROTO_UDP, FWPM_CONDITION_IP_REMOTE_PORT, 161, FILTER_WEIGHT_APPLICATION, FWPM_LAYER_ALE_AUTH_CONNECT_V6, GUID_WfpOutboundCallout6, action);
				_wfp_createportfilter (blob, path, IPPROTO_UDP, FWPM_CONDITION_IP_LOCAL_PORT, 162, FILTER_WEIGHT_APPLICATION, FWPM_LAYER_ALE_AUTH_RECV_ACCEPT_V6, GUID_WfpInboundCallout6, action);
			}

			// allow ssdp service
			if (app.ConfigGet (L"AllowSsdpService", TRUE).AsBool ())
			{
				_wfp_createportfilter (blob, path, IPPROTO_TCP, FWPM_CONDITION_IP_REMOTE_PORT, 1900, FILTER_WEIGHT_APPLICATION, FWPM_LAYER_ALE_AUTH_CONNECT_V4, GUID_WfpOutboundCallout4, action);
				_wfp_createportfilter (blob, path, IPPROTO_TCP, FWPM_CONDITION_IP_LOCAL_PORT, 1900, FILTER_WEIGHT_APPLICATION, FWPM_LAYER_ALE_AUTH_RECV_ACCEPT_V4, GUID_WfpInboundCallout4, action);

				_wfp_createportfilter (blob, path, IPPROTO_TCP, FWPM_CONDITION_IP_REMOTE_PORT, 1900, FILTER_WEIGHT_APPLICATION, FWPM_LAYER_ALE_AUTH_CONNECT_V6, GUID_WfpOutboundCallout6, action);
				_wfp_createportfilter (blob, path, IPPROTO_TCP, FWPM_CONDITION_IP_LOCAL_PORT, 1900, FILTER_WEIGHT_APPLICATION, FWPM_LAYER_ALE_AUTH_RECV_ACCEPT_V6, GUID_WfpInboundCallout6, action);

				_wfp_createportfilter (blob, path, IPPROTO_UDP, FWPM_CONDITION_IP_REMOTE_PORT, 1900, FILTER_WEIGHT_APPLICATION, FWPM_LAYER_ALE_AUTH_CONNECT_V4, GUID_WfpOutboundCallout4, action);
				_wfp_createportfilter (blob, path, IPPROTO_UDP, FWPM_CONDITION_IP_LOCAL_PORT, 1900, FILTER_WEIGHT_APPLICATION, FWPM_LAYER_ALE_AUTH_RECV_ACCEPT_V4, GUID_WfpInboundCallout4, action);

				_wfp_createportfilter (blob, path, IPPROTO_UDP, FWPM_CONDITION_IP_REMOTE_PORT, 1900, FILTER_WEIGHT_APPLICATION, FWPM_LAYER_ALE_AUTH_CONNECT_V6, GUID_WfpOutboundCallout6, action);
				_wfp_createportfilter (blob, path, IPPROTO_UDP, FWPM_CONDITION_IP_LOCAL_PORT, 1900, FILTER_WEIGHT_APPLICATION, FWPM_LAYER_ALE_AUTH_RECV_ACCEPT_V6, GUID_WfpInboundCallout6, action);
			}

			// allow windows update service
			if (app.ConfigGet (L"AllowWindowUpdateService", FALSE).AsBool ())
			{
				_wfp_createportfilter (blob, path, IPPROTO_TCP, FWPM_CONDITION_IP_REMOTE_PORT, 80, FILTER_WEIGHT_APPLICATION, FWPM_LAYER_ALE_AUTH_CONNECT_V4, GUID_WfpOutboundCallout4, action);
				_wfp_createportfilter (blob, path, IPPROTO_TCP, FWPM_CONDITION_IP_REMOTE_PORT, 80, FILTER_WEIGHT_APPLICATION, FWPM_LAYER_ALE_AUTH_CONNECT_V6, GUID_WfpOutboundCallout6, action);

				_wfp_createportfilter (blob, path, IPPROTO_TCP, FWPM_CONDITION_IP_REMOTE_PORT, 443, FILTER_WEIGHT_APPLICATION, FWPM_LAYER_ALE_AUTH_CONNECT_V4, GUID_WfpOutboundCallout4, action);
				_wfp_createportfilter (blob, path, IPPROTO_TCP, FWPM_CONDITION_IP_REMOTE_PORT, 443, FILTER_WEIGHT_APPLICATION, FWPM_LAYER_ALE_AUTH_CONNECT_V6, GUID_WfpOutboundCallout6, action);
			}

			// allow netwok discovery service
			if (app.ConfigGet (L"AllowNetworkDiscoveryService", TRUE).AsBool ())
			{
				// microsoft directory
				_wfp_createportfilter (blob, path, IPPROTO_UDP, FWPM_CONDITION_IP_REMOTE_PORT, 445, FILTER_WEIGHT_APPLICATION, FWPM_LAYER_ALE_AUTH_CONNECT_V4, GUID_WfpOutboundCallout4, action);
				_wfp_createportfilter (blob, path, IPPROTO_UDP, FWPM_CONDITION_IP_REMOTE_PORT, 445, FILTER_WEIGHT_APPLICATION, FWPM_LAYER_ALE_AUTH_CONNECT_V6, GUID_WfpOutboundCallout6, action);

				_wfp_createportfilter (blob, path, IPPROTO_TCP, FWPM_CONDITION_IP_REMOTE_PORT, 445, FILTER_WEIGHT_APPLICATION, FWPM_LAYER_ALE_AUTH_CONNECT_V4, GUID_WfpOutboundCallout4, action);
				_wfp_createportfilter (blob, path, IPPROTO_TCP, FWPM_CONDITION_IP_REMOTE_PORT, 445, FILTER_WEIGHT_APPLICATION, FWPM_LAYER_ALE_AUTH_CONNECT_V6, GUID_WfpOutboundCallout6, action);

				_wfp_createportfilter (blob, path, IPPROTO_TCP, FWPM_CONDITION_IP_LOCAL_PORT, 445, FILTER_WEIGHT_APPLICATION, FWPM_LAYER_ALE_AUTH_RECV_ACCEPT_V4, GUID_WfpInboundCallout4, action);
				_wfp_createportfilter (blob, path, IPPROTO_TCP, FWPM_CONDITION_IP_LOCAL_PORT, 445, FILTER_WEIGHT_APPLICATION, FWPM_LAYER_ALE_AUTH_RECV_ACCEPT_V6, GUID_WfpInboundCallout6, action);

				// netbios name
				_wfp_createportfilter (blob, path, IPPROTO_UDP, FWPM_CONDITION_IP_LOCAL_PORT, 137, FILTER_WEIGHT_APPLICATION, FWPM_LAYER_ALE_AUTH_RECV_ACCEPT_V4, GUID_WfpInboundCallout4, action);
				_wfp_createportfilter (blob, path, IPPROTO_UDP, FWPM_CONDITION_IP_LOCAL_PORT, 137, FILTER_WEIGHT_APPLICATION, FWPM_LAYER_ALE_AUTH_RECV_ACCEPT_V6, GUID_WfpInboundCallout6, action);

				_wfp_createportfilter (blob, path, IPPROTO_UDP, FWPM_CONDITION_IP_REMOTE_PORT, 137, FILTER_WEIGHT_APPLICATION, FWPM_LAYER_ALE_AUTH_CONNECT_V4, GUID_WfpOutboundCallout4, action);
				_wfp_createportfilter (blob, path, IPPROTO_UDP, FWPM_CONDITION_IP_REMOTE_PORT, 137, FILTER_WEIGHT_APPLICATION, FWPM_LAYER_ALE_AUTH_CONNECT_V6, GUID_WfpOutboundCallout6, action);

				_wfp_createportfilter (blob, path, IPPROTO_TCP, FWPM_CONDITION_IP_REMOTE_PORT, 137, FILTER_WEIGHT_APPLICATION, FWPM_LAYER_ALE_AUTH_CONNECT_V4, GUID_WfpOutboundCallout4, action);
				_wfp_createportfilter (blob, path, IPPROTO_TCP, FWPM_CONDITION_IP_REMOTE_PORT, 137, FILTER_WEIGHT_APPLICATION, FWPM_LAYER_ALE_AUTH_CONNECT_V6, GUID_WfpOutboundCallout6, action);

				// netbios datagram
				_wfp_createportfilter (blob, path, IPPROTO_UDP, FWPM_CONDITION_IP_LOCAL_PORT, 138, FILTER_WEIGHT_APPLICATION, FWPM_LAYER_ALE_AUTH_RECV_ACCEPT_V4, GUID_WfpInboundCallout4, action);
				_wfp_createportfilter (blob, path, IPPROTO_UDP, FWPM_CONDITION_IP_LOCAL_PORT, 138, FILTER_WEIGHT_APPLICATION, FWPM_LAYER_ALE_AUTH_RECV_ACCEPT_V6, GUID_WfpInboundCallout6, action);

				_wfp_createportfilter (blob, path, IPPROTO_UDP, FWPM_CONDITION_IP_REMOTE_PORT, 138, FILTER_WEIGHT_APPLICATION, FWPM_LAYER_ALE_AUTH_CONNECT_V4, GUID_WfpOutboundCallout4, action);
				_wfp_createportfilter (blob, path, IPPROTO_UDP, FWPM_CONDITION_IP_REMOTE_PORT, 138, FILTER_WEIGHT_APPLICATION, FWPM_LAYER_ALE_AUTH_CONNECT_V6, GUID_WfpOutboundCallout6, action);

				// netbios session
				_wfp_createportfilter (blob, path, IPPROTO_TCP, FWPM_CONDITION_IP_LOCAL_PORT, 139, FILTER_WEIGHT_APPLICATION, FWPM_LAYER_ALE_AUTH_RECV_ACCEPT_V4, GUID_WfpInboundCallout4, action);
				_wfp_createportfilter (blob, path, IPPROTO_TCP, FWPM_CONDITION_IP_LOCAL_PORT, 139, FILTER_WEIGHT_APPLICATION, FWPM_LAYER_ALE_AUTH_RECV_ACCEPT_V6, GUID_WfpInboundCallout6, action);

				_wfp_createportfilter (blob, path, IPPROTO_TCP, FWPM_CONDITION_IP_REMOTE_PORT, 139, FILTER_WEIGHT_APPLICATION, FWPM_LAYER_ALE_AUTH_CONNECT_V4, GUID_WfpOutboundCallout4, action);
				_wfp_createportfilter (blob, path, IPPROTO_TCP, FWPM_CONDITION_IP_REMOTE_PORT, 139, FILTER_WEIGHT_APPLICATION, FWPM_LAYER_ALE_AUTH_CONNECT_V6, GUID_WfpOutboundCallout6, action);
			}
		}

		FwpmFreeMemory ((LPVOID*)&blob);
	}
}

VOID _wfp_applyfilters (HWND hwnd, BOOL is_silent)
{
	if (!config.hengine || !config.is_admin)
		return;

	_R_SPINLOCK (config.lock_apply);

	const EnumMode mode = (EnumMode)app.ConfigGet (L"Mode", Whitelist).AsUint ();

	FWPM_FILTER_CONDITION fwfc[3] = {0};

	DWORD result = FwpmTransactionBegin (config.hengine, 0);

	if (result != ERROR_SUCCESS)
	{
		WDBG (L"FwpmTransactionBegin failed. Return value: 0x%.8lx.", result);
	}
	else
	{
		_wfp_destroyfilters ();

		if (mode != TrustNoOne)
		{
			fwfc[0].fieldKey = FWPM_CONDITION_IP_PROTOCOL;
			fwfc[0].matchType = FWP_MATCH_EQUAL;
			fwfc[0].conditionValue.type = FWP_UINT8;
			//fwfc[0].conditionValue.uint8 = IPPROTO_UDP;

			// allow outbound icmp protocol
			if (app.ConfigGet (L"AllowOutboundIcmp", TRUE).AsBool ())
			{
				fwfc[0].conditionValue.uint8 = IPPROTO_ICMP;
				_wfp_createfilter (nullptr, fwfc, 1, FILTER_WEIGHT_HIGHEST, FWPM_LAYER_ALE_AUTH_CONNECT_V4, GUID_WfpOutboundCallout4, FWP_ACTION_PERMIT);

				fwfc[0].conditionValue.uint8 = IPPROTO_ICMPV6;
				_wfp_createfilter (nullptr, fwfc, 1, FILTER_WEIGHT_HIGHEST, FWPM_LAYER_ALE_AUTH_CONNECT_V6, GUID_WfpOutboundCallout6, FWP_ACTION_PERMIT);
			}

			// allow inbound icmp protocol
			if (app.ConfigGet (L"AllowInboundIcmp", FALSE).AsBool ())
			{
				fwfc[0].conditionValue.uint8 = IPPROTO_ICMP;
				_wfp_createfilter (nullptr, fwfc, 1, FILTER_WEIGHT_HIGHEST, FWPM_LAYER_ALE_AUTH_RECV_ACCEPT_V4, GUID_WfpInboundCallout4, FWP_ACTION_PERMIT);

				fwfc[0].conditionValue.uint8 = IPPROTO_ICMPV6;
				_wfp_createfilter (nullptr, fwfc, 1, FILTER_WEIGHT_HIGHEST, FWPM_LAYER_ALE_AUTH_RECV_ACCEPT_V6, GUID_WfpInboundCallout6, FWP_ACTION_PERMIT);
			}

			// add loopback connections permission
			{
				FWP_V4_ADDR_AND_MASK addrmask4 = {0};
				FWP_V6_ADDR_AND_MASK addrmask6 = {0};

				// First condition. Match only unicast addresses.
				fwfc[0].fieldKey = FWPM_CONDITION_IP_LOCAL_ADDRESS_TYPE;
				fwfc[0].matchType = FWP_MATCH_EQUAL;
				fwfc[0].conditionValue.type = FWP_UINT8;
				fwfc[0].conditionValue.uint8 = NlatUnicast;

				// Second condition. Match all loopback (localhost) data.
				fwfc[1].fieldKey = FWPM_CONDITION_FLAGS;
				fwfc[1].matchType = FWP_MATCH_EQUAL;
				fwfc[1].conditionValue.type = FWP_UINT32;
				fwfc[1].conditionValue.uint32 = FWP_CONDITION_FLAG_IS_LOOPBACK;

				_wfp_createfilter (nullptr, fwfc, 2, FILTER_WEIGHT_HIGHEST, FWPM_LAYER_ALE_AUTH_CONNECT_V4, GUID_WfpOutboundCallout4, FWP_ACTION_PERMIT);
				_wfp_createfilter (nullptr, fwfc, 2, FILTER_WEIGHT_HIGHEST, FWPM_LAYER_ALE_AUTH_CONNECT_V6, GUID_WfpOutboundCallout6, FWP_ACTION_PERMIT);

				_wfp_createfilter (nullptr, fwfc, 2, FILTER_WEIGHT_HIGHEST, FWPM_LAYER_ALE_AUTH_RECV_ACCEPT_V4, GUID_WfpInboundCallout4, FWP_ACTION_PERMIT);
				_wfp_createfilter (nullptr, fwfc, 2, FILTER_WEIGHT_HIGHEST, FWPM_LAYER_ALE_AUTH_RECV_ACCEPT_V6, GUID_WfpInboundCallout6, FWP_ACTION_PERMIT);

				// ipv4/ipv6 loopback
				LPCWSTR loopback4[] = {L"10.0.0.0/8", L"172.16.0.0/12", L"169.254.0.0/16", L"192.168.0.0/16"};

				for (size_t i = 0; i < _countof (loopback4); i++)
				{
					_wfp_parseip (loopback4[i], &fwfc[2], &addrmask4, &addrmask6);

					_wfp_createfilter (loopback4[i], fwfc, 3, FILTER_WEIGHT_HIGHEST, FWPM_LAYER_ALE_AUTH_CONNECT_V4, GUID_WfpOutboundCallout4, FWP_ACTION_PERMIT);
					_wfp_createfilter (loopback4[i], fwfc, 3, FILTER_WEIGHT_HIGHEST, FWPM_LAYER_ALE_AUTH_RECV_ACCEPT_V4, GUID_WfpInboundCallout4, FWP_ACTION_PERMIT);
				}

				LPCWSTR loopback6[] = {L"fd00::/8", L"fe80::/10"};

				for (size_t i = 0; i < _countof (loopback6); i++)
				{
					_wfp_parseip (loopback6[i], &fwfc[2], &addrmask4, &addrmask6);

					_wfp_createfilter (loopback6[i], fwfc, 3, FILTER_WEIGHT_HIGHEST, FWPM_LAYER_ALE_AUTH_CONNECT_V6, GUID_WfpOutboundCallout6, FWP_ACTION_PERMIT);
					_wfp_createfilter (loopback6[i], fwfc, 3, FILTER_WEIGHT_HIGHEST, FWPM_LAYER_ALE_AUTH_RECV_ACCEPT_V6, GUID_WfpInboundCallout6, FWP_ACTION_PERMIT);
				}
			}

			// load rules from file
			_app_loadrules ();

			// apply IP rules
			{
				for (size_t i = 0; i < rules_ext.size (); i++)
				{
					if (!rules_ext.at (i).is_enabled || (app.ConfigGet (L"UseBlocklist", TRUE).AsBool () == FALSE && rules_ext.at (i).is_default))
						continue;

					FWP_ACTION_TYPE action = rules_ext.at (i).is_block ? FWP_ACTION_BLOCK : FWP_ACTION_PERMIT;

					rstring arr = rules_ext.at (i).rule;
					rstring::rvector vc = arr.AsVector (L",; ");

					for (size_t j = 0; j < vc.size (); j++)
					{
						vc.at (j).Trim (L" "); // trim whitespace

						if (rules_ext.at (i).is_port)
						{
							//fwfc[0].fieldKey = FWPM_CONDITION_IP_REMOTE_PORT;
							fwfc[0].matchType = FWP_MATCH_EQUAL;
							fwfc[0].conditionValue.type = FWP_UINT16;
							fwfc[0].conditionValue.uint16 = (UINT16)vc.at (j).AsUint ();

							if (rules_ext.at (i).direction == Out || rules_ext.at (i).direction == Both)
							{
								fwfc[0].fieldKey = FWPM_CONDITION_IP_REMOTE_PORT;

								_wfp_createfilter (rules_ext.at (i).name, fwfc, 1, FILTER_WEIGHT_HIGHEST, FWPM_LAYER_ALE_AUTH_CONNECT_V4, GUID_WfpOutboundCallout4, action);
								_wfp_createfilter (rules_ext.at (i).name, fwfc, 1, FILTER_WEIGHT_HIGHEST, FWPM_LAYER_ALE_AUTH_CONNECT_V6, GUID_WfpOutboundCallout6, action);
							}

							if (rules_ext.at (i).direction == In || rules_ext.at (i).direction == Both)
							{
								fwfc[0].fieldKey = FWPM_CONDITION_IP_LOCAL_ADDRESS;

								_wfp_createfilter (rules_ext.at (i).name, fwfc, 1, FILTER_WEIGHT_HIGHEST, FWPM_LAYER_ALE_AUTH_RECV_ACCEPT_V4, GUID_WfpInboundCallout4, action);
								_wfp_createfilter (rules_ext.at (i).name, fwfc, 1, FILTER_WEIGHT_HIGHEST, FWPM_LAYER_ALE_AUTH_RECV_ACCEPT_V6, GUID_WfpInboundCallout6, action);
							}
						}
						else
						{
							FWP_V4_ADDR_AND_MASK addrmask4 = {0};
							FWP_V6_ADDR_AND_MASK addrmask6 = {0};

							ADDRESS_FAMILY af = _wfp_parseip (vc.at (j), &fwfc[0], &addrmask4, &addrmask6);

							if (af == AF_INET6)
							{
								// outbound ipv6 address
								if (rules_ext.at (i).direction == Out || rules_ext.at (i).direction == Both)
									_wfp_createfilter (rules_ext.at (i).name, fwfc, 1, FILTER_WEIGHT_HIGHEST, FWPM_LAYER_ALE_AUTH_CONNECT_V6, GUID_WfpOutboundCallout6, action);

								// inbound ipv6 address
								if (rules_ext.at (i).direction == In || rules_ext.at (i).direction == Both)
									_wfp_createfilter (rules_ext.at (i).name, fwfc, 1, FILTER_WEIGHT_HIGHEST, FWPM_LAYER_ALE_AUTH_RECV_ACCEPT_V6, GUID_WfpInboundCallout6, action);
							}
							else if (af == AF_INET)
							{
								// outbound ipv4 address
								if (rules_ext.at (i).direction == Out || rules_ext.at (i).direction == Both)
									_wfp_createfilter (rules_ext.at (i).name, fwfc, 1, FILTER_WEIGHT_HIGHEST, FWPM_LAYER_ALE_AUTH_CONNECT_V4, GUID_WfpOutboundCallout4, rules_ext.at (i).is_block ? FWP_ACTION_BLOCK : FWP_ACTION_PERMIT);

								// inbound ipv4 address
								if (rules_ext.at (i).direction == In || rules_ext.at (i).direction == Both)
									_wfp_createfilter (rules_ext.at (i).name, fwfc, 1, FILTER_WEIGHT_HIGHEST, FWPM_LAYER_ALE_AUTH_RECV_ACCEPT_V4, GUID_WfpInboundCallout4, rules_ext.at (i).is_block ? FWP_ACTION_BLOCK : FWP_ACTION_PERMIT);
							}
						}
					}
				}
			}

			// application filters
			{
				BOOL is_svchostallowed = FALSE;
				BOOL is_myselfallowed = FALSE;

				for (auto const& p : applications)
				{
					if (!p.second.is_checked)
						continue;

					if ((mode == Whitelist) && config.svchost_hash == p.first)
						is_svchostallowed = TRUE;

					if ((mode == Whitelist) && config.myself_hash == p.first && app.ConfigGet (L"DoNotBlockMainExecutable", TRUE).AsBool ())
						is_myselfallowed = TRUE;

					_wfp_applypath (p.second.full_path, (mode == Whitelist) ? FWP_ACTION_PERMIT : FWP_ACTION_BLOCK, FALSE);
				}

				// unlock itself
				if (!is_myselfallowed && app.ConfigGet (L"DoNotBlockMainExecutable", TRUE).AsBool ())
					_wfp_applypath (config.myself_path, FWP_ACTION_PERMIT, FALSE);

				// allow services
				if (!is_svchostallowed)
				{
					_wfp_applypath (L"System", FWP_ACTION_PERMIT, TRUE);
					_wfp_applypath (config.svchost_path, FWP_ACTION_PERMIT, TRUE);
				}
			}
		}

		// block all other traffic (only on whitelist mode)
		if (mode == Whitelist)
		{
			_wfp_createfilter (nullptr, nullptr, 0, FILTER_WEIGHT_ALLOWBLOCK, FWPM_LAYER_ALE_AUTH_CONNECT_V4, GUID_WfpOutboundCallout4, FWP_ACTION_BLOCK);
			_wfp_createfilter (nullptr, nullptr, 0, FILTER_WEIGHT_ALLOWBLOCK, FWPM_LAYER_ALE_AUTH_CONNECT_V6, GUID_WfpOutboundCallout6, FWP_ACTION_BLOCK);

			if (app.ConfigGet (L"AllowInboundConnections", FALSE).AsBool () == FALSE)
			{
				_wfp_createfilter (nullptr, nullptr, 0, FILTER_WEIGHT_ALLOWBLOCK, FWPM_LAYER_ALE_AUTH_RECV_ACCEPT_V4, GUID_WfpInboundCallout4, FWP_ACTION_BLOCK);
				_wfp_createfilter (nullptr, nullptr, 0, FILTER_WEIGHT_ALLOWBLOCK, FWPM_LAYER_ALE_AUTH_RECV_ACCEPT_V6, GUID_WfpInboundCallout6, FWP_ACTION_BLOCK);
			}
		}
		else if (mode == TrustNoOne)
		{
			_wfp_createfilter (nullptr, nullptr, 0, FILTER_WEIGHT_HIGHEST, FWPM_LAYER_ALE_AUTH_CONNECT_V4, GUID_WfpOutboundCallout4, FWP_ACTION_BLOCK);
			_wfp_createfilter (nullptr, nullptr, 0, FILTER_WEIGHT_HIGHEST, FWPM_LAYER_ALE_AUTH_CONNECT_V6, GUID_WfpOutboundCallout6, FWP_ACTION_BLOCK);

			_wfp_createfilter (nullptr, nullptr, 0, FILTER_WEIGHT_HIGHEST, FWPM_LAYER_ALE_AUTH_RECV_ACCEPT_V4, GUID_WfpInboundCallout4, FWP_ACTION_BLOCK);
			_wfp_createfilter (nullptr, nullptr, 0, FILTER_WEIGHT_HIGHEST, FWPM_LAYER_ALE_AUTH_RECV_ACCEPT_V6, GUID_WfpInboundCallout6, FWP_ACTION_BLOCK);
		}

		FwpmTransactionCommit (config.hengine);
	}

	if (!is_silent && app.ConfigGet (L"ShowBalloonTips", TRUE).AsBool ())
		app.TrayPopup (NIIF_INFO, APP_NAME, I18N (&app, IDS_STATUS_FILTERS, 0));

	_app_listviewsort (hwnd);
	_app_profilesave (hwnd);

	_R_SPINUNLOCK (config.lock_apply);
}

VOID _wfp_createcallout (HANDLE h, const GUID layer_key, const GUID callout_key)
{
	FWPM_CALLOUT0 callout = {0};
	UINT32 callout_id = 0;

	callout.displayData.name = APP_NAME;
	callout.displayData.description = APP_NAME;

	callout.flags = FWPM_CALLOUT_FLAG_PERSISTENT;

	callout.providerKey = (LPGUID)&GUID_WfpProvider;
	callout.calloutKey = callout_key;
	callout.applicableLayer = layer_key;

	DWORD result = FwpmCalloutAdd (h, &callout, nullptr, &callout_id);

	if (result != ERROR_SUCCESS && result != FWP_E_ALREADY_EXISTS)
		WDBG (L"FwpmCalloutAdd failed. Return value: 0x%.8lx.", result);
}

VOID CALLBACK DropEventCallback (LPVOID, const FWPM_NET_EVENT1* pEvent)
{
	WCHAR prot[100] = {0};

	if (pEvent->header.ipProtocol == IPPROTO_ICMP)
	{
		StringCchCopy (prot, _countof (prot), L"ICMP");
	}
	else if (pEvent->header.ipProtocol == IPPROTO_TCP)
	{
		StringCchCopy (prot, _countof (prot), L"TCP");
	}
	else if (pEvent->header.ipProtocol == IPPROTO_UDP)
	{
		StringCchCopy (prot, _countof (prot), L"UDP");
	}
	else if (pEvent->header.ipProtocol == IPPROTO_ICMPV6)
	{
		StringCchCopy (prot, _countof (prot), L"ICMPv6");
	}

	if (pEvent->header.ipVersion == FWP_IP_VERSION_V4)
	{
		if ((pEvent->header.flags & FWPM_NET_EVENT_FLAG_REMOTE_ADDR_SET) != 0)
		{
			WDBG (L"[%s] %s [%s:%d.%d.%d.%d:%d] Out",
				APP_NAME_SHORT,
				pEvent->header.appId.data,
				prot,
				pEvent->header.remoteAddrV6.byteArray16[3],
				pEvent->header.remoteAddrV6.byteArray16[2],
				pEvent->header.remoteAddrV6.byteArray16[1],
				pEvent->header.remoteAddrV6.byteArray16[0],
				pEvent->header.remotePort
			);
		}

		if ((pEvent->header.flags & FWPM_NET_EVENT_FLAG_LOCAL_ADDR_SET) != 0)
		{
			WDBG (L"[%s] %s [%s:%d.%d.%d.%d:%d] In",
				APP_NAME_SHORT,
				pEvent->header.appId.data,
				prot,
				pEvent->header.localAddrV6.byteArray16[3],
				pEvent->header.localAddrV6.byteArray16[2],
				pEvent->header.localAddrV6.byteArray16[1],
				pEvent->header.localAddrV6.byteArray16[0],
				pEvent->header.localPort
			);
		}
	}
	else if (pEvent->header.ipVersion == FWP_IP_VERSION_V6)
	{
		if ((pEvent->header.flags & FWPM_NET_EVENT_FLAG_REMOTE_ADDR_SET) != 0)
		{
			WDBG (L"[%s] %s [%s:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%d] Out",
				APP_NAME_SHORT,
				pEvent->header.appId.data,
				prot,
				pEvent->header.remoteAddrV6.byteArray16[0],
				pEvent->header.remoteAddrV6.byteArray16[1],
				pEvent->header.remoteAddrV6.byteArray16[2],
				pEvent->header.remoteAddrV6.byteArray16[3],
				pEvent->header.remoteAddrV6.byteArray16[4],
				pEvent->header.remoteAddrV6.byteArray16[5],
				pEvent->header.remoteAddrV6.byteArray16[6],
				pEvent->header.remoteAddrV6.byteArray16[7],
				pEvent->header.remoteAddrV6.byteArray16[8],
				pEvent->header.remoteAddrV6.byteArray16[9],
				pEvent->header.remoteAddrV6.byteArray16[10],
				pEvent->header.remoteAddrV6.byteArray16[11],
				pEvent->header.remoteAddrV6.byteArray16[12],
				pEvent->header.remoteAddrV6.byteArray16[13],
				pEvent->header.remoteAddrV6.byteArray16[14],
				pEvent->header.remoteAddrV6.byteArray16[15],
				pEvent->header.remotePort
			);
		}

		if ((pEvent->header.flags & FWPM_NET_EVENT_FLAG_LOCAL_ADDR_SET) != 0)
		{
			WDBG (L"[%s] %s [%s:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%d] In",
				APP_NAME_SHORT,
				pEvent->header.appId.data,
				prot,
				pEvent->header.localAddrV6.byteArray16[0],
				pEvent->header.localAddrV6.byteArray16[1],
				pEvent->header.localAddrV6.byteArray16[2],
				pEvent->header.localAddrV6.byteArray16[3],
				pEvent->header.localAddrV6.byteArray16[4],
				pEvent->header.localAddrV6.byteArray16[5],
				pEvent->header.localAddrV6.byteArray16[6],
				pEvent->header.localAddrV6.byteArray16[7],
				pEvent->header.localAddrV6.byteArray16[8],
				pEvent->header.localAddrV6.byteArray16[9],
				pEvent->header.localAddrV6.byteArray16[10],
				pEvent->header.localAddrV6.byteArray16[11],
				pEvent->header.localAddrV6.byteArray16[12],
				pEvent->header.localAddrV6.byteArray16[13],
				pEvent->header.localAddrV6.byteArray16[14],
				pEvent->header.localAddrV6.byteArray16[15],
				pEvent->header.localPort
			);
		}
	}
}

VOID _wfp_setevents (BOOL is_install, LPGUID const session_key)
{
	if (!config.hengine)
		return;

	DWORD result = 0;

	if (is_install)
	{
		FWP_VALUE val;
		SecureZeroMemory (&val, sizeof (val));

		val.type = FWP_UINT32;
		val.uint32 = 1;

		result = FwpmEngineSetOption (config.hengine, FWPM_ENGINE_COLLECT_NET_EVENTS, &val);

		if (result != ERROR_SUCCESS)
			WDBG (L"FwpmEngineSetOption failed. Return value: 0x%.8lx.", result);
	}

	FWPMNES0 _FwpmNetEventSubscribe0 = (FWPMNES0)GetProcAddress (GetModuleHandle (L"fwpuclnt.dll"), "FwpmNetEventSubscribe0");
	FWPMNEU0 _FwpmNetEventUnsubscribe0 = (FWPMNEU0)GetProcAddress (GetModuleHandle (L"fwpuclnt.dll"), "FwpmNetEventUnsubscribe0");

	if (!_FwpmNetEventUnsubscribe0 || !_FwpmNetEventUnsubscribe0)
	{
		WDBG (L"GetProcAddress failed. Return value: 0x%.8lx.", GetLastError ());
	}
	else
	{
		if (is_install)
		{
			FWPM_NET_EVENT_ENUM_TEMPLATE0 enumTemplate;
			FWPM_NET_EVENT_SUBSCRIPTION0 subscription;

			SecureZeroMemory (&enumTemplate, sizeof (enumTemplate));
			SecureZeroMemory (&subscription, sizeof (subscription));

			subscription.sessionKey = *session_key;
			subscription.enumTemplate = &enumTemplate;

			result = _FwpmNetEventSubscribe0 (config.hengine, &subscription, DropEventCallback, nullptr, &config.hevent);

			if (result != ERROR_SUCCESS)
				WDBG (L"FwpmNetEventSubscribe0 failed. Return value: 0x%.8lx.", result);
		}
		else
		{
			if (config.hevent)
			{
				result = _FwpmNetEventUnsubscribe0 (config.hengine, config.hevent);
				config.hevent = nullptr;

				if (result != ERROR_SUCCESS)
					WDBG (L"FwpmNetEventUnsubscribe0 failed. Return value: 0x%.8lx.", result);
			}
		}
	}

	if (!is_install)
	{
		FWP_VALUE val;
		SecureZeroMemory (&val, sizeof (val));

		val.type = FWP_UINT32;
		val.uint32 = 0;

		result = FwpmEngineSetOption (config.hengine, FWPM_ENGINE_COLLECT_NET_EVENTS, &val);

		if (result != ERROR_SUCCESS)
			WDBG (L"FwpmEngineSetOption failed. Return value: 0x%.8lx.", result);
	}
}

UINT WINAPI ApplyThread (LPVOID)
{
	HANDLE evts[] = {config.apply_evt, config.apply2_evt, config.stop_evt};

	while (TRUE)
	{
		DWORD state = WaitForMultipleObjectsEx (_countof (evts), evts, FALSE, INFINITE, FALSE);

		if (state == WAIT_OBJECT_0 || (state == WAIT_OBJECT_0 + 1)) // apply event
		{
			BOOL is_silent = (state == WAIT_OBJECT_0 + 1);

			_wfp_applyfilters (app.GetHWND (), is_silent);
		}
		else if (state == WAIT_OBJECT_0 + 2) // stop event
		{
			_wfp_setevents (FALSE, nullptr);
			break;
		}
	}

	return ERROR_SUCCESS;
}

VOID _wfp_start ()
{
	if (config.hengine || !config.is_admin)
		return;

	FWPM_SESSION session = {0};

	session.displayData.name = APP_NAME;
	session.displayData.description = APP_NAME;

	DWORD result = FwpmEngineOpen (nullptr, RPC_C_AUTHN_WINNT, nullptr, nullptr, &config.hengine);

	if (result != ERROR_SUCCESS)
	{
		WDBG (L"FwpmEngineOpen failed. Return value: 0x%.8lx.", result);
	}
	else
	{
		// configure net events (win7 and above)
		_wfp_setevents (TRUE, &session.sessionKey);

		result = FwpmTransactionBegin (config.hengine, 0);

		if (result != ERROR_SUCCESS)
		{
			WDBG (L"FwpmTransactionBegin failed. Return value: 0x%.8lx.", result);
		}
		else
		{
			FWPM_PROVIDER provider = {0};

			provider.displayData.name = APP_NAME;
			provider.displayData.description = APP_NAME;

			provider.providerKey = GUID_WfpProvider;
			provider.flags = FWPM_PROVIDER_FLAG_PERSISTENT;

			result = FwpmProviderAdd (config.hengine, &provider, nullptr);

			if (result != ERROR_SUCCESS && result != FWP_E_ALREADY_EXISTS)
			{
				WDBG (L"FwpmProviderAdd failed. Return value: 0x%.8lx.", result);
				FwpmTransactionAbort (config.hengine);
			}
			else
			{
				FWPM_SUBLAYER sublayer = {0};

				sublayer.displayData.name = APP_NAME;
				sublayer.displayData.description = APP_NAME;

				sublayer.providerKey = (LPGUID)&GUID_WfpProvider;
				sublayer.subLayerKey = GUID_WfpSublayer;
				sublayer.flags = FWPM_SUBLAYER_FLAG_PERSISTENT;
				sublayer.weight = (UINT16)app.ConfigGet (L"SublayerWeight", 0x0000ffff).AsUint ();

				result = FwpmSubLayerAdd (config.hengine, &sublayer, nullptr);

				if (result != ERROR_SUCCESS && result != FWP_E_ALREADY_EXISTS)
				{
					WDBG (L"FwpmSubLayerAdd failed. Return value: 0x%.8lx.", result);
					FwpmTransactionAbort (config.hengine);
				}
				else
				{
					// create outbound callouts
					_wfp_createcallout (config.hengine, FWPM_LAYER_ALE_AUTH_CONNECT_V4, GUID_WfpOutboundCallout4);
					_wfp_createcallout (config.hengine, FWPM_LAYER_ALE_AUTH_CONNECT_V6, GUID_WfpOutboundCallout6);

					// create inbound callouts
					_wfp_createcallout (config.hengine, FWPM_LAYER_ALE_AUTH_RECV_ACCEPT_V4, GUID_WfpInboundCallout4);
					_wfp_createcallout (config.hengine, FWPM_LAYER_ALE_AUTH_RECV_ACCEPT_V6, GUID_WfpInboundCallout6);

					result = FwpmTransactionCommit (config.hengine);

					if (result != ERROR_SUCCESS)
					{
						WDBG (L"FwpmTransactionCommit failed. Return value: 0x%.8lx.", result);
					}
					else
					{
						if (!config.hthread)
							config.hthread = (HANDLE)_beginthreadex (nullptr, 0, &ApplyThread, nullptr, 0, nullptr);

						app.SetIcon (IDI_MAIN);
						app.TraySetInfo (_r_loadicon (app.GetHINSTANCE (), MAKEINTRESOURCE (IDI_MAIN), GetSystemMetrics (SM_CXSMICON)), APP_NAME);

						SetEvent (config.apply2_evt); // apply filters

						return;
					}
				}
			}
		}
	}

	app.TrayPopup (NIIF_ERROR, APP_NAME, I18N (&app, IDS_STATUS_FILTERS_FAILED, 0));
}

VOID _wfp_stop ()
{
	DWORD result = 0;

	if (config.hengine)
	{
		_wfp_setevents (FALSE, nullptr);

		// start transaction
		result = FwpmTransactionBegin (config.hengine, 0);

		if (result != ERROR_SUCCESS)
		{
			WDBG (L"FwpmTransactionBegin failed. Return value: 0x%.8lx.", result);
		}
		else
		{
			if (config.hthread)
			{
				SetEvent (config.stop_evt); // stop thread
				config.hthread = nullptr;
			}

			_wfp_destroyfilters ();

			FwpmCalloutDeleteByKey (config.hengine, &GUID_WfpOutboundCallout4);
			FwpmCalloutDeleteByKey (config.hengine, &GUID_WfpOutboundCallout6);

			FwpmCalloutDeleteByKey (config.hengine, &GUID_WfpInboundCallout4);
			FwpmCalloutDeleteByKey (config.hengine, &GUID_WfpInboundCallout6);

			FwpmSubLayerDeleteByKey (config.hengine, &GUID_WfpSublayer);

			result = FwpmProviderDeleteByKey (config.hengine, &GUID_WfpProvider);

			if (result != ERROR_SUCCESS)
			{
				WDBG (L"FwpmProviderDeleteByKey failed. Return value: 0x%.8lx.", result);
			}

			FwpmTransactionCommit (config.hengine);
		}

		FwpmEngineClose (config.hengine);
		config.hengine = nullptr;

		app.SetIcon (IDI_INACTIVE);
		app.TraySetInfo (_r_loadicon (app.GetHINSTANCE (), MAKEINTRESOURCE (IDI_INACTIVE), GetSystemMetrics (SM_CXSMICON)), APP_NAME);

		if (app.ConfigGet (L"ShowBalloonTips", TRUE).AsBool ())
			app.TrayPopup (NIIF_INFO, APP_NAME, I18N (&app, IDS_STATUS_FILTERS_REMOVED, 0));
	}
}

VOID addrule (LPCWSTR locale_sid, UINT locale_id, LPCWSTR cfg, BOOL defval, size_t group_id)
{
	ITEM_RULE rule;

	rule.is_enabled = defval;
	rule.group_id = group_id;

	rule.locale_id = locale_id;
	StringCchCopy (rule.locale_sid, _countof (rule.locale_sid), locale_sid);

	StringCchCopy (rule.config, _countof (rule.config), cfg);

	rules.push_back (rule);
}

VOID addcolor (LPCWSTR locale_sid, UINT locale_id, LPCWSTR cfg, BOOL is_enabled, LPCWSTR config_color, COLORREF default_clr)
{
	ITEM_COLOR color;

	StringCchCopy (color.config, _countof (color.config), cfg);
	StringCchCopy (color.config_color, _countof (color.config_color), config_color);

	color.is_enabled = is_enabled;
	color.default_clr = default_clr;

	color.locale_id = locale_id;
	StringCchCopy (color.locale_sid, _countof (color.locale_sid), locale_sid);

	colors.push_back (color);
}

VOID _app_getprocesslist (HWND hwnd, std::vector<ITEM_PROCESS>* pvc)
{
	if (!config.is_admin)
		return;

	if (pvc)
	{
		for (size_t i = 0; i < pvc->size (); i++)
			DeleteObject (pvc->at (i).hbmp); // free memory

		pvc->clear ();
	}

	NTSTATUS status = 0;

	OBJECT_ATTRIBUTES oa = {0};

	ULONG length = 0x4000;
	PVOID buffer = malloc (length);

	while (TRUE)
	{
		status = NtQuerySystemInformation (SystemProcessInformation, buffer, length, &length);

		if (status == 0xC0000023L /*STATUS_BUFFER_TOO_SMALL*/ || status == 0xc0000004 /*STATUS_INFO_LENGTH_MISMATCH*/)
		{
			PVOID buffer_new = realloc (buffer, length);

			if (!buffer_new)
			{
				break;
			}
			else
			{
				buffer = buffer_new;
			}
		}
		else
		{
			break;
		}
	}

	if (NT_SUCCESS (status))
	{
		PSYSTEM_PROCESS_INFORMATION spi = (PSYSTEM_PROCESS_INFORMATION)buffer;

		std::unordered_map<size_t, BOOL> checker;
		DWORD size = 0;

		do
		{
			HANDLE hprocess = nullptr;
			CLIENT_ID client_id = {0};

			client_id.UniqueProcess = spi->UniqueProcessId;

			InitializeObjectAttributes (&oa, nullptr, 0, nullptr, nullptr);

			if (NT_SUCCESS (NtOpenProcess (&hprocess, PROCESS_QUERY_INFORMATION, &oa, &client_id)))
			{
				WCHAR path[MAX_PATH] = {0};

				size = _countof (path);

				if (QueryFullProcessImageName (hprocess, 0, path, &size))
				{
					const size_t hash = rstring (path).Hash ();

					if (applications.find (hash) == applications.end () && checker.find (hash) == checker.end ())
					{
						checker[hash] = TRUE;

						if (!pvc)
						{
							if (_wcsnicmp (path, config.windows_dir, config.wd_length) != 0)
								_app_addapplication (hwnd, path, FALSE);
						}
						else
						{
							ITEM_PROCESS item;
							SecureZeroMemory (&item, sizeof (item));

							StringCchCopy (item.file_path, _countof (item.file_path), path);

							SHFILEINFO shfi = {0};
							SHGetFileInfo (item.file_path, 0, &shfi, sizeof (shfi), SHGFI_SMALLICON | SHGFI_ICON);

							RECT rc = {0};
							rc.right = GetSystemMetrics (SM_CXSMICON);
							rc.bottom = GetSystemMetrics (SM_CYSMICON);

							HDC hdc = GetDC (nullptr);
							HDC hmemdc = CreateCompatibleDC (hdc);
							HBITMAP hbmp = CreateCompatibleBitmap (hdc, rc.right, rc.bottom);
							ReleaseDC (nullptr, hdc);

							HGDIOBJ old_bmp = SelectObject (hmemdc, hbmp);
							_r_wnd_fillrect (hmemdc, &rc, GetSysColor (COLOR_MENU));
							DrawIconEx (hmemdc, 0, 0, shfi.hIcon, GetSystemMetrics (SM_CXSMICON), GetSystemMetrics (SM_CYSMICON), 0, nullptr, DI_NORMAL);
							SelectObject (hmemdc, old_bmp);

							DeleteDC (hmemdc);
							DestroyIcon (shfi.hIcon);

							item.hbmp = hbmp;

							PathCompactPathEx (item.display_path, item.file_path, _countof (item.display_path), 0);

							pvc->push_back (item);
						}
					}
				}
				else
				{
					WDBG (L"QueryFullProcessImageName failed: %s", spi->ImageName.Buffer);
				}

				NtClose (hprocess);
			}
			else
			{
				//WDBG (L"NtOpenProcess failed: %s", spi->ImageName.Buffer);
			}
		}
		while ((spi = ((spi->NextEntryOffset ? (PSYSTEM_PROCESS_INFORMATION)((PCHAR)(spi)+(spi)->NextEntryOffset) : nullptr))) != nullptr);
	}
	else
	{
		WDBG (L"NtQuerySystemInformation failed.");
	}

	free (buffer); // free the allocated buffer.
}

VOID _app_profileload (HWND hwnd)
{
	// clear all
	applications.clear ();
	colors.clear ();
	rules.clear ();

	_r_listview_deleteallitems (hwnd, IDC_LISTVIEW);

	// load applications
	pugi::xml_document doc;

	if (doc.load_file (config.config_path, pugi::parse_escapes, pugi::encoding_utf16_le))
	{
		pugi::xml_node root = doc.child (L"root");

		if (root)
		{
			for (pugi::xml_node item = root.child (L"item"); item; item = item.next_sibling (L"item"))
				_app_addapplication (hwnd, item.attribute (L"path").as_string (), item.attribute (L"is_enabled").as_bool ());
		}
	}
	else
	{
		_app_getprocesslist (hwnd, nullptr);
	}

	// load default rules
	addrule (L"IDS_RULE_DHCP", IDS_RULE_DHCP, L"AllowDhcpService", TRUE, 0);
	addrule (L"IDS_RULE_DNS", IDS_RULE_DNS, L"AllowDnsService", TRUE, 0);
	addrule (L"IDS_RULE_NETWORKDISCOVERY", IDS_RULE_NETWORKDISCOVERY, L"AllowNetworkDiscoveryService", TRUE, 0);
	addrule (L"IDS_RULE_NTP", IDS_RULE_NTP, L"AllowNtpService", TRUE, 0);
	addrule (L"IDS_RULE_SNMP", IDS_RULE_SNMP, L"AllowSnmpService", FALSE, 0);
	addrule (L"IDS_RULE_SSDP", IDS_RULE_SSDP, L"AllowSsdpService", TRUE, 0);
	addrule (L"IDS_RULE_WUAUSERV", IDS_RULE_WUAUSERV, L"AllowWindowUpdateService", FALSE, 0);

	addrule (L"IDS_RULE_OUTBOUND_ICMP", IDS_RULE_OUTBOUND_ICMP, L"AllowOutboundIcmp", TRUE, 1);
	addrule (L"IDS_RULE_INBOUND_ICMP", IDS_RULE_INBOUND_ICMP, L"AllowInboundIcmp", FALSE, 1);
	addrule (L"IDS_RULE_INBOUND", IDS_RULE_INBOUND, L"AllowInboundConnections", FALSE, 1);

	addcolor (L"IDS_HIGHLIGHT_SYSTEM", IDS_HIGHLIGHT_SYSTEM, L"IsHighlightSystem", TRUE, L"ColorSystem", COLOR_SYSTEM);
	addcolor (L"IDS_HIGHLIGHT_INVALID", IDS_HIGHLIGHT_INVALID, L"IsHighlightInvalid", TRUE, L"ColorInvalid", COLOR_INVALID);
}

BOOL initializer_callback (HWND hwnd, DWORD msg, LPVOID, LPVOID)
{
	switch (msg)
	{
		case _RM_INITIALIZE:
		{
			// set window icon indicator
			app.SetIcon (app.ConfigGet (L"IsFiltersEnabled", TRUE).AsBool () ? IDI_MAIN : IDI_INACTIVE);

			// init tray icon
			app.TrayCreate (UID, WM_TRAYICON, _r_loadicon (app.GetHINSTANCE (), MAKEINTRESOURCE (app.ConfigGet (L"IsFiltersEnabled", TRUE).AsBool () ? IDI_MAIN : IDI_INACTIVE), GetSystemMetrics (SM_CXSMICON)));

			if (!config.hengine && app.ConfigGet (L"IsFiltersEnabled", TRUE).AsBool ())
			{
				_wfp_start (); // start firewall
			}
			else
			{
				SetEvent (config.apply2_evt); // apply filters
			}

			CheckMenuItem (GetMenu (hwnd), IDM_ALWAYSONTOP_CHK, MF_BYCOMMAND | (app.ConfigGet (L"AlwaysOnTop", FALSE).AsBool () ? MF_CHECKED : MF_UNCHECKED));

			break;
		}

		case _RM_LOCALIZE:
		{
			HMENU menu = GetMenu (hwnd);

			app.LocaleMenu (menu, I18N (&app, IDS_FILE, 0), 0, TRUE);
			app.LocaleMenu (menu, I18N (&app, IDS_SETTINGS, 0), IDM_SETTINGS, FALSE);
			app.LocaleMenu (menu, I18N (&app, IDS_EXIT, 0), IDM_EXIT, FALSE);

			app.LocaleMenu (menu, I18N (&app, IDS_EDIT, 0), 1, TRUE);

			app.LocaleMenu (menu, I18N (&app, IDS_FIND, 0), IDM_FIND, FALSE);
			app.LocaleMenu (menu, I18N (&app, IDS_FINDNEXT, 0), IDM_FINDNEXT, FALSE);

			app.LocaleMenu (menu, I18N (&app, IDS_REFRESH, 0), IDM_REFRESH, FALSE);

			app.LocaleMenu (menu, I18N (&app, IDS_SETTINGS, 0), 2, TRUE);

			app.LocaleMenu (menu, I18N (&app, IDS_ALWAYSONTOP_CHK, 0), IDM_ALWAYSONTOP_CHK, FALSE);

			app.LocaleMenu (GetSubMenu (menu, 2), I18N (&app, IDS_ICONS, 0), 2, TRUE);
			app.LocaleMenu (menu, I18N (&app, IDS_ICONSSMALL, 0), IDM_ICONSSMALL, FALSE);
			app.LocaleMenu (menu, I18N (&app, IDS_ICONSLARGE, 0), IDM_ICONSLARGE, FALSE);

			app.LocaleMenu (GetSubMenu (menu, 2), I18N (&app, IDS_SORT, 0), 3, TRUE);
			app.LocaleMenu (menu, I18N (&app, IDS_SORTBYFNAME, 0), IDM_SORTBYFNAME, FALSE);
			app.LocaleMenu (menu, I18N (&app, IDS_SORTBYFDIR, 0), IDM_SORTBYFDIR, FALSE);
			app.LocaleMenu (menu, I18N (&app, IDS_SORTISDESCEND, 0), IDM_SORTISDESCEND, FALSE);

			app.LocaleMenu (GetSubMenu (menu, 2), I18N (&app, IDS_LANGUAGE, 0), 5, TRUE);

			app.LocaleMenu (menu, I18N (&app, IDS_HELP, 0), 3, TRUE);
			app.LocaleMenu (menu, I18N (&app, IDS_WEBSITE, 0), IDM_WEBSITE, FALSE);
			app.LocaleMenu (menu, I18N (&app, IDS_DONATE, 0), IDM_DONATE, FALSE);
			app.LocaleMenu (menu, I18N (&app, IDS_CHECKUPDATES, 0), IDM_CHECKUPDATES, FALSE);
			app.LocaleMenu (menu, I18N (&app, IDS_ABOUT, 0), IDM_ABOUT, FALSE);

			app.LocaleEnum ((HWND)GetSubMenu (menu, 2), 5, TRUE, IDM_DEFAULT); // enum localizations

			_app_refreshstatus (hwnd); // refresh statusbar

			break;
		}

		case _RM_UNINITIALIZE:
		{
			app.TrayDestroy (UID);
			break;
		}
	}

	return FALSE;
}

INT_PTR CALLBACK EditorProc (HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
	static ITEM_RULE_EXT* ptr = nullptr;

	switch (msg)
	{
		case WM_INITDIALOG:
		{
			ptr = (ITEM_RULE_EXT*)lparam;

			// configure window
			_r_wnd_center (hwnd);

			// localize window
			SetWindowText (hwnd, I18N (&app, IDS_EDITOR, 0));

			SetDlgItemText (hwnd, IDC_NAME, I18N (&app, IDS_NAME, 0));
			SetDlgItemText (hwnd, IDC_DIRECTION, I18N (&app, IDS_DIRECTION, 0));
			SetDlgItemText (hwnd, IDC_ACTION, I18N (&app, IDS_ACTION, 0));
			SetDlgItemText (hwnd, IDC_RULES, I18N (&app, IDS_RULE, 0));

			SetDlgItemText (hwnd, IDC_APPLY, I18N (&app, IDS_APPLY, 0));
			SetDlgItemText (hwnd, IDC_CLOSE, I18N (&app, IDS_CLOSE, 0));

			_r_wnd_addstyle (hwnd, IDC_APPLY, app.IsClassicUI () ? WS_EX_STATICEDGE : 0, WS_EX_STATICEDGE, GWL_EXSTYLE);
			_r_wnd_addstyle (hwnd, IDC_CLOSE, app.IsClassicUI () ? WS_EX_STATICEDGE : 0, WS_EX_STATICEDGE, GWL_EXSTYLE);

			// set data
			SetDlgItemText (hwnd, IDC_NAME_EDIT, ptr->name);
			SetDlgItemText (hwnd, IDC_RULES_EDIT, ptr->rule);

			SendDlgItemMessage (hwnd, IDC_DIRECTION_EDIT, CB_INSERTSTRING, 0, (LPARAM)I18N (&app, IDS_DIRECTION_1, 0).GetString ());
			SendDlgItemMessage (hwnd, IDC_DIRECTION_EDIT, CB_INSERTSTRING, 1, (LPARAM)I18N (&app, IDS_DIRECTION_2, 0).GetString ());
			SendDlgItemMessage (hwnd, IDC_DIRECTION_EDIT, CB_INSERTSTRING, 2, (LPARAM)I18N (&app, IDS_DIRECTION_3, 0).GetString ());

			SendDlgItemMessage (hwnd, IDC_DIRECTION_EDIT, CB_SETCURSEL, (WPARAM)ptr->direction, 0);

			SendDlgItemMessage (hwnd, IDC_ACTION_EDIT, CB_INSERTSTRING, 0, (LPARAM)I18N (&app, IDS_ACTION_1, 0).GetString ());
			SendDlgItemMessage (hwnd, IDC_ACTION_EDIT, CB_INSERTSTRING, 1, (LPARAM)I18N (&app, IDS_ACTION_2, 0).GetString ());

			SendDlgItemMessage (hwnd, IDC_ACTION_EDIT, CB_SETCURSEL, ptr->is_block, 0);

			// set limitation
			SendDlgItemMessage (hwnd, IDC_NAME_EDIT, EM_LIMITTEXT, _countof (ptr->name) - 1, 0);
			SendDlgItemMessage (hwnd, IDC_RULES_EDIT, EM_LIMITTEXT, _countof (ptr->rule) - 1, 0);

			_r_ctrl_enable (hwnd, IDC_APPLY, FALSE); // disable apply button

			break;
		}

		case WM_COMMAND:
		{
			if (HIWORD (wparam) == EN_CHANGE || HIWORD (wparam) == CBN_SELENDOK)
			{
				BOOL is_enable = SendDlgItemMessage (hwnd, IDC_NAME_EDIT, WM_GETTEXTLENGTH, 0, 0) > 0 &&
					SendDlgItemMessage (hwnd, IDC_RULES_EDIT, WM_GETTEXTLENGTH, 0, 0);

				_r_ctrl_enable (hwnd, IDC_APPLY, is_enable); // enable apply button

				return FALSE;
			}

			switch (LOWORD (wparam))
			{
				case IDOK: // process Enter key
				case IDC_APPLY:
				{
					StringCchCopy (ptr->name, _countof (ptr->name), _r_ctrl_gettext (hwnd, IDC_NAME_EDIT));
					StringCchCopy (ptr->rule, _countof (ptr->rule), _r_ctrl_gettext (hwnd, IDC_RULES_EDIT));

					ptr->direction = (EnumDirection)SendDlgItemMessage (hwnd, IDC_DIRECTION_EDIT, CB_GETCURSEL, 0, 0);
					ptr->is_block = (BOOL)SendDlgItemMessage (hwnd, IDC_ACTION_EDIT, CB_GETCURSEL, 0, 0);

					EndDialog (hwnd, 1);
					break;
				}

				case IDCANCEL: // process Esc key
				case IDC_CLOSE:
				{
					EndDialog (hwnd, 0);
					break;
				}
			}

			break;
		}
	}

	return FALSE;
}

VOID SetIconsSize (HWND hwnd)
{
	HIMAGELIST h = nullptr;
	RECT rc = {0};

	BOOL is_large = app.ConfigGet (L"IsLargeIcons", TRUE).AsBool ();

	if (SUCCEEDED (SHGetImageList (is_large ? SHIL_LARGE : SHIL_SMALL, IID_IImageList, (LPVOID*)&h)))
	{
		SendDlgItemMessage (hwnd, IDC_LISTVIEW, LVM_SETIMAGELIST, LVSIL_SMALL, (LPARAM)h);
		SendDlgItemMessage (hwnd, IDC_LISTVIEW, LVM_SETIMAGELIST, LVSIL_NORMAL, (LPARAM)h);
	}

	SendDlgItemMessage (hwnd, IDC_LISTVIEW, LVM_SCROLL, 0, GetScrollPos (hwnd, SB_VERT)); // scroll-resize-HACK!!!

	CheckMenuRadioItem (GetMenu (hwnd), IDM_ICONSSMALL, IDM_ICONSLARGE, (is_large ? IDM_ICONSLARGE : IDM_ICONSSMALL), MF_BYCOMMAND);

	_r_listview_resizeonecolumn (hwnd, IDC_LISTVIEW);
}

VOID ShowItem (HWND hwnd, UINT ctrl_id, size_t item)
{
	ListView_SetItemState (GetDlgItem (hwnd, ctrl_id), -1, 0, LVIS_SELECTED); // deselect all
	ListView_SetItemState (GetDlgItem (hwnd, ctrl_id), item, LVIS_SELECTED, LVIS_SELECTED); // select item

	SendDlgItemMessage (hwnd, ctrl_id, LVM_ENSUREVISIBLE, item, TRUE); // ensure him visible
}

VOID ResizeWindow (HWND hwnd, INT width, INT height)
{
	SetWindowPos (GetDlgItem (hwnd, IDC_LISTVIEW), nullptr, 0, 0, width, height - config.sb_height - 1, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREPOSITION);

	// resize statusbar parts
	INT parts[] = {_R_PERCENT_VAL (50, width), -1};
	SendDlgItemMessage (hwnd, IDC_STATUSBAR, SB_SETPARTS, 2, (LPARAM)parts);

	// resize column width
	_r_listview_resizeonecolumn (hwnd, IDC_LISTVIEW);

	// resize statusbar
	SendDlgItemMessage (hwnd, IDC_STATUSBAR, WM_SIZE, 0, 0);
}

BOOL settings_callback (HWND hwnd, DWORD msg, LPVOID lpdata1, LPVOID lpdata2)
{
	PAPPLICATION_PAGE page = (PAPPLICATION_PAGE)lpdata2;

	switch (msg)
	{
		case _RM_INITIALIZE:
		{
			switch (page->dlg_id)
			{
				case IDD_SETTINGS_1:
				{
					// localize
					SetDlgItemText (hwnd, IDC_TITLE_1, I18N (&app, IDS_TITLE_1, 0));
					SetDlgItemText (hwnd, IDC_TITLE_2, I18N (&app, IDS_TITLE_2, 0));

					SetDlgItemText (hwnd, IDC_ALWAYSONTOP_CHK, I18N (&app, IDS_ALWAYSONTOP_CHK, 0));
					SetDlgItemText (hwnd, IDC_LOADONSTARTUP_CHK, I18N (&app, IDS_LOADONSTARTUP_CHK, 0));
					SetDlgItemText (hwnd, IDC_STARTMINIMIZED_CHK, I18N (&app, IDS_STARTMINIMIZED_CHK, 0));
					SetDlgItemText (hwnd, IDC_SKIPUACWARNING_CHK, I18N (&app, IDS_SKIPUACWARNING_CHK, 0));
					SetDlgItemText (hwnd, IDC_CHECKUPDATES_CHK, I18N (&app, IDS_CHECKUPDATES_CHK, 0));

					SetDlgItemText (hwnd, IDC_LANGUAGE_HINT, I18N (&app, IDS_LANGUAGE_HINT, 0));

					if (!config.is_admin)
						_r_ctrl_enable (hwnd, IDC_SKIPUACWARNING_CHK, FALSE);

					CheckDlgButton (hwnd, IDC_ALWAYSONTOP_CHK, app.ConfigGet (L"AlwaysOnTop", FALSE).AsBool () ? BST_CHECKED : BST_UNCHECKED);
					CheckDlgButton (hwnd, IDC_LOADONSTARTUP_CHK, app.AutorunIsPresent () ? BST_CHECKED : BST_UNCHECKED);
					CheckDlgButton (hwnd, IDC_STARTMINIMIZED_CHK, app.ConfigGet (L"StartMinimized", TRUE).AsBool () ? BST_CHECKED : BST_UNCHECKED);
#ifdef _APP_HAVE_SKIPUAC
					if (config.is_admin)
						CheckDlgButton (hwnd, IDC_SKIPUACWARNING_CHK, app.SkipUacIsPresent (FALSE) ? BST_CHECKED : BST_UNCHECKED);
#endif // _APP_HAVE_SKIPUAC
					CheckDlgButton (hwnd, IDC_CHECKUPDATES_CHK, app.ConfigGet (L"CheckUpdates", TRUE).AsBool () ? BST_CHECKED : BST_UNCHECKED);

					app.LocaleEnum (hwnd, IDC_LANGUAGE, FALSE, 0);

					SetWindowLongPtr (hwnd, GWLP_USERDATA, (LONG_PTR)SendDlgItemMessage (hwnd, IDC_LANGUAGE, CB_GETCURSEL, 0, 0)); // check on save

					break;
				}

				case IDD_SETTINGS_2:
				{
					// localize
					SetDlgItemText (hwnd, IDC_TITLE_1, I18N (&app, IDS_TITLE_1, 0));
					SetDlgItemText (hwnd, IDC_TITLE_3, I18N (&app, IDS_TITLE_3, 0));

					SetDlgItemText (hwnd, IDC_USEBLOCKLIST_CHK, I18N (&app, IDS_USEBLOCKLIST_CHK, 0));
					SetDlgItemText (hwnd, IDC_DONOTBLOCKMAINEXECUTABLE_CHK, I18N (&app, IDS_DONOTBLOCKMAINEXECUTABLE_CHK, 0));

					SetDlgItemText (hwnd, IDC_DISABLEWINDOWSFIREWALL_CHK, I18N (&app, IDS_DISABLEWINDOWSFIREWALL_CHK, 0));

					if (!config.is_admin)
						_r_ctrl_enable (hwnd, IDC_DISABLEWINDOWSFIREWALL_CHK, FALSE);

					CheckDlgButton (hwnd, IDC_USEBLOCKLIST_CHK, app.ConfigGet (L"UseBlocklist", TRUE).AsBool () ? BST_CHECKED : BST_UNCHECKED);
					CheckDlgButton (hwnd, IDC_DONOTBLOCKMAINEXECUTABLE_CHK, app.ConfigGet (L"DoNotBlockMainExecutable", TRUE).AsBool () ? BST_CHECKED : BST_UNCHECKED);

					if (config.is_admin)
						CheckDlgButton (hwnd, IDC_DISABLEWINDOWSFIREWALL_CHK, (changeservicestate (MPS_SERVICE1, FALSE, 0) == SERVICE_DISABLED && changeservicestate (MPS_SERVICE2, FALSE, 0) == SERVICE_DISABLED) ? BST_CHECKED : BST_UNCHECKED);

					break;
				}

				case IDD_SETTINGS_3:
				{
					// localize
					SetDlgItemText (hwnd, IDC_TITLE_4, I18N (&app, IDS_TITLE_4, 0));
					SetDlgItemText (hwnd, IDC_TITLE_5, I18N (&app, IDS_TITLE_5, 0));

					SetDlgItemText (hwnd, IDC_SHOWBALLOONTIPS_CHK, I18N (&app, IDS_SHOWBALLOONTIPS_CHK, 0));

					_r_listview_setstyle (hwnd, IDC_COLORS, LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP | LVS_EX_LABELTIP | LVS_EX_CHECKBOXES);

					_r_listview_deleteallitems (hwnd, IDC_COLORS);
					_r_listview_deleteallcolumns (hwnd, IDC_COLORS);

					_r_listview_addcolumn (hwnd, IDC_COLORS, nullptr, 95, 0, LVCFMT_LEFT);

					for (size_t i = 0; i < colors.size (); i++)
					{
						colors.at (i).clr = app.ConfigGet (colors.at (i).config_color, colors.at (i).default_clr).AsUlong ();

						_r_listview_additem (hwnd, IDC_COLORS, I18N (&app, colors.at (i).locale_id, colors.at (i).locale_sid), LAST_VALUE, 0, LAST_VALUE, LAST_VALUE, i);

						if (app.ConfigGet (colors.at (i).config, colors.at (i).is_enabled).AsBool ())
							_r_listview_setcheckstate (hwnd, IDC_COLORS, LAST_VALUE, TRUE);
					}

					_r_listview_resizeonecolumn (hwnd, IDC_COLORS);

					CheckDlgButton (hwnd, IDC_SHOWBALLOONTIPS_CHK, app.ConfigGet (L"ShowBalloonTips", TRUE).AsBool () ? BST_CHECKED : BST_UNCHECKED);

					break;
				}

				case IDD_SETTINGS_4:
				{
					// configure listview
					_r_listview_setstyle (hwnd, IDC_COMMON, LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP | LVS_EX_LABELTIP | LVS_EX_CHECKBOXES);

					_r_listview_deleteallgroups (hwnd, IDC_COMMON);
					_r_listview_deleteallitems (hwnd, IDC_COMMON);
					_r_listview_deleteallcolumns (hwnd, IDC_COMMON);

					_r_listview_addcolumn (hwnd, IDC_COMMON, nullptr, 95, 0, LVCFMT_LEFT);

					_r_listview_addgroup (hwnd, IDC_COMMON, 0, I18N (&app, IDS_GROUP_1, 0));
					_r_listview_addgroup (hwnd, IDC_COMMON, 1, I18N (&app, IDS_GROUP_2, 0));

					for (size_t i = 0; i < rules.size (); i++)
					{
						_r_listview_additem (hwnd, IDC_COMMON, I18N (&app, rules.at (i).locale_id, rules.at (i).locale_sid), i, 0, LAST_VALUE, rules.at (i).group_id, i);

						_r_listview_setcheckstate (hwnd, IDC_COMMON, i, app.ConfigGet (rules.at (i).config, rules.at (i).is_enabled).AsBool ());
					}

					_r_listview_resizeonecolumn (hwnd, IDC_COMMON);

					break;
				}

				case IDD_SETTINGS_5:
				case IDD_SETTINGS_6:
				{
					// configure listview
					_r_listview_setstyle (hwnd, IDC_EDITOR, LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP | LVS_EX_LABELTIP | LVS_EX_CHECKBOXES);

					size_t item = (size_t)SendDlgItemMessage (hwnd, IDC_EDITOR, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_SELECTED);

					_r_listview_deleteallitems (hwnd, IDC_EDITOR);
					_r_listview_deleteallcolumns (hwnd, IDC_EDITOR);

					_r_listview_addcolumn (hwnd, IDC_EDITOR, I18N (&app, IDS_NAME, 0), 35, 0, LVCFMT_LEFT);
					_r_listview_addcolumn (hwnd, IDC_EDITOR, I18N (&app, IDS_RULE, 0), 40, 1, LVCFMT_LEFT);
					_r_listview_addcolumn (hwnd, IDC_EDITOR, I18N (&app, IDS_ACTION, 0), 20, 2, LVCFMT_LEFT);

					for (size_t i = 0, j = 0; i < rules_ext.size (); i++)
					{
						if (
							rules_ext.at (i).is_default ||
							(page->dlg_id == IDD_SETTINGS_5 && rules_ext.at (i).is_port == FALSE) ||
							(page->dlg_id == IDD_SETTINGS_6 && rules_ext.at (i).is_port == TRUE)
							)
							continue;

						_r_listview_additem (hwnd, IDC_EDITOR, rules_ext.at (i).name, j, 0, LAST_VALUE, LAST_VALUE, i);
						_r_listview_additem (hwnd, IDC_EDITOR, rules_ext.at (i).rule, j, 1, LAST_VALUE, LAST_VALUE, i);

						if (rules_ext.at (i).is_block)
							_r_listview_additem (hwnd, IDC_EDITOR, I18N (&app, IDS_ACTION_2, 0), j, 2, LAST_VALUE, LAST_VALUE, i);
						else
							_r_listview_additem (hwnd, IDC_EDITOR, I18N (&app, IDS_ACTION_1, 0), j, 2, LAST_VALUE, LAST_VALUE, i);

						_r_listview_setcheckstate (hwnd, IDC_EDITOR, j, rules_ext.at (i).is_enabled);

						j += 1;
					}

					if (item != LAST_VALUE)
						ShowItem (hwnd, IDC_EDITOR, item);

					break;
				}
			}

			break;
		}

		case _RM_MESSAGE:
		{
			LPMSG pmsg = (LPMSG)lpdata1;

			switch (pmsg->message)
			{
				case WM_NOTIFY:
				{
					LPNMHDR nmlp = (LPNMHDR)pmsg->lParam;

					switch (nmlp->code)
					{
						case NM_CUSTOMDRAW:
						{
							LONG result = CDRF_DODEFAULT;
							LPNMLVCUSTOMDRAW lpnmlv = (LPNMLVCUSTOMDRAW)pmsg->lParam;

							switch (lpnmlv->nmcd.dwDrawStage)
							{
								case CDDS_PREPAINT:
								{
									result = CDRF_NOTIFYITEMDRAW;
									break;
								}

								case CDDS_ITEMPREPAINT:
								{
									if (nmlp->idFrom == IDC_COLORS)
									{
										size_t idx = lpnmlv->nmcd.lItemlParam;

										_r_wnd_fillrect (lpnmlv->nmcd.hdc, &lpnmlv->nmcd.rc, colors.at (idx).clr);
										lpnmlv->clrTextBk = colors.at (idx).clr;

										result = CDRF_NEWFONT;
									}

									break;
								}
							}

							SetWindowLongPtr (hwnd, DWLP_MSGRESULT, result);
							return TRUE;
						}

						case NM_DBLCLK:
						{
							LPNMITEMACTIVATE lpnmlv = (LPNMITEMACTIVATE)pmsg->lParam;

							if (lpnmlv->iItem != -1)
							{
								if (nmlp->idFrom == IDC_COLORS)
								{
									size_t idx = _r_listview_getlparam (hwnd, IDC_COLORS, lpnmlv->iItem);

									CHOOSECOLOR cc = {0};
									COLORREF cust[16] = {COLOR_SYSTEM, COLOR_INVALID};

									cc.lStructSize = sizeof (cc);
									cc.Flags = CC_RGBINIT | CC_FULLOPEN;
									cc.hwndOwner = hwnd;
									cc.lpCustColors = cust;
									cc.rgbResult = colors.at (idx).clr;

									if (ChooseColor (&cc))
									{
										colors.at (idx).clr = cc.rgbResult;

										// send notify for apply button (required!)
										BOOL prev_state = _r_listview_getcheckstate (hwnd, IDC_COLORS, lpnmlv->iItem);

										_r_listview_setcheckstate (hwnd, IDC_COLORS, lpnmlv->iItem, !prev_state);
										_r_listview_setcheckstate (hwnd, IDC_COLORS, lpnmlv->iItem, prev_state);
									}
								}
								else if (nmlp->idFrom == IDC_EDITOR)
								{
									SendMessage (hwnd, WM_COMMAND, MAKELPARAM (IDM_EDIT, 0), 0);
								}
							}

							break;
						}
					}

					break;
				}

				case WM_CONTEXTMENU:
				{
					UINT ctrl_id = GetDlgCtrlID ((HWND)pmsg->wParam);

					if (ctrl_id == IDC_EDITOR)
					{
						HMENU menu = LoadMenu (nullptr, MAKEINTRESOURCE (IDM_EDITOR)), submenu = GetSubMenu (menu, 0);

						// localize
						app.LocaleMenu (submenu, I18N (&app, IDS_ADD, 0), IDM_ADD, FALSE);
						app.LocaleMenu (submenu, I18N (&app, IDS_EDIT2, 0), IDM_EDIT, FALSE);
						app.LocaleMenu (submenu, I18N (&app, IDS_DELETE, 0), IDM_DELETE, FALSE);

						if (!SendDlgItemMessage (hwnd, ctrl_id, LVM_GETSELECTEDCOUNT, 0, 0))
						{
							EnableMenuItem (submenu, IDM_EDIT, MF_BYCOMMAND | MF_DISABLED);
							EnableMenuItem (submenu, IDM_DELETE, MF_BYCOMMAND | MF_DISABLED);
						}

						POINT pt = {0};
						GetCursorPos (&pt);

						TrackPopupMenuEx (submenu, TPM_RIGHTBUTTON | TPM_LEFTBUTTON, pt.x, pt.y, hwnd, nullptr);

						DestroyMenu (menu);
						DestroyMenu (submenu);
					}
				}

				case WM_COMMAND:
				{
					switch (LOWORD (pmsg->wParam))
					{
						case IDM_ADD:
						{
							ITEM_RULE_EXT* ptr = nullptr;

							ptr = new ITEM_RULE_EXT;
							SecureZeroMemory (ptr, sizeof (ITEM_RULE_EXT));

							if (page->dlg_id == IDD_SETTINGS_5)
								ptr->is_port = TRUE;

							if (DialogBoxParam (nullptr, MAKEINTRESOURCE (IDD_EDITOR), hwnd, &EditorProc, (LPARAM)ptr))
							{
								rules_ext.push_back (*ptr);
								settings_callback (page->hwnd, _RM_INITIALIZE, nullptr, page); // re-inititalize page
							}

							delete ptr;

							break;
						}

						case IDM_EDIT:
						{
							size_t item = (size_t)SendDlgItemMessage (hwnd, IDC_EDITOR, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_SELECTED);

							if (item == LAST_VALUE)
								break;

							ITEM_RULE_EXT* ptr = &rules_ext.at ((size_t)_r_listview_getlparam (hwnd, IDC_EDITOR, item));

							if (DialogBoxParam (nullptr, MAKEINTRESOURCE (IDD_EDITOR), hwnd, &EditorProc, (LPARAM)ptr))
							{
								settings_callback (page->hwnd, _RM_INITIALIZE, nullptr, page); // re-inititalize page
							}

							break;
						}

						case IDM_DELETE:
						{
							if (_r_msg (hwnd, MB_YESNO | MB_ICONQUESTION, APP_NAME, nullptr, I18N (&app, IDS_QUESTION, 0)) == IDYES)
							{
								size_t count = _r_listview_getitemcount (hwnd, IDC_EDITOR) - 1;

								for (size_t i = count; i != LAST_VALUE; i--)
								{
									if (ListView_GetItemState (GetDlgItem (hwnd, IDC_EDITOR), i, LVNI_SELECTED))
									{
										size_t idx = _r_listview_getlparam (hwnd, IDC_EDITOR, i);

										rules_ext.erase (rules_ext.begin () + idx);

										SendDlgItemMessage (hwnd, IDC_EDITOR, LVM_DELETEITEM, i, 0);
									}
								}
							}

							break;
						}
					}

					break;
				}
			}

			break;
		}

		case _RM_SAVE:
		{
			switch (page->dlg_id)
			{
				case IDD_SETTINGS_1:
				{
					app.ConfigSet (L"AlwaysOnTop", DWORD ((IsDlgButtonChecked (hwnd, IDC_ALWAYSONTOP_CHK) == BST_CHECKED) ? TRUE : FALSE));
					app.AutorunCreate (IsDlgButtonChecked (hwnd, IDC_LOADONSTARTUP_CHK) == BST_UNCHECKED);
					app.ConfigSet (L"StartMinimized", DWORD ((IsDlgButtonChecked (hwnd, IDC_STARTMINIMIZED_CHK) == BST_CHECKED) ? TRUE : FALSE));

#ifdef _APP_HAVE_SKIPUAC
					if (!_r_sys_uacstate ())
						app.SkipUacCreate (IsDlgButtonChecked (hwnd, IDC_SKIPUACWARNING_CHK) == BST_UNCHECKED);
#endif // _APP_HAVE_SKIPUAC

					app.ConfigSet (L"CheckUpdates", ((IsDlgButtonChecked (hwnd, IDC_CHECKUPDATES_CHK) == BST_CHECKED) ? TRUE : FALSE));

					// set language
					rstring buffer;

					if (SendDlgItemMessage (hwnd, IDC_LANGUAGE, CB_GETCURSEL, 0, 0) >= 1)
						buffer = _r_ctrl_gettext (hwnd, IDC_LANGUAGE);

					app.ConfigSet (L"Language", buffer);

					if (GetWindowLongPtr (hwnd, GWLP_USERDATA) != (INT)SendDlgItemMessage (hwnd, IDC_LANGUAGE, CB_GETCURSEL, 0, 0))
						return TRUE; // for restart

					break;
				}

				case IDD_SETTINGS_2:
				{
					app.ConfigSet (L"UseBlocklist", DWORD ((IsDlgButtonChecked (hwnd, IDC_USEBLOCKLIST_CHK) == BST_CHECKED) ? TRUE : FALSE));
					app.ConfigSet (L"DoNotBlockMainExecutable", DWORD ((IsDlgButtonChecked (hwnd, IDC_DONOTBLOCKMAINEXECUTABLE_CHK) == BST_CHECKED) ? TRUE : FALSE));

					if (config.is_admin)
					{
						changeservicestate (MPS_SERVICE1, TRUE, IsDlgButtonChecked (hwnd, IDC_DISABLEWINDOWSFIREWALL_CHK));
						changeservicestate (MPS_SERVICE2, TRUE, IsDlgButtonChecked (hwnd, IDC_DISABLEWINDOWSFIREWALL_CHK));
					}

					break;
				}

				case IDD_SETTINGS_3:
				{
					for (size_t i = 0; i < colors.size (); i++)
					{
						app.ConfigSet (colors.at (i).config, _r_listview_getcheckstate (hwnd, IDC_COLORS, i));
						app.ConfigSet (colors.at (i).config_color, colors.at (i).clr);
					}

					app.ConfigSet (L"ShowBalloonTips", DWORD ((IsDlgButtonChecked (hwnd, IDC_SHOWBALLOONTIPS_CHK) == BST_CHECKED) ? TRUE : FALSE));

					break;
				}

				case IDD_SETTINGS_4:
				{
					for (size_t i = 0; i < _r_listview_getitemcount (hwnd, IDC_COMMON); i++)
					{
						size_t idx = _r_listview_getlparam (hwnd, IDC_COMMON, i);

						app.ConfigSet (rules.at (idx).config, _r_listview_getcheckstate (hwnd, IDC_COMMON, i));
					}

					break;
				}

				case IDD_SETTINGS_5:
				case IDD_SETTINGS_6:
				{
					for (size_t i = 0; i < _r_listview_getitemcount (hwnd, IDC_EDITOR); i++)
					{
						size_t idx = _r_listview_getlparam (hwnd, IDC_EDITOR, i);

						rules_ext.at (idx).is_enabled = _r_listview_getcheckstate (hwnd, IDC_EDITOR, i);
					}

					_app_profilesave (app.GetHWND ());

					break;
				}
			}

			break;
		}
	}

	return FALSE;
}

INT_PTR CALLBACK DlgProc (HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
	if (msg == WM_FINDMSGSTRING)
	{
		LPFINDREPLACE lpfr = (LPFINDREPLACE)lparam;

		if ((lpfr->Flags & FR_DIALOGTERM) != 0)
		{
			config.hfind = nullptr;
		}
		else if ((lpfr->Flags & FR_FINDNEXT) != 0)
		{
			size_t total = _r_listview_getitemcount (hwnd, IDC_LISTVIEW);
			INT start = (INT)SendDlgItemMessage (hwnd, IDC_LISTVIEW, LVM_GETNEXTITEM, (WPARAM)total - 1, LVNI_SELECTED | LVNI_DIRECTIONMASK | LVNI_BELOW) + 1;

			for (size_t i = start; i < total; i++)
			{
				const size_t hash = _r_listview_getlparam (hwnd, IDC_LISTVIEW, i);

				ITEM_APPLICATION* ptr = &applications[hash];

				if (StrStrI (ptr->full_path, lpfr->lpstrFindWhat) != nullptr)
				{
					ShowItem (hwnd, IDC_LISTVIEW, i);
					break;
				}
			}
		}

		return FALSE;
	}

	switch (msg)
	{
		case WM_INITDIALOG:
		{
			RECT rc = {0};

#ifndef _WIN64
			if (_r_sys_iswow64 ())
			{
				BOOL old = 0;
				Wow64DisableWow64FsRedirection ((PVOID*)&old);
		}
#endif // _WIN64

			// static initializer
			config.is_admin = _r_sys_adminstate ();
			config.apply_evt = CreateEvent (nullptr, FALSE, FALSE, nullptr);
			config.apply2_evt = CreateEvent (nullptr, FALSE, FALSE, nullptr);
			config.stop_evt = CreateEvent (nullptr, FALSE, FALSE, nullptr);
			config.wd_length = GetWindowsDirectory (config.windows_dir, _countof (config.windows_dir));
			StringCchPrintf (config.config_path, _countof (config.config_path), L"%s\\config.xml", app.GetProfileDirectory ());
			StringCchPrintf (config.profile_path, _countof (config.profile_path), L"%s\\profile.xml", app.GetProfileDirectory ());
			StringCchPrintf (config.rules_path, _countof (config.rules_path), L"%s\\rules.xml", app.GetProfileDirectory ());

			StringCchPrintf (config.svchost_path, _countof (config.svchost_path), L"%s\\system32\\svchost.exe", config.windows_dir);
			config.svchost_hash = rstring (config.svchost_path).Hash ();

			GetModuleFileName (app.GetHINSTANCE (), config.myself_path, _countof (config.myself_path));
			config.myself_hash = rstring (config.myself_path).Hash ();

			// set privileges
			if (config.is_admin)
				_r_sys_setprivilege (SE_DEBUG_NAME, TRUE);

			// configure listview
			_r_listview_setstyle (hwnd, IDC_LISTVIEW, LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP | LVS_EX_LABELTIP | LVS_EX_CHECKBOXES);

			_r_listview_addcolumn (hwnd, IDC_LISTVIEW, nullptr, 95, 0, LVCFMT_LEFT);

			SetIconsSize (hwnd);

			// drag & drop support
			DragAcceptFiles (hwnd, TRUE);

			// resize support
			GetClientRect (GetDlgItem (hwnd, IDC_STATUSBAR), &rc);
			config.sb_height = rc.bottom;
			GetClientRect (hwnd, &rc);

			SendMessage (hwnd, WM_SIZE, 0, MAKELPARAM ((rc.right - rc.left), (rc.bottom - rc.top)));

			// load xml
			_app_profileload (hwnd);

			_app_listviewsort (hwnd);

			// settings
			app.AddSettingsPage (nullptr, IDD_SETTINGS_1, IDS_SETTINGS_1, L"IDS_SETTINGS_1", &settings_callback);
			app.AddSettingsPage (nullptr, IDD_SETTINGS_2, IDS_SETTINGS_2, L"IDS_SETTINGS_2", &settings_callback);
			app.AddSettingsPage (nullptr, IDD_SETTINGS_3, IDS_SETTINGS_3, L"IDS_SETTINGS_3", &settings_callback);

			size_t rules_page = app.AddSettingsPage (nullptr, IDD_SETTINGS_4, IDS_SETTINGS_4, L"IDS_SETTINGS_4", &settings_callback);

			app.AddSettingsPage (nullptr, IDD_SETTINGS_5, IDS_SETTINGS_5, L"IDS_SETTINGS_5", &settings_callback, rules_page);
			app.AddSettingsPage (nullptr, IDD_SETTINGS_6, IDS_SETTINGS_6, L"IDS_SETTINGS_6", &settings_callback, rules_page);

			// startup parameters
			if (!wcsstr (GetCommandLine (), L"/minimized") && !app.ConfigGet (L"StartMinimized", TRUE).AsBool ())
				_r_wnd_toggle (hwnd, TRUE);

			break;
	}

		case WM_DROPFILES:
		{
			UINT numfiles = DragQueryFile ((HDROP)wparam, 0xFFFFFFFF, nullptr, 0);
			size_t item = 0;

			for (UINT i = 0; i < numfiles; i++)
			{
				UINT lenname = DragQueryFile ((HDROP)wparam, i, nullptr, 0);

				LPWSTR file = new WCHAR[(lenname + 1) * sizeof (WCHAR)];

				DragQueryFile ((HDROP)wparam, i, file, lenname + 1);

				item = _app_addapplication (hwnd, file, FALSE);

				delete[] file;
			}

			_app_listviewsort (hwnd);

			ShowItem (hwnd, IDC_LISTVIEW, _app_getposition (hwnd, item));

			DragFinish ((HDROP)wparam);

			break;
		}

		case WM_DESTROY:
		{
			SetEvent (config.stop_evt); // stop thread
			config.hthread = nullptr;

			PostQuitMessage (0);

			break;
		}

		case WM_NOTIFY:
		{
			LPNMHDR nmlp = (LPNMHDR)lparam;

			switch (nmlp->code)
			{
				case NM_CUSTOMDRAW:
				{
					if (nmlp->idFrom == IDC_LISTVIEW)
					{
						LONG result = CDRF_DODEFAULT;
						LPNMLVCUSTOMDRAW lpnmlv = (LPNMLVCUSTOMDRAW)lparam;

						switch (lpnmlv->nmcd.dwDrawStage)
						{
							case CDDS_PREPAINT:
							{
								result = CDRF_NOTIFYITEMDRAW;
								break;
							}

							case CDDS_ITEMPREPAINT:
							{
								const size_t hash = lpnmlv->nmcd.lItemlParam;

								if (hash)
								{
									ITEM_APPLICATION const * ptr = &applications[hash];

									COLORREF new_clr = 0;

									if (ptr->is_system && app.ConfigGet (L"IsHighlightSystem", TRUE).AsBool ())
									{
										new_clr = app.ConfigGet (L"ColorSystem", COLOR_SYSTEM).AsUlong ();
									}
									else if (app.ConfigGet (L"IsHighlightInvalid", TRUE).AsBool () && !_r_fs_exists (ptr->full_path))
									{
										new_clr = app.ConfigGet (L"ColorInvalid", COLOR_INVALID).AsUlong ();
									}

									if (new_clr)
									{
										_r_wnd_fillrect (lpnmlv->nmcd.hdc, &lpnmlv->nmcd.rc, new_clr);
										lpnmlv->clrTextBk = new_clr;

										result = CDRF_NEWFONT;
									}
								}

								break;
							}
						}

						SetWindowLongPtr (hwnd, DWLP_MSGRESULT, result);
						return TRUE;
					}

					break;
				}

				case LVN_GETINFOTIP:
				{
					LPNMLVGETINFOTIP lpnmlv = (LPNMLVGETINFOTIP)lparam;

					const size_t hash = (size_t)_r_listview_getlparam (hwnd, IDC_LISTVIEW, lpnmlv->iItem);

					if (hash)
					{
						ITEM_APPLICATION* ptr = &applications[hash];

						rstring tmp;

						if (ptr->description[0])
						{
							tmp.Append (ptr->description);
							tmp.Append (L"\r\n");
						}

						if (ptr->author[0])
						{
							tmp.Append (ptr->author);
							tmp.Append (L"\r\n");
						}

						if (ptr->version[0])
						{
							tmp.Append (ptr->version);
							tmp.Append (L"\r\n");
						}

						if (tmp.IsEmpty ())
							tmp = L"n/a";

						StringCchCopy (lpnmlv->pszText, lpnmlv->cchTextMax, tmp);
					}

					break;
				}

				case LVN_ITEMCHANGED:
				{
					LPNMLISTVIEW lpnmlv = reinterpret_cast<LPNMLISTVIEW>(nmlp);

					if (lpnmlv->uNewState == 8192 || lpnmlv->uNewState == 4096)
					{
						const size_t hash = lpnmlv->lParam;

						if (!hash || !config.is_firstapply || applications.find (hash) == applications.end ())
							return FALSE;

						ITEM_APPLICATION* ptr = &applications[hash];
						ptr->is_checked = (lpnmlv->uNewState == 8192) ? TRUE : FALSE;

						if ((EnumMode)app.ConfigGet (L"Mode", Whitelist).AsUint () != TrustNoOne)
							SetEvent (config.apply_evt); // apply filters
						else
							_app_listviewsort (hwnd);
					}

					break;
				}

				case LVN_DELETEALLITEMS:
				case LVN_INSERTITEM:
				case LVN_DELETEITEM:
				{
					_r_listview_resizeonecolumn (hwnd, IDC_LISTVIEW);
					_app_refreshstatus (hwnd);

					break;
				}
			}

			break;
		}

		case WM_CONTEXTMENU:
		{
			if (GetDlgCtrlID ((HWND)wparam) == IDC_LISTVIEW)
			{
				HMENU menu = LoadMenu (nullptr, MAKEINTRESOURCE (IDM_LISTVIEW)), submenu = GetSubMenu (menu, 0);
				HMENU submenu1 = GetSubMenu (submenu, 1);

				// localize
				app.LocaleMenu (submenu, I18N (&app, IDS_ADD, 0), 0, TRUE);
				app.LocaleMenu (submenu, I18N (&app, IDS_ADD_FILE, 0), IDM_ADD_FILE, FALSE);
				app.LocaleMenu (submenu, I18N (&app, IDS_ADD_PROCESS, 0), 1, TRUE);
				app.LocaleMenu (submenu, I18N (&app, IDS_ALL, 0), IDM_ALL, FALSE);
				app.LocaleMenu (submenu, I18N (&app, IDS_REFRESH, 0), IDM_REFRESH2, FALSE);
				app.LocaleMenu (submenu, I18N (&app, IDS_EXPLORE, 0), IDM_EXPLORE, FALSE);
				app.LocaleMenu (submenu, I18N (&app, IDS_COPY, 0), IDM_COPY, FALSE);
				app.LocaleMenu (submenu, I18N (&app, IDS_DELETE, 0), IDM_DELETE, FALSE);
				app.LocaleMenu (submenu, I18N (&app, IDS_CHECK, 0), IDM_CHECK, FALSE);
				app.LocaleMenu (submenu, I18N (&app, IDS_UNCHECK, 0), IDM_UNCHECK, FALSE);

				if (!SendDlgItemMessage (hwnd, IDC_LISTVIEW, LVM_GETSELECTEDCOUNT, 0, 0))
				{
					EnableMenuItem (submenu, IDM_EXPLORE, MF_BYCOMMAND | MF_DISABLED);
					EnableMenuItem (submenu, IDM_COPY, MF_BYCOMMAND | MF_DISABLED);
					EnableMenuItem (submenu, IDM_DELETE, MF_BYCOMMAND | MF_DISABLED);
					EnableMenuItem (submenu, IDM_CHECK, MF_BYCOMMAND | MF_DISABLED);
					EnableMenuItem (submenu, IDM_UNCHECK, MF_BYCOMMAND | MF_DISABLED);
				}

				// generate processes popup menu
				{
					_app_getprocesslist (nullptr, &processes);

					for (size_t i = 0; i < processes.size (); i++)
					{
						AppendMenu (submenu1, MF_STRING, IDM_ADD_PROCESS + i, processes.at (i).display_path);

						MENUITEMINFO mii = {0};

						mii.cbSize = sizeof (mii);
						mii.fMask = MIIM_BITMAP;
						mii.hbmpItem = processes.at (i).hbmp;

						SetMenuItemInfo (submenu1, IDM_ADD_PROCESS + UINT (i), FALSE, &mii);
					}

					if (!processes.size ())
						EnableMenuItem (submenu1, IDM_ALL, MF_BYCOMMAND | MF_DISABLED);
				}

				POINT pt = {0};
				GetCursorPos (&pt);

				TrackPopupMenuEx (submenu, TPM_RIGHTBUTTON | TPM_LEFTBUTTON, pt.x, pt.y, hwnd, nullptr);

				DestroyMenu (menu);
				DestroyMenu (submenu);
			}

			break;
		}

		case WM_SIZE:
		{
			if (wparam == SIZE_MINIMIZED)
			{
				_r_wnd_toggle (hwnd, FALSE);
				return FALSE;
			}

			ResizeWindow (hwnd, LOWORD (lparam), HIWORD (lparam));
			RedrawWindow (hwnd, nullptr, nullptr, RDW_ALLCHILDREN | RDW_ERASE | RDW_INVALIDATE);

			break;
		}

		case WM_SYSCOMMAND:
		{
			if (wparam == SC_CLOSE)
			{
				_r_wnd_toggle (hwnd, FALSE);
				return TRUE;
			}

			break;
		}

		case WM_TRAYICON:
		{
			switch (LOWORD (lparam))
			{
				case WM_LBUTTONUP:
				{
					SetForegroundWindow (hwnd);
					break;
				}

				case WM_LBUTTONDBLCLK:
				case WM_MBUTTONDOWN:
				{
					_r_wnd_toggle (hwnd, FALSE);
					break;
				}

				case WM_RBUTTONUP:
				{
					SetForegroundWindow (hwnd); // don't touch

					HMENU menu = LoadMenu (nullptr, MAKEINTRESOURCE (IDM_TRAY)), submenu = GetSubMenu (menu, 0);
					HMENU submenu1 = GetSubMenu (submenu, 5);

					// localize
					app.LocaleMenu (submenu, I18N (&app, IDS_TRAY_SHOW, 0), IDM_TRAY_SHOW, FALSE);
					app.LocaleMenu (submenu, I18N (&app, (config.hengine ? IDS_TRAY_STOP : IDS_TRAY_START), config.hengine ? L"IDS_TRAY_STOP" : L"IDS_TRAY_START"), IDM_TRAY_START, FALSE);
					app.LocaleMenu (submenu, I18N (&app, IDS_TRAY_MODE, 0), 3, TRUE);
					app.LocaleMenu (submenu, I18N (&app, IDS_MODE_WHITELIST, 0), IDM_TRAY_MODEWHITELIST, FALSE);
					app.LocaleMenu (submenu, I18N (&app, IDS_MODE_BLACKLIST, 0), IDM_TRAY_MODEBLACKLIST, FALSE);
					app.LocaleMenu (submenu, I18N (&app, IDS_MODE_TRUSTNOONE, 0), IDM_TRAY_MODETRUSTNOONE, FALSE);
					app.LocaleMenu (submenu, I18N (&app, IDS_SETTINGS, 0), 5, TRUE);
					app.LocaleMenu (submenu, I18N (&app, IDS_SETTINGS, 0), IDM_TRAY_SETTINGS, FALSE);
					app.LocaleMenu (submenu, I18N (&app, IDS_WEBSITE, 0), IDM_TRAY_WEBSITE, FALSE);
					app.LocaleMenu (submenu, I18N (&app, IDS_ABOUT, 0), IDM_TRAY_ABOUT, FALSE);
					app.LocaleMenu (submenu, I18N (&app, IDS_EXIT, 0), IDM_TRAY_EXIT, FALSE);

					if (!config.is_admin)
						EnableMenuItem (submenu, 3, MF_BYPOSITION | MF_DISABLED);

					CheckMenuRadioItem (submenu, IDM_TRAY_MODEWHITELIST, IDM_TRAY_MODETRUSTNOONE, IDM_TRAY_MODEWHITELIST + app.ConfigGet (L"Mode", Whitelist).AsUint (), MF_BYCOMMAND);

					{
						DeleteMenu (submenu1, 0, MF_BYPOSITION);

						for (size_t i = 0; i < rules.size (); i++)
						{
							if (rules.at (i).group_id != 0)
								break;

							AppendMenu (submenu1, MF_STRING, IDM_RULES + i, I18N (&app, rules.at (i).locale_id, rules.at (i).locale_sid));

							if (app.ConfigGet (rules.at (i).config, rules.at (i).is_enabled).AsBool ())
								CheckMenuItem (submenu1, IDM_RULES + UINT (i), MF_BYCOMMAND | MF_CHECKED);
						}

						AppendMenu (submenu1, MF_SEPARATOR, 0, 0);
						AppendMenu (submenu1, MF_STRING, IDM_TRAY_SETTINGS, I18N (&app, IDS_SETTINGS, 0));
					}

					POINT pt = {0};
					GetCursorPos (&pt);

					TrackPopupMenuEx (submenu, TPM_RIGHTBUTTON | TPM_LEFTBUTTON, pt.x, pt.y, hwnd, nullptr);

					DestroyMenu (submenu);
					DestroyMenu (menu);

					break;
				}
			}

			break;
		}

		case WM_DEVICECHANGE:
		{
			if ((wparam == DBT_DEVICEARRIVAL) || (wparam == DBT_DEVICEREMOVECOMPLETE))
			{
				DEV_BROADCAST_HDR* header = (PDEV_BROADCAST_HDR)lparam;

				if (header->dbch_devicetype == DBT_DEVTYP_VOLUME)
					SetEvent (config.apply2_evt); // apply filters
			}

			break;
		}

		case WM_COMMAND:
		{
			if (HIWORD (wparam) == 0 && LOWORD (wparam) >= IDM_DEFAULT && LOWORD (wparam) <= IDM_DEFAULT + app.LocaleGetCount ())
			{
				app.LocaleApplyFromMenu (GetSubMenu (GetSubMenu (GetMenu (hwnd), 2), 5), LOWORD (wparam), IDM_DEFAULT);

				return FALSE;
			}
			else if ((LOWORD (wparam) >= IDM_ADD_PROCESS && LOWORD (wparam) <= IDM_ADD_PROCESS + processes.size ()))
			{
				ITEM_PROCESS const * ptr = &processes.at (LOWORD (wparam) - IDM_ADD_PROCESS);

				const size_t hash = _app_addapplication (hwnd, ptr->file_path, FALSE);

				_app_listviewsort (hwnd);

				ShowItem (hwnd, IDC_LISTVIEW, _app_getposition (hwnd, hash));

				return FALSE;
			}
			else if ((LOWORD (wparam) >= IDM_RULES && LOWORD (wparam) <= IDM_RULES + rules.size ()))
			{
				ITEM_RULE const * ptr = &rules.at (LOWORD (wparam) - IDM_RULES);

				app.ConfigSet (ptr->config, !app.ConfigGet (ptr->config, ptr->is_enabled).AsBool ());

				SetEvent (config.apply2_evt); // apply filters

				return FALSE;
			}

			switch (LOWORD (wparam))
			{
				case IDCANCEL: // process Esc key
				case IDM_TRAY_SHOW:
				{
					_r_wnd_toggle (hwnd, FALSE);
					break;
				}

				case IDM_SETTINGS:
				case IDM_TRAY_SETTINGS:
				{
					app.CreateSettingsWindow ();
					break;
				}

				case IDM_EXIT:
				case IDM_TRAY_EXIT:
				{
					DestroyWindow (hwnd);
					break;
				}

				case IDM_WEBSITE:
				case IDM_TRAY_WEBSITE:
				{
					ShellExecute (hwnd, nullptr, _APP_WEBSITE_URL, nullptr, nullptr, SW_SHOWDEFAULT);
					break;
				}

				case IDM_DONATE:
				{
					ShellExecute (hwnd, nullptr, _APP_DONATION_URL, nullptr, nullptr, SW_SHOWDEFAULT);
					break;
				}

				case IDM_CHECKUPDATES:
				{
					app.CheckForUpdates (FALSE);
					break;
				}

				case IDM_ABOUT:
				case IDM_TRAY_ABOUT:
				{
					app.CreateAboutWindow ();
					break;
				}

				case IDM_ALWAYSONTOP_CHK:
				{
					BOOL val = app.ConfigGet (L"AlwaysOnTop", FALSE).AsBool ();

					CheckMenuItem (GetMenu (hwnd), IDM_ALWAYSONTOP_CHK, MF_BYCOMMAND | (!val ? MF_CHECKED : MF_UNCHECKED));
					app.ConfigSet (L"AlwaysOnTop", !val);

					_r_wnd_top (hwnd, !val);

					break;
				}

				case IDM_SORTBYFNAME:
				case IDM_SORTBYFDIR:
				{
					app.ConfigSet (L"SortMode", LOWORD (wparam) == IDM_SORTBYFNAME ? 1 : 0);

					_app_listviewsort (hwnd);

					break;
				}

				case IDM_SORTISDESCEND:
				{
					app.ConfigSet (L"IsSortDescending", !app.ConfigGet (L"IsSortDescending", FALSE).AsBool ());

					_app_listviewsort (hwnd);

					break;
				}

				case IDM_ICONSSMALL:
				case IDM_ICONSLARGE:
				{
					app.ConfigSet (L"IsLargeIcons", LOWORD (wparam) == IDM_ICONSLARGE);

					SetIconsSize (hwnd);

					break;
				}

				case IDM_REFRESH:
				case IDM_REFRESH2:
				{
					_app_profileload (hwnd);
					SetEvent (config.apply2_evt); // apply filters

					break;
				}

				case IDM_TRAY_MODEWHITELIST:
				case IDM_TRAY_MODEBLACKLIST:
				case IDM_TRAY_MODETRUSTNOONE:
				{
					EnumMode curr = Whitelist;

					if (LOWORD (wparam) == IDM_TRAY_MODEBLACKLIST)
						curr = Blacklist;
					else if (LOWORD (wparam) == IDM_TRAY_MODETRUSTNOONE)
						curr = TrustNoOne;

					app.ConfigSet (L"Mode", curr);

					_app_refreshstatus (hwnd);

					SetEvent (config.apply_evt); // apply filters

					break;
				}

				case IDM_FIND:
				{
					if (!config.hfind)
					{
						FINDREPLACE fr = {0};

						fr.lStructSize = sizeof (fr);
						fr.hwndOwner = hwnd;
						fr.lpstrFindWhat = config.search_string;
						fr.wFindWhatLen = _countof (config.search_string) - 1;
						fr.Flags = FR_HIDEWHOLEWORD | FR_HIDEMATCHCASE | FR_HIDEUPDOWN;

						config.hfind = FindText (&fr);
					}
					else
					{
						SetFocus (config.hfind);
					}

					break;
				}

				case IDM_FINDNEXT:
				{
					if (!config.search_string[0])
					{
						SendMessage (hwnd, WM_COMMAND, MAKEWPARAM (IDM_FIND, 0), 0);
					}
					else
					{
						FINDREPLACE fr = {0};

						fr.Flags = FR_FINDNEXT;
						fr.lpstrFindWhat = config.search_string;

						SendMessage (hwnd, WM_FINDMSGSTRING, 0, (LPARAM)&fr);
					}

					break;
				}

				case IDM_TRAY_START:
				{
					if (!config.is_admin)
					{
						if (app.SkipUacRun ())
							DestroyWindow (hwnd);

						app.TrayPopup (NIIF_ERROR, APP_NAME, I18N (&app, IDS_STATUS_NOPRIVILEGES, 0));
					}
					else
					{
						BOOL status = app.ConfigGet (L"IsFiltersEnabled", TRUE).AsBool ();

						if (status && _r_msg (hwnd, MB_YESNO | MB_ICONQUESTION, APP_NAME, nullptr, I18N (&app, IDS_QUESTION_DISABLE, 0)) != IDYES)
							break;

						if (!status && !config.hengine)
						{
							_wfp_start ();
						}
						else if (status && config.hengine)
						{
							_wfp_stop ();
						}

						app.ConfigSet (L"IsFiltersEnabled", !status);
					}

					break;
				}

				case IDM_ADD_FILE:
				{
					WCHAR files[_R_BUFFER_LENGTH] = {0};
					size_t item = 0;
					OPENFILENAME ofn = {0};

					ofn.lStructSize = sizeof (ofn);
					ofn.hwndOwner = hwnd;
					ofn.lpstrFile = files;
					ofn.nMaxFile = _countof (files);
					ofn.lpstrFilter = L"*.exe\0*.exe\0*.*\0*.*\0\0";
					ofn.Flags = OFN_ALLOWMULTISELECT | OFN_EXPLORER | OFN_ENABLESIZING | OFN_PATHMUSTEXIST | OFN_FORCESHOWHIDDEN;

					if (GetOpenFileName (&ofn))
					{
						if (files[ofn.nFileOffset - 1] != 0)
						{
							item = _app_addapplication (hwnd, files, FALSE);
						}
						else
						{
							LPWSTR p = files;
							WCHAR dir[MAX_PATH] = {0};
							GetCurrentDirectory (MAX_PATH, dir);

							while (*p)
							{
								p += wcslen (p) + 1;

								if (*p)
									item = _app_addapplication (hwnd, _r_fmt (L"%s\\%s", dir, p), FALSE);
							}
						}

						_app_listviewsort (hwnd);

						ShowItem (hwnd, IDC_LISTVIEW, _app_getposition (hwnd, _app_getposition (hwnd, item)));
					}

					break;
				}

				case IDM_ALL:
				{
					for (size_t i = 0; i < processes.size (); i++)
						_app_addapplication (hwnd, processes.at (i).file_path, FALSE);

					_app_listviewsort (hwnd);

					break;
				}

				case IDM_EXPLORE:
				case IDM_COPY:
				case IDM_UNCHECK:
				case IDM_CHECK:
				{
					INT item = -1;

					rstring buffer;

					while ((item = (INT)SendDlgItemMessage (hwnd, IDC_LISTVIEW, LVM_GETNEXTITEM, item, LVNI_SELECTED)) != -1)
					{
						const size_t hash = (size_t)_r_listview_getlparam (hwnd, IDC_LISTVIEW, item);

						if (applications.find (hash) == applications.end ())
							continue;

						ITEM_APPLICATION* ptr = &applications[hash];

						if (LOWORD (wparam) == IDM_EXPLORE)
						{
							if (_r_fs_exists (ptr->full_path))
								_r_run (_r_fmt (L"\"explorer.exe\" /select,\"%s\"", ptr->full_path));
							else
								ShellExecute (hwnd, nullptr, ptr->file_dir, nullptr, nullptr, SW_SHOWDEFAULT);
						}
						else if (LOWORD (wparam) == IDM_COPY)
						{
							buffer.Append (ptr->full_path).Append (L"\r\n");
						}
						else if (LOWORD (wparam) == IDM_CHECK || LOWORD (wparam) == IDM_UNCHECK)
						{
							ptr->is_checked = LOWORD (wparam) == IDM_CHECK ? TRUE : FALSE;
							_r_listview_setcheckstate (hwnd, IDC_LISTVIEW, item, LOWORD (wparam) == IDM_CHECK ? TRUE : FALSE);
						}
					}

					if (LOWORD (wparam) == IDM_COPY)
					{
						buffer.Trim (L"\r\n");
						_r_clipboard_set (hwnd, buffer, buffer.GetLength ());
					}

					break;
				}

				case IDM_DELETE:
				{
					if (SendDlgItemMessage (hwnd, IDC_LISTVIEW, LVM_GETSELECTEDCOUNT, 0, 0) && _r_msg (hwnd, MB_YESNO | MB_ICONQUESTION, APP_NAME, nullptr, I18N (&app, IDS_QUESTION, 0)) == IDYES)
					{
						size_t count = _r_listview_getitemcount (hwnd, IDC_LISTVIEW) - 1;

						BOOL is_checked = FALSE;

						for (size_t i = count; i != LAST_VALUE; i--)
						{
							if (ListView_GetItemState (GetDlgItem (hwnd, IDC_LISTVIEW), i, LVNI_SELECTED))
							{
								const size_t hash = (size_t)_r_listview_getlparam (hwnd, IDC_LISTVIEW, i);

								ITEM_APPLICATION* ptr = &applications[hash];

								if (ptr->is_checked)
									is_checked = TRUE;

								ptr = nullptr;

								applications.erase (hash);

								SendDlgItemMessage (hwnd, IDC_LISTVIEW, LVM_DELETEITEM, i, 0);
							}
						}

						if (is_checked)
							SetEvent (config.apply_evt); // apply filters
						else
							_app_profilesave (hwnd);
					}

					break;
				}

				case IDM_SELECT_ALL:
				{
					ListView_SetItemState (GetDlgItem (hwnd, IDC_LISTVIEW), -1, LVIS_SELECTED, LVIS_SELECTED);
					break;
				}
			}

			break;
		}
}

	return FALSE;
}

INT APIENTRY wWinMain (HINSTANCE, HINSTANCE, LPWSTR, INT)
{
	if (app.CreateMainWindow (&DlgProc, &initializer_callback))
	{
		MSG msg = {0};

		HACCEL haccel = LoadAccelerators (app.GetHINSTANCE (), MAKEINTRESOURCE (IDA_MAIN));

		while (GetMessage (&msg, nullptr, 0, 0) > 0)
		{
			if ((haccel && !TranslateAccelerator (app.GetHWND (), haccel, &msg)) && !IsDialogMessage (app.GetHWND (), &msg))
			{
				TranslateMessage (&msg);
				DispatchMessage (&msg);
			}
		}

		if (haccel)
			DestroyAcceleratorTable (haccel);
	}

	return ERROR_SUCCESS;
}
