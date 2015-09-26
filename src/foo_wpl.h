#pragma once

class wpl : public playlist_loader
{
	public:
		bool can_write();
		const char *get_extension();
		bool is_associatable();
		bool is_our_content_type(const char *p_content_type);

		void open(const char *p_path, const service_ptr_t<file> &p_file, playlist_loader_callback::ptr p_callback, abort_callback &p_abort);
		void write(const char *p_path, const service_ptr_t<file> &p_file, metadb_handle_list_cref p_data, abort_callback &p_abort);

	private:
		static std::set<std::string> file_list;
};
std::set<std::string> wpl::file_list;

playlist_loader_factory_t<wpl> wpl_main;