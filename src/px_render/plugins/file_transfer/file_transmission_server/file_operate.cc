#include "file_operate.h"

#ifdef WIN32
#include <Windows.h>
#include <Knownfolders.h>
#include <shlobj_core.h>
#include <wtsapi32.h>
#endif // WIN32
#include <filesystem>
#include <chrono>
#include "px_message.pb.h"
#include "px_common_new/string_util.h"
#include "px_common_new/log.h"
#include "px_common_new/process_util.h"
#include "px_tr.h"

#ifdef WIN32
#pragma comment(lib, "Wtsapi32.lib")
#endif // WIN32


namespace px {
	static std::string s_file_permission_path_ = "/";

	FileOperate::FileOperate() {}

	std::vector<px::FileDescInfo> FileOperate::GetFilesListImpl(const std::string& path) {
		std::vector<px::FileDescInfo> file_infos;
		try {
			for (const auto& entry : std::filesystem::directory_iterator(PathFromUTF8(path))) {
				px::FileDescInfo info;
				auto abs_path = PathToUTF8(std::filesystem::absolute(entry.path()));
				info.set_name(PathToUTF8(entry.path().filename()));
				info.set_path(abs_path);
				if (std::filesystem::is_directory(entry.status())) {
					info.set_type(px::FileDescInfo::kFolder);
					if (desktop_path_ == abs_path) {
						info.set_type(px::FileDescInfo::kDeskFolder);
					}
				}
				else if (std::filesystem::is_regular_file(entry.status())) {
					info.set_type(px::FileDescInfo::FileType::FileDescInfo_FileType_kFile);
					info.set_size(std::filesystem::file_size(entry));
					auto lwt = std::filesystem::last_write_time(entry);
					auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
						lwt - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
					info.set_date(std::chrono::duration_cast<std::chrono::seconds>(sctp.time_since_epoch()).count());
				}
				else {
					LOGI("Other is {}", abs_path);
					continue;
				}
				file_infos.emplace_back(info);
			}
		} catch (const std::exception& e) {
			LOGE("GetFilesListImpl error: {}", e.what());
		}
		return file_infos;
	}

	std::tuple<bool, std::vector<px::FileDescInfo>, std::string, std::string> FileOperate::GetFilesList(std::string path) {
#ifdef WIN32
		try {
			std::string permission_log;
			bool permission = true;
			path = StringUtil::StandardizeWinPath(path);
			do {
				if ("/" != s_file_permission_path_) {
					std::string visitFileInfo = PathToUTF8(std::filesystem::canonical(PathFromUTF8(path)));
					std::string filePermissionFileInfo = PathToUTF8(std::filesystem::canonical(PathFromUTF8(s_file_permission_path_)));
					if (visitFileInfo == filePermissionFileInfo) {
						break;
					}
					std::string visit_path_str = PathToUTF8(std::filesystem::path(PathFromUTF8(visitFileInfo)).parent_path());
					std::string permission_path_str = PathToUTF8(std::filesystem::path(PathFromUTF8(filePermissionFileInfo)).parent_path());
					if (StringUtil::StartWith(visit_path_str, permission_path_str)) {
						permission = false;
						break;
					}
				}
			} while (0);

			if (!permission) {
				permission_log = "The accessed path is not authorized and has been switched to an authorized path The authorization path is:" + s_file_permission_path_;
				path = s_file_permission_path_;
			}
			if (root_path_ == path) {
				return { true, GetThisPCFiles(), "", s_file_permission_path_};
			}
			if (!std::filesystem::exists(PathFromUTF8(path))) {
				return { false, {}, permission_log + std::string("The accessed directory no longer exists"), s_file_permission_path_};
			}
			if (!std::filesystem::is_directory(PathFromUTF8(path))) {
				return { false, {}, permission_log + std::string("The accessed path is not a valid folder or disk directory"), s_file_permission_path_};
			}
			std::vector<px::FileDescInfo> file_infos = GetFilesListImpl(path);
			return { true, file_infos, permission_log, s_file_permission_path_ };
		}
		catch (std::exception& e) {
			std::string s = e.what();
			LOGE("GetFilesList path is {}, error is {}", path, s);
			return { false, {}, s, s_file_permission_path_ };
		}
#else
		return { false, {}, std::string("linux file trans unsupport"), ""};
#endif // WIN32
	}

	std::tuple<bool, std::vector<px::FileDescInfo>, std::string, std::string> FileOperate::RecursiveGetFilesList(std::string path) {
#ifdef WIN32
		try {
			std::string permission_log;
			bool permission = true;
			path = StringUtil::StandardizeWinPath(path);
			do {
				if ("/" != s_file_permission_path_) {
					std::string visitFileInfo = PathToUTF8(std::filesystem::canonical(PathFromUTF8(path)));
					std::string filePermissionFileInfo = PathToUTF8(std::filesystem::canonical(PathFromUTF8(s_file_permission_path_)));
					if (visitFileInfo == filePermissionFileInfo) {
						break;
					}
					std::string visit_path_str = PathToUTF8(std::filesystem::path(visitFileInfo).parent_path());
					std::string permission_path_str = PathToUTF8(std::filesystem::path(filePermissionFileInfo).parent_path());
					if (StringUtil::StartWith(visit_path_str, permission_path_str)) {
						permission = false;
						break;
					}
				}
			} while (0);

			if (!permission) {
				permission_log = "The accessed path is not authorized and has been switched to an authorized path The authorization path is:" + s_file_permission_path_;
				path = s_file_permission_path_;
			}

			if (root_path_ == path) {
				return { true, GetThisPCFiles(), "", s_file_permission_path_ };
			}

			if (!std::filesystem::exists(PathFromUTF8(path))) {
				return { false, {}, permission_log + std::string("The accessed directory no longer exists"), s_file_permission_path_ };
			}

			if (!std::filesystem::is_directory(PathFromUTF8(path))) {
				return { false, {}, permission_log + std::string("The accessed path is not a valid folder or disk directory"), s_file_permission_path_ };
			}
			
			std::vector<std::string> folders;
			std::vector<std::string> files;
			TraverseDirectory(path, folders, files);
			std::vector<px::FileDescInfo> file_infos;
			for (auto& folder : folders) {
				px::FileDescInfo info;
				info.set_type(px::FileDescInfo::kFolder);
				info.set_name(PathToUTF8(std::filesystem::path(PathFromUTF8(folder)).filename()));
				info.set_path(PathToUTF8(std::filesystem::absolute(PathFromUTF8(folder))));
				file_infos.emplace_back(info);
			}

			for (auto& file : files) {
				px::FileDescInfo info;
				info.set_type(px::FileDescInfo::kFile);
				info.set_name(PathToUTF8(std::filesystem::path(PathFromUTF8(file)).filename()));
				info.set_path(PathToUTF8(std::filesystem::absolute(PathFromUTF8(file))));
				info.set_size(std::filesystem::file_size(PathFromUTF8(file)));
				auto lwt = std::filesystem::last_write_time(PathFromUTF8(file));
				auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
					lwt - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
				info.set_date(std::chrono::duration_cast<std::chrono::seconds>(sctp.time_since_epoch()).count());
				file_infos.emplace_back(info);
			}
			return { true, file_infos, "", s_file_permission_path_ };
		}
		catch (std::exception& e) {
			std::string s = e.what();
			LOGE("GetFilesList path is {}, error is {}", path, s);
			return { false, {}, s, s_file_permission_path_ };
		}
#else
		return { false, {}, std::string("linux file trans unsupport"), "" };
#endif // WIN32
	}

	void FileOperate::TraverseDirectory(const std::string& path, std::vector<std::string>& folders, std::vector<std::string>& files) {
		try {
			for (const auto& entry : std::filesystem::directory_iterator(PathFromUTF8(path), std::filesystem::directory_options::skip_permission_denied)) {
				auto itemPath = PathToUTF8(entry.path());
				if (std::filesystem::is_directory(entry.status())) {
					folders.push_back(itemPath);
					TraverseDirectory(itemPath, folders, files);
				}
				else if (std::filesystem::is_regular_file(entry.status())) {
					files.push_back(itemPath);
				}
			}
		} catch (const std::exception& e) {
			LOGE("TraverseDirectory error: {}", e.what());
		}
	}

	std::vector<px::FileDescInfo> FileOperate::GetThisPCFiles() {
#ifdef WIN32
		try {
			bool impersonate = false;
			bool query_token = false;
			HANDLE hToken = nullptr;  
			DWORD dwSessionId = ProcessUtil::GetCurrentSessionId();
			if (WTSQueryUserToken(dwSessionId, &hToken)) {
				if (ImpersonateLoggedOnUser(hToken)) {
					impersonate = true;
				}
				else {
					LOGE("Failed to impersonate user. Error code: %d ", GetLastError());
				}
				query_token = true;
			}
			else {
				LOGE("Failed to get user token. Error code: %d", GetLastError());
			}

			std::vector<px::FileDescInfo> file_infos;
			DWORD drives = GetLogicalDrives();
			for (int i = 0; i < 26; i++) {
				if (drives & (1 << i)) {
					char drive_name[4] = { (char)('A' + i), ':', '/', '\0' };
					px::FileDescInfo info;
					info.set_name(drive_name);
					info.set_path(drive_name);
					info.set_type(px::FileDescInfo::kDisk);
					file_infos.emplace_back(info);
				}
			}

			auto get_folder = [](REFKNOWNFOLDERID folderId) -> std::string {
				PWSTR path = nullptr;
				if (SUCCEEDED(SHGetKnownFolderPath(folderId, 0, nullptr, &path)) && path) {
					std::wstring wpath(path);
					CoTaskMemFree(path);
					return StringUtil::ToUTF8(wpath);
				}
				return "";
			};

			{
				px::FileDescInfo desktop_info;
				desktop_info.set_name(tcTr("id_file_trans_desktop"));
				desktop_info.set_path(get_folder(FOLDERID_Desktop));
				desktop_info.set_type(px::FileDescInfo::kDeskFolder);
				desktop_path_ = desktop_info.path();
				file_infos.emplace_back(desktop_info);
			}
			{
				px::FileDescInfo doc_info;
				doc_info.set_name(tcTr("id_file_trans_my_document"));
				doc_info.set_path(get_folder(FOLDERID_Documents));
				doc_info.set_type(px::FileDescInfo::kFolder);
				file_infos.emplace_back(doc_info);
			}
			{
				px::FileDescInfo music_info;
				music_info.set_name(tcTr("id_file_trans_my_music"));
				music_info.set_path(get_folder(FOLDERID_Music));
				music_info.set_type(px::FileDescInfo::kFolder);
				file_infos.emplace_back(music_info);
			}
			{
				px::FileDescInfo pic_info;
				pic_info.set_name(tcTr("id_file_trans_my_picture"));
				pic_info.set_path(get_folder(FOLDERID_Pictures));
				pic_info.set_type(px::FileDescInfo::kFolder);
				file_infos.emplace_back(pic_info);
			}
			{
				px::FileDescInfo mov_info;
				mov_info.set_name(tcTr("id_file_trans_my_video"));
				mov_info.set_path(get_folder(FOLDERID_Videos));
				mov_info.set_type(px::FileDescInfo::kFolder);
				file_infos.emplace_back(mov_info);
			}

			if (impersonate) {
				RevertToSelf();
			}
			if (query_token) {
				CloseHandle(hToken);
			}

			return file_infos;
		}
		catch (std::exception& e) {
			std::string s = e.what();
			LOGE("FileOperate::GetThisPCFiles error is {}.", s);
			return {};
		}
#else
		return {};
#endif
	}

	std::tuple<bool, std::string> FileOperate::CreateFolder(std::string folder_path) {
		bool ret = true;
		std::string er_msg;
		try {
			if (!std::filesystem::exists(PathFromUTF8(folder_path))) {
				std::filesystem::create_directories(PathFromUTF8(folder_path));
			}
			if (!std::filesystem::exists(PathFromUTF8(folder_path))) {
				ret = false;
			}
		}
		catch (std::exception& e) {
			ret = false;
			LOGE("CreateFolderHandle folder_path is {}, error is {}", folder_path, std::string(e.what()));
		}
		return {ret, er_msg};
	}

	std::tuple<bool, std::vector<std::string>, std::string> FileOperate::Remove(const std::vector<std::string>& paths) {
		bool ret = true;
		std::vector<std::string> er_paths;
		std::string er_msg;
		for (auto ph : paths) {
			try {
				if (std::filesystem::is_directory(PathFromUTF8(ph))) {
					if (!std::filesystem::exists(PathFromUTF8(ph))) {
						break;
					}
					std::filesystem::remove_all(PathFromUTF8(ph));
				}
				else if (std::filesystem::is_regular_file(PathFromUTF8(ph))) {
					if (!std::filesystem::exists(PathFromUTF8(ph))) {
						break;
					}
					std::filesystem::remove(PathFromUTF8(ph));
				}
			}
			catch (std::exception& e) {
				er_paths.emplace_back(ph);
				LOGE("Remove error, remove {}, failed is {}.", ph, e.what());
			}
		}
		return {ret, er_paths ,er_msg};
	}

	std::tuple<bool, std::string, std::string> FileOperate::CreateNewFolder(const std::string& parent_path_str) {
		try {
			std::string create_new_folder_path_ = "";
			bool create_new_folder_res_ = false;
			if (!std::filesystem::exists(PathFromUTF8(parent_path_str))) {
				return { false, "", parent_path_str + tcTr("id_file_trans_no_exists") };
			}
			int temp_count = 1;
			std::string prefix = tcTr("id_file_trans_new_folder");
			do {
				std::string suffix;
				if (1 == temp_count) {
					suffix = "";
				}
				else {
					suffix = "(" + std::to_string(temp_count) + ")";
				}

				std::string new_folder_name = prefix + suffix;
				create_new_folder_path_ = parent_path_str + "/" + new_folder_name;
				if (std::filesystem::exists(PathFromUTF8(create_new_folder_path_))) {
					++temp_count;
					continue;
				}
				std::filesystem::create_directories(PathFromUTF8(create_new_folder_path_));
				create_new_folder_res_ = true;
				break;
			} while (true);
			return { create_new_folder_res_, create_new_folder_res_ ? create_new_folder_path_ : "", ""};
		}
		catch (std::exception& e) {
			std::string s = e.what();
			LOGE("FileOperate::CreateNewFolder parent_path is {} error is {}", parent_path_str, s);
			return { false, "", tcTr("id_file_trans_new_folder_failed") + s};
		}
	}

	std::tuple<bool, uint64_t, uint64_t> FileOperate::IsExists(const std::string& u8_path) {
		try { 
			bool ret = std::filesystem::exists(PathFromUTF8(u8_path));
			uint64_t file_size = 0;
			uint64_t date_changed = 0;
			if (ret) {
				file_size = std::filesystem::file_size(PathFromUTF8(u8_path));
				auto lwt = std::filesystem::last_write_time(PathFromUTF8(u8_path));
				auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
					lwt - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
				date_changed = std::chrono::duration_cast<std::chrono::seconds>(sctp.time_since_epoch()).count();
			}
			return {ret, file_size, date_changed};
		}
		catch (std::exception& e) {
			std::string s = e.what();
			LOGE(" FileOperate::IsExists error is {}", s);
			return {false, 0, 0};
		}
	}

	std::tuple<bool, std::string, std::string> FileOperate::Rename(const std::string& u8_old_path, const std::string& u8_new_name) {
		try {
			if (!std::filesystem::exists(PathFromUTF8(u8_old_path))) {
				return { false, "", tcTr("id_file_trans_target_path_no_exists")};
			}
			std::string new_file_path = PathToUTF8((PathFromUTF8(u8_old_path).parent_path() / PathFromUTF8(u8_new_name)));
			if (std::filesystem::exists(PathFromUTF8(new_file_path))) {
				return { false, "", tcTr("id_file_trans_rename_failed_name_occypy") };
			}

			std::filesystem::rename(PathFromUTF8(u8_old_path), PathFromUTF8(new_file_path));
			return { true, new_file_path, "" };
		}
		catch (std::exception& e) {
			std::string s = e.what();
			LOGE("FileOperate::rename error is {}", s);
			return { false, "", tcTr("id_file_trans_rename_failed") + s};
		}
	}
}
