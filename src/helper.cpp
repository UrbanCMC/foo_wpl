/*
Copyright (c) 2015, Mike Tzou
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

* Redistributions of source code must retain the above copyright notice, this
list of conditions and the following disclaimer.

* Redistributions in binary form must reproduce the above copyright notice,
this list of conditions and the following disclaimer in the documentation
and/or other materials provided with the distribution.

* Neither the name of foo_xspf_1 nor the names of its
contributors may be used to endorse or promote products derived from
this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "stdafx.h"
#include "helper.h"

// classes, typedefs
class MainThreadTask : public main_thread_callback
{
	public:
		enum class Task
		{
			IS_LIBRARY_ENABLED,
			GET_ALL_LIBRARY,
			PROCESS_LOCATIONS,
			TASK_MAX
		};

		explicit MainThreadTask(const Task t) : task_sel(t)
		{
			return;
		}

		void add_callback()
		{
			static_api_ptr_t<main_thread_callback_manager> m;
			m->add_callback(this);
			return;
		}

		void callback_run()  // overwrite virtual func
		{
			// main thread runs here
			switch(task_sel)
			{
			case Task::IS_LIBRARY_ENABLED:
			{
				static_api_ptr_t<library_manager> m;
				is_library_enabled.set_value(m->is_library_enabled());
				break;
			}

			case Task::GET_ALL_LIBRARY:
			{
				l_1.remove_all();

				static_api_ptr_t<library_manager> m;
				m->get_all_items(l_1);
				list_out.set_value(&l_1);
				break;
			}

			case Task::PROCESS_LOCATIONS:
			{
				l_2.remove_all();

				static_api_ptr_t<playlist_incoming_item_filter> p;
				p->process_locations(resolve_list_in, l_2, false, nullptr, nullptr, NULL);
				resolve_list_out.set_value(&l_2);
				break;
			}

			default:
			{
				console::printf(CONSOLE_HEADER"Invalid task_sel: %d", task_sel);
				break;
			}
			};

			return;
		}

		// Task::IS_LIBRARY_ENABLED
		std::promise<bool> is_library_enabled;

		// Task::GET_ALL_LIBRARY
		std::promise<DbList *> list_out;

		// Task::PROCESS_LOCATIONS
		pfc::list_t<const char *> resolve_list_in;
		std::promise<DbList * > resolve_list_out;

	private:
		const Task task_sel;

		// Task::GET_ALL_LIBRARY
		DbList l_1;

		// Task::PROCESS_LOCATIONS
		DbList l_2;
};

class TrackQueue
{
	public:
		void add(const char *in)
		{
			str_list += in;
			return;
		}

		void reset()
		{
			str_list.remove_all();
			return;
		}

		void resolve(playlist_loader_callback::ptr p_callback)
		{
			// let fb2k handle all input

			if(str_list.get_count() == 0)
				return;

			service_ptr_t<MainThreadTask> m_task(new service_impl_t<MainThreadTask>(MainThreadTask::Task::PROCESS_LOCATIONS));

			m_task->resolve_list_in.remove_all();
			for(t_size i = 0, max = str_list.get_count(); i < max; ++i)
			{
				const char *tmp = str_list.get_item_ref(i);
				m_task->resolve_list_in += tmp;
				p_callback->on_progress(tmp);
			}
			auto cb_list = m_task->resolve_list_out.get_future();
			m_task->add_callback();

			// add
			const DbList l = *(cb_list.get());
			for(t_size i = 0, max = l.get_count(); i < max; ++i)
			{
				p_callback->on_entry(l.get_item_ref(i), playlist_loader_callback::entry_from_playlist, filestats_invalid, false);
			}

			this->reset();
			return;
		}

	private:
		pfc::list_t<pfc::string8> str_list;
};

class TrackInfoCache
{
	public:
		explicit TrackInfoCache()
		{
			// get library status
			{
				service_ptr_t<MainThreadTask> m_task(new service_impl_t<MainThreadTask>(MainThreadTask::Task::IS_LIBRARY_ENABLED));
				auto h_library = m_task->is_library_enabled.get_future();
				m_task->add_callback();
				have_library = h_library.get();
			}

			// get media library
			if(have_library)
			{
				service_ptr_t<MainThreadTask> m_task(new service_impl_t<MainThreadTask>(MainThreadTask::Task::GET_ALL_LIBRARY));
				auto list_ptr = m_task->list_out.get_future();
				m_task->add_callback();
				lib_list.move_from(*(list_ptr.get()));
				//				lib_list.sort_by_path_quick();
			}

			return;
		}

		void session_restart()
		{
			session_list.remove_all();
			is_first = true;
			return;
		}

		void filter(const char *x_name, const char *x_val, const char *meta_name, const bool use_cache)
		{
			const DbList *list = &session_list;  // the "list" operates on
			if(is_first)
			{
				list = &lib_list;
			}

			LruCacheHandleList *cache_ptr = nullptr;
			if(is_first && use_cache)
			{
				// search for "type name", also insert a new element...
				cache_ptr = &(cache_map[x_name]);
				const auto j = cache_ptr->get(x_val);
				if(j != nullptr)
				{
					list = j;
				}
			}

			// scan through list
			std::multimap< size_t, t_size >out;  // <number of character matches, index in `list`>
			const size_t x_val_len = strlen(x_val);
			for(t_size i = 0, max = list->get_count(); i < max; ++i)
			{
				// get meta string from db
				const auto item = list->get_item_ref(i);
				const auto info = item->get_async_info_ref();
				const char *str = info->info().meta_get(meta_name, 0);
				if(str == nullptr)
					continue;

				// try best match, case-sensitive
				const bool b_match = (strcmp(str, x_val) == 0) ? true : false;
				if(b_match)
				{
					out.emplace_hint(out.end(), SIZE_MAX, i);  // special place for best match
					continue;
				}

				//try partial match, case-insensitive
				const size_t str_len = strlen(str);
				const bool p_match = (str_len > x_val_len) ? my_strcasestr(str, x_val) : my_strcasestr(x_val, str);
				if(p_match)
				{
					out.emplace(min(str_len, x_val_len), i);
				}
			}

			// handle the results
			DbList out_list;
			for(auto i = out.crbegin(), max = out.crend(); i != max; ++i)
			{
				// sorted from most likely to least likely
				out_list += list->get_item_ref(i->second);
			}

			if(is_first && use_cache)
			{
				// store back results
				cache_ptr->set(x_val, out_list);
			}

			session_list.move_from(out_list);
			is_first = false;
			return;
		}

		bool is_library_enabled() const
		{
			return have_library;
		}

		const DbList *getList() const
		{
			return &session_list;
		}

	private:
		DbList lib_list;
		std::map<std::string, LruCacheHandleList> cache_map;

		DbList session_list;
		bool is_first = true;

		bool have_library = false;

		bool my_strcasestr(const char *haystack, const char *needle)
		{
			// haystack & needle needs to be NULL terminated!
			const char *haystack_end = haystack + strlen(haystack);
			const char *needle_end = needle + strlen(needle);
			const auto it = std::search(haystack, haystack_end, needle, needle_end,
				[](const char &ch1, const char &ch2) { return (std::toupper(ch1) == std::toupper(ch2)); });
			return (it != haystack_end);
		}
};

//prototypes
void xmlCreateDocument(tinyxml2::XMLDocument *xml_doc, const char *f);
void xmlAddAttribute(tinyxml2::XMLElement *xml_element, const char *attribute_name, const char *attribute_value);
const char* xmlGetAttribute(const tinyxml2::XMLElement *xml_element, const char *attribute_name);
tinyxml2::XMLElement* xmlAddElement(tinyxml2::XMLDocument *xml_doc, tinyxml2::XMLNode *xml_parent_node, const char *element_name);
void xmlAddMeta(tinyxml2::XMLDocument *xml_doc, tinyxml2::XMLNode *xml_parent_node, const char *element_name, const char *element_text);
const tinyxml2::XMLElement* xmlGetElement(const tinyxml2::XMLNode *xml_node, const char *element_name);


// Parses the playlist specified by p_file and adds all tracks to the foobar2000 playlist.
void parse(const char *p_path, const service_ptr_t<file> &p_file, playlist_loader_callback::ptr p_callback, abort_callback &p_abort)
{
	//load the file
	pfc::string8 input_file;
	std::string playlistPath = p_path;
	std::string basePath;

	bool workingDirSet = false;

	try
	{
		p_file->seek(0, p_abort);
		p_file->read_string_raw(input_file, p_abort);
	}
	catch(...)
	{
		console::printf(CONSOLE_HEADER "exception while loading file");
		throw;
	}

	//parse the document
	tinyxml2::XMLDocument xml_doc;
	xmlCreateDocument(&xml_doc, input_file);

	// Get smil element
	const auto xml_smil = xmlGetElement(&xml_doc, "smil");

	// Get body element
	const auto xml_body = xmlGetElement(xml_smil, "body");

	// Get seq element
	const auto xml_seq = xmlGetElement(xml_body, "seq");

	// Loop through the media elements
	TrackQueue track_queue;
	TrackInfoCache track_cache;

	for(auto *xml_media = xml_seq->FirstChildElement("media"); xml_media != nullptr; xml_media = xml_media->NextSiblingElement("media"))
	{
		// Check if the user cancelled the operation
		if(p_abort.is_aborting())
		{
			return;
		}

		// Get the src attribute
		const char *xml_src_attribute = xmlGetAttribute(xml_media, "src");

		std::string fullPath;

		// Check if the path is actually relative, in which case we build the full path from it.
		if (PathIsRelativeA(xml_src_attribute))
		{
#ifdef DEBUG
			console::printf(CONSOLE_HEADER "File path is relative, constructing full path for %s...", xml_src_attribute);
#endif
			if (!workingDirSet)
			{
				// Set the working directory to the path of the playlist, so that we correctly construct the absolute path
				basePath = playlistPath.substr(7, playlistPath.find_last_of("\\") - 6);
				workingDirSet = true;
			}

			fullPath = basePath + xml_src_attribute;

#ifdef DEBUG
			console::printf(CONSOLE_HEADER "Adding %s to queue", fullPath.c_str());
#endif
			track_queue.add(fullPath.c_str());
		}
		else
		{
#ifdef DEBUG
			console::printf(CONSOLE_HEADER "Adding %s to queue", xml_src_attribute);
#endif
			track_queue.add(xml_src_attribute);
		}
	}

	track_queue.resolve(p_callback);
}

// Writes the tracks specified by p_data to the specified file system location
void write_playlist(const char *p_path, const service_ptr_t<file> &p_file, metadb_handle_list_cref p_data, abort_callback &p_abort)
{
	//Create the xml document where we will write the playlist
	tinyxml2::XMLDocument xml_doc;

	//Add wpl declaration
	auto xml_dec = xml_doc.NewDeclaration("wpl version=\"1.0\"");
	xml_doc.InsertEndChild(xml_dec);

	//Add smil tag
	auto xml_smil = xmlAddElement(&xml_doc, &xml_doc, "smil");

	//Add head tag
	auto xml_head = xmlAddElement(&xml_doc, xml_smil, "head");

	//Add generator info
	auto xml_meta_generator = xmlAddElement(&xml_doc, xml_head, "meta");
	xmlAddAttribute(xml_meta_generator, "name", "Generator");
	xmlAddAttribute(xml_meta_generator, "content", "Foobar2000 WPL plugin -- " PLUGIN_VERSION);

	//Add body tag
	auto xml_body = xmlAddElement(&xml_doc, xml_smil, "body");

	//Add seq tag
	auto xml_seq = xmlAddElement(&xml_doc, xml_body, "seq");

	//Add all tracks
	for(t_size i = 0, max = p_data.get_size(); i < max; i++)
	{
		if(p_abort.is_aborting())
		{
			return;
		}

		//get track
		const auto track = p_data.get_item(i);

		//Add media element
		auto xml_media = xmlAddElement(&xml_doc, xml_seq, "media");

#ifdef DEBUG
		console::printf(CONSOLE_HEADER "Adding %s to playlist file", track->get_path());
#endif

		//Add track location attribute
		xmlAddAttribute(xml_media, "src", track->get_path());
	}

	//Add xml document to the xml_printer
	tinyxml2::XMLPrinter xml_printer(nullptr, false);
	xml_doc.Print(&xml_printer);

	try
	{
		//Write xml_printer content to disk
		p_file->write_string_raw(xml_printer.CStr(), p_abort);
	}
	catch(...)
	{
		console::printf(CONSOLE_HEADER "write_string_raw exception");
	}
}

// Creates an xml document from the specified character string.
// If an error occurs, an io_data-exception is thrown.
void xmlCreateDocument(tinyxml2::XMLDocument *xml_doc, const char *f)
{
	const tinyxml2::XMLError error = xml_doc->Parse(f);

	if(error != tinyxml2::XML_NO_ERROR)
	{
		console::printf(CONSOLE_HEADER "XML parse error, ID: %d, Msg: %s", error, xml_doc->GetErrorStr1());
		throw exception_io_data();
	}
}

// Adds a new xml attribute with the specified attribute_name and attribute_value to the xml_element.
void xmlAddAttribute(tinyxml2::XMLElement *xml_element, const char *attribute_name, const char *attribute_value)
{
	xml_element->SetAttribute(attribute_name, attribute_value);
}

// Returns the content of the specified attribute from an xml element as a character string.
// If the specified attribute doesn't exist, an io_data-exception is thrown.
const char* xmlGetAttribute(const tinyxml2::XMLElement *xml_element, const char *attribute_name)
{
	const char *xml_attribute = xml_element->Attribute(attribute_name);

	if(xml_attribute == nullptr)
	{
		console::printf(CONSOLE_HEADER "attribute %s not found", attribute_name);
		throw exception_io_data();
	}

	return xml_attribute;
}

// Adds a new xml element with the specified element_name to the xml_doc, as a child element of xml_parent_element.
// Returns the xml element that was added.
tinyxml2::XMLElement* xmlAddElement(tinyxml2::XMLDocument *xml_doc, tinyxml2::XMLNode *xml_parent_node, const char *element_name)
{
	auto *xml_element = xml_doc->NewElement(element_name);
	xml_parent_node->InsertEndChild(xml_element);

	return xml_element;
}

// Adds a new xml element with the specified element_name and element_text to the xml_doc, as a child element of xml_parent_element.
void xmlAddMeta(tinyxml2::XMLDocument *xml_doc, tinyxml2::XMLNode *xml_parent_node, const char *element_name, const char *element_text)
{
	if(element_text == nullptr)
	{
		return;
	}

	auto *xml_element = xmlAddElement(xml_doc, xml_parent_node, element_name);
	xml_element->SetText(element_text);
}

// Returns the xml element with the specified name from the specified xml node.
// If there are multiple elements with the same name, the first will be returned.
// If there are no elements with the specified name, an io_data-exception is thrown.
const tinyxml2::XMLElement* xmlGetElement(const tinyxml2::XMLNode *xml_node, const char *element_name)
{
	const tinyxml2::XMLElement *xml_element = xml_node->FirstChildElement(element_name);

	if(xml_element == nullptr)
	{
		console::printf(CONSOLE_HEADER " element %s not found", element_name);
		throw exception_io_data();
	}

	return xml_element;
}