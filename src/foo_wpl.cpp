#include "stdafx.h"
#include "foo_wpl.h"
#include "helper.h"

DECLARE_COMPONENT_VERSION
(
	PLUGIN_NAME, PLUGIN_VERSION,
	PLUGIN_NAME "\n"
	"Compiled: " __DATE__ "\n"
	"Author: Alexander Gyimesi\n"
	"\n"
	"Source: https://github.com/UrbanCMC/foo_wpl \n"
	"This plugin is released under the BSD 3-Clause license\n"
	"\n"
	"This plugin is statically linked with the open source TinyXML-2 library:\n"
	" http://www.grinninglizard.com/tinyxml2 \n"
	"\n"
	"Parts of the helper classes were taken and modified from foo_xspf_1:\n"
	"(https://github.com/Chocobo1/foo_xspf_1) by Mike Tzou."
);

// Returns whether this plugin can write playlists to the file system
bool wpl::can_write()
{
	return false;
}

// Returns the file extension that is handled by this plugin
const char *wpl::get_extension()
{
	return "wpl";
}

// Returns whether foobar2000 can be set as the default application for the file extension handled by this plugin
bool wpl::is_associatable()
{
	return true;
}

// Returns whether the specified content type is the one that is supported by this plugin
bool wpl::is_our_content_type(const char *p_content_type)
{
	const char mime[] = "application/vnd.ms-wpl";

	return strcmp(p_content_type, mime) == 0;
}

// Opens the specified playlist and adds all tracks found inside to the foobar2000 playlist
void wpl::open(const char *p_path, const service_ptr_t<file> &p_file, playlist_loader_callback::ptr p_callback, abort_callback &p_abort)
{
	if(file_list.find(p_path) != file_list.cend())
	{
		return;
	}
	file_list.emplace(p_path);
	
	try
	{
		p_callback->on_progress(p_path);
		parse(p_path, p_file, p_callback, p_abort);
	}
	catch(...)
	{
		file_list.erase(p_path);
		console::printf(CONSOLE_HEADER "error while opening playlist");
		throw;
	}

	file_list.erase(p_path);
}
// Method not yet implemented.
void wpl::write(const char* p_path, const service_ptr_t<file>& p_file, metadb_handle_list_cref p_data, abort_callback& p_abort)
{
	return;
}