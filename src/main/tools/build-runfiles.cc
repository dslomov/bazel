// Copyright 2014 The Bazel Authors. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// This program creates a "runfiles tree" from a "runfiles manifest".
//
// The command line arguments are an input manifest INPUT and an output
// directory RUNFILES. First, the files in the RUNFILES directory are scanned
// and any extraneous ones are removed. Second, any missing files are created.
// Finally, a copy of the input manifest is written to RUNFILES/MANIFEST.
//
// The input manifest consists of lines, each containing a relative path within
// the runfiles, a space, and an optional absolute path.  If this second path
// is present, a symlink is created pointing to it; otherwise an empty file is
// created.
//
// Given the line
//   <workspace root>/output/path /real/path
// we will create directories
//   RUNFILES/<workspace root>
//   RUNFILES/<workspace root>/output
// a symlink
//   RUNFILES/<workspace root>/output/path -> /real/path
// and the output manifest will contain a line
//   <workspace root>/output/path /real/path
//
// If --use_metadata is supplied, every other line is treated as opaque
// metadata, and is ignored here.
//
// All output paths must be relative and generally (but not always) begin with
// <workspace root>. No output path may be equal to another.  No output path may
// be a path prefix of another.

#define _FILE_OFFSET_BITS 64

#include <algorithm>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include <map>
#include <string>

#ifdef __CYGWIN__
#include <locale>
#include <codecvt>
#define _WIN32_WINNT 0x0701
#include <windows.h>
#endif

// program_invocation_short_name is not portable.
static const char *argv0;

const char *input_filename;
const char *output_base_dir;

#define LOG() { \
  fprintf(stderr, "%s (args %s %s): ", \
          argv0, input_filename, output_base_dir); \
}

#define DIE(args...) { \
  LOG(); \
  fprintf(stderr, args); \
  fprintf(stderr, "\n"); \
  exit(1); \
}

#define PDIE(args...) { \
  int saved_errno = errno; \
  LOG(); \
  fprintf(stderr, args); \
  fprintf(stderr, ": %s [%d]\n", strerror(saved_errno), saved_errno); \
  exit(1); \
}

enum FileType {
  FILE_TYPE_REGULAR,
  FILE_TYPE_DIRECTORY,
  FILE_TYPE_SYMLINK
};

enum LinkAlgorithm {
  LINK_ALGORITHM_SYMLINK,
  LINK_ALGORITHM_HARDLINK,
  LINK_ALGORITHM_JUNCTION,
};

struct FileInfo {
  FileType type;
  std::string symlink_target;

  bool operator==(const FileInfo &other) const {
    return type == other.type && symlink_target == other.symlink_target;
  }

  bool operator!=(const FileInfo &other) const {
    return !(*this == other);
  }
};

typedef std::map<std::string, FileInfo> FileInfoMap;

#ifdef __CYGWIN__
void DieWithWindowsError(const std::string& op) {
  DWORD last_error = ::GetLastError();
  if (last_error == 0) {
      return;
  }

  char* message_buffer;
  size_t size = FormatMessageA(
      FORMAT_MESSAGE_ALLOCATE_BUFFER
          | FORMAT_MESSAGE_FROM_SYSTEM
          | FORMAT_MESSAGE_IGNORE_INSERTS,
      NULL,
      last_error,
      MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
      (LPSTR) &message_buffer,
      0,
      NULL);

  fprintf(stderr, "ERROR: %s: %s (%d)\n",
          op.c_str(), message_buffer, last_error);
  LocalFree(message_buffer);
  exit(1);
}
#endif

class RunfilesCreator {
 public:
  explicit RunfilesCreator(const std::string &output_base, bool windows_compatible)
      : output_base_(output_base),
        windows_compatible_(windows_compatible),
        output_filename_("MANIFEST"),
        temp_filename_(output_filename_ + ".tmp") {
    SetupOutputBase();
    if (chdir(output_base_.c_str()) != 0) {
      PDIE("chdir '%s'", output_base_.c_str());
    }
  }

  void ReadManifest(const std::string &manifest_file, bool allow_relative,
                    bool use_metadata) {
    FILE *outfile = fopen(temp_filename_.c_str(), "w");
    if (!outfile) {
      PDIE("opening '%s/%s' for writing", output_base_.c_str(),
           temp_filename_.c_str());
    }
    FILE *infile = fopen(manifest_file.c_str(), "r");
    if (!infile) {
      PDIE("opening '%s' for reading", manifest_file.c_str());
    }

    // read input manifest
    int lineno = 0;
    char buf[3 * PATH_MAX];
    while (fgets(buf, sizeof buf, infile)) {
      // copy line to output manifest
      if (fputs(buf, outfile) == EOF) {
        PDIE("writing to '%s/%s'", output_base_.c_str(),
             temp_filename_.c_str());
      }

      // parse line
      ++lineno;
      // Skip metadata lines. They are used solely for
      // dependency checking.
      if (use_metadata && lineno % 2 == 0) continue;

      int n = strlen(buf)-1;
      if (!n || buf[n] != '\n') {
        DIE("missing terminator at line %d: '%s'\n", lineno, buf);
      }
      buf[n] = '\0';
      if (buf[0] ==  '/') {
        DIE("paths must not be absolute: line %d: '%s'\n", lineno, buf);
      }
      const char *s = strchr(buf, ' ');
      if (!s) {
        DIE("missing field delimiter at line %d: '%s'\n", lineno, buf);
      } else if (strchr(s+1, ' ')) {
        DIE("link or target filename contains space on line %d: '%s'\n",
            lineno, buf);
      }
      std::string link(buf, s-buf);
      const char *target = s+1;
      if (!allow_relative && target[0] != '\0' && target[0] != '/'
          && target[1] != ':') {  // Match Windows paths, e.g. C:\foo or C:/foo.
        DIE("expected absolute path at line %d: '%s'\n", lineno, buf);
      }

      FileInfo *info = &manifest_[link];
      if (target[0] == '\0') {
        // No target means an empty file.
        info->type = FILE_TYPE_REGULAR;
      } else {
        info->type = FILE_TYPE_SYMLINK;
        info->symlink_target = target;
      }

      FileInfo parent_info;
      parent_info.type = FILE_TYPE_DIRECTORY;

      while (true) {
        int k = link.rfind('/');
        if (k < 0) break;
        link.erase(k, std::string::npos);
        if (!manifest_.insert(std::make_pair(link, parent_info)).second) break;
      }
    }
    if (fclose(outfile) != 0) {
      PDIE("writing to '%s/%s'", output_base_.c_str(),
           temp_filename_.c_str());
    }
    fclose(infile);

    // Don't delete the temp manifest file.
    manifest_[temp_filename_].type = FILE_TYPE_REGULAR;
  }

  void CreateRunfiles() {
    if (unlink(output_filename_.c_str()) != 0 && errno != ENOENT) {
      PDIE("removing previous file at '%s/%s'", output_base_.c_str(),
           output_filename_.c_str());
    }

    ScanTreeAndPrune(".");
    CreateFiles();

    // rename output file into place
    if (rename(temp_filename_.c_str(), output_filename_.c_str()) != 0) {
      PDIE("renaming '%s/%s' to '%s/%s'",
           output_base_.c_str(), temp_filename_.c_str(),
           output_base_.c_str(), output_filename_.c_str());
    }
  }

 private:
#ifdef __CYGWIN__  
  // Move the given path to a "trash" directory. The path can be either
  // directory or a file.  
  void MoveToTrash(const std::string& path) {
    static const char* trash_directory_name = "bazel-trash";
    struct stat st;
    if (stat(trash_directory_name, &st) != 0) {
      if (mkdir(trash_directory_name, 0777) != 0) {
        PDIE("creating '%s'", trash_directory_name);
      }
    }
    bool success = false;

    TCHAR full_path[MAX_PATH]; 
    if (!::GetFullPathName(path.c_str(), MAX_PATH, full_path, NULL)) {
      DieWithWindowsError("GetFullPathName");
    }

    std::string unc_full_path = std::string("\\\\?\\") + std::string(full_path);

    // Attempt to generate unique name and move the file/directory.
    // If move fails because the target exists, then we are racing 
    // with ourselves - generate a new name and retry.
    for (int attempts = 3; attempts > 0; attempts--) {
      TCHAR temp_trash_name[MAX_PATH + 1];

      snprintf(temp_trash_name, MAX_PATH, 
        // The name has a current time and a rand component.
        // ::GetTickCount() has 10-20ms resolution.
        // If we are racing with ourselves within that timeslice,
        // continuous iterations of rand() ensure progress.  
        "%s/%u%d", trash_directory_name, ::GetTickCount(), rand() % 0xFFFF);
      TCHAR full_temp_trash_name[MAX_PATH + 1];
      if (!::GetFullPathName(temp_trash_name, MAX_PATH, full_temp_trash_name, NULL)) {
        DieWithWindowsError("GetFullPathName");
      }       
      std::string unc_full_trash_name = std::string("\\\\?\\") + std::string(full_temp_trash_name);

      if (::MoveFileEx(unc_full_path.c_str(), unc_full_trash_name.c_str(), MOVEFILE_WRITE_THROUGH)) {
        success = true;
        break;
      }

      if (ERROR_FILE_EXISTS != ::GetLastError() && ERROR_ACCESS_DENIED != ::GetLastError()) {
        fprintf(stderr, "%lu: %lu Moving %s to %s\n", ::GetCurrentProcessId(), ::GetLastError(),
                unc_full_path.c_str(), unc_full_trash_name.c_str());
        exit(1);

        DieWithWindowsError("MoveFileEx");
        return;
      }           
    }
    if (!success) {
      DieWithWindowsError("MoveFileEx after 3 attempts");
    }    
  }

#endif

  void SetupOutputBase() {    
    struct stat st;
    if (stat(output_base_.c_str(), &st) != 0) {
      // Technically, this will cause problems if the user's umask contains
      // 0200, but we don't care. Anyone who does that deserves what's coming.
      if (mkdir(output_base_.c_str(), 0777) != 0) {
        PDIE("creating directory '%s'", output_base_.c_str());
      }
    } else {
      EnsureDirReadAndWritePerms(output_base_);
    }
  }

#ifdef __CYGWIN__
  bool CheckIfHardlinkPointsTo(const std::string& path, const std::string& target) {
    fprintf(stderr, "Trying:%s '%s'\n", path.c_str(), target.c_str());
    //std::wstring_convert<std::codecvt<wchar_t, char, std::mbstate_t>> converter;
    std::wstring wpath(path.begin(), path.end());
    std::wstring wtarget(target.begin(), target.end());

    //fprintf(stderr, "wpath = %s\n", converter.to_bytes(wpath).c_str());
    wchar_t full_path[MAX_PATH]; 
    if (!GetFullPathNameW(wpath.c_str(), MAX_PATH, full_path, NULL)) {
      DieWithWindowsError(std::string("GetFullPathName:" ) + path);
    }
    //fprintf(stderr, "full_path = %s\n", converter.to_bytes(full_path).c_str());


    WCHAR search_result_buffer[PATH_MAX];
    DWORD buffer_len = ARRAYSIZE(search_result_buffer);
    HANDLE hFind = FindFirstFileNameW(full_path, 0, &buffer_len, search_result_buffer);
    if (hFind == INVALID_HANDLE_VALUE) {
      fwprintf(stderr, L"%s\n", full_path);
      DieWithWindowsError(std::string("FindFirstFileNameW: ") + path);
    }

    wchar_t volume_root[MAX_PATH];
    if (!::GetVolumePathNameW(wpath.c_str(), volume_root, ARRAYSIZE(volume_root))) {
      DieWithWindowsError("GetVolumePathName");
    }
    if (volume_root[wcslen(volume_root) - 1] == L'\\') {
      volume_root[wcslen(volume_root) - 1] = L'\0';
    }
    std::wstring wvolume_root(volume_root);
    std::string normalized_target = target;
    std::replace(normalized_target.begin(), normalized_target.end(), '/', '\\');
    while(true) {
      std::wstring wresult = wvolume_root + std::wstring(search_result_buffer);
      std::string result(wresult.begin(), wresult.end());
      fprintf(stderr, "result:%s\ntarget:%s\n", result.c_str(), normalized_target.c_str());
      if (result == normalized_target) {
        fprintf(stderr, "Found!\n");
        //return true;
      }
      buffer_len = ARRAYSIZE(search_result_buffer);
      if (!FindNextFileNameW(hFind, &buffer_len, search_result_buffer)) {
        if (ERROR_HANDLE_EOF == ::GetLastError()) {
          fprintf(stderr, "Not found!\n");
          FindClose(hFind);
          return false;
        }
        DieWithWindowsError(std::string("FindNextFileNameW: ") + path);
      } 
    }
  }
#endif  


  void ScanTreeAndPrune(const std::string &path) {
    // A note on non-empty files:
    // We don't distinguish between empty and non-empty files. That is, if
    // there's a file that has contents, we don't truncate it here, even though
    // the manifest supports creation of empty files, only. Given that
    // .runfiles are *supposed* to be immutable, this shouldn't be a problem.
    EnsureDirReadAndWritePerms(path);

    struct dirent *entry;
    DIR *dh = opendir(path.c_str());
    if (!dh) {
      PDIE("opendir '%s'", path.c_str());
    }

    errno = 0;
    const std::string prefix = (path == "." ? "" : path + "/");
    while ((entry = readdir(dh)) != NULL) {
      if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;

      std::string entry_path = prefix + entry->d_name;
      FileInfo actual_info;
      actual_info.type = DentryToFileType(entry_path, entry->d_type);

      if (actual_info.type == FILE_TYPE_SYMLINK) {
        ReadLinkOrDie(entry_path, &actual_info.symlink_target);
      }

      FileInfoMap::iterator expected_it = manifest_.find(entry_path);
      
#ifdef __CYGWIN__
      bool already_correct;
      if (expected_it == manifest_.end()) {
        already_correct = false;
      } else {
        if (expected_it->second.type == FILE_TYPE_SYMLINK) {
          already_correct = CheckIfHardlinkPointsTo(entry_path, expected_it->second.symlink_target);
        } else {
          already_correct = actual_info.type == expected_it->second.type;
        }
      }
#else
      bool already_correct =
       expected_it != manifest_.end() && expected_it->second == actual_info;
#endif
      

      // When windows_compatible is enabled, if the hard link already existing
      // is still
      // in the mainifest, no need to recreate it.
      // Note: here we assume the content won't change, which might not be true
      // in some rare cases.
      if (!already_correct) {
        DelTree(entry_path, actual_info.type);
      } else {
        manifest_.erase(expected_it);
        if (actual_info.type == FILE_TYPE_DIRECTORY) {
          ScanTreeAndPrune(entry_path);
        }
      }

      errno = 0;
    }
    if (errno != 0) {
      PDIE("reading directory '%s'", path.c_str());
    }
    closedir(dh);
  }

  void CreateFiles() {
    for (FileInfoMap::const_iterator it = manifest_.begin();
         it != manifest_.end(); ++it) {
      const std::string &path = it->first;
      switch (it->second.type) {
        case FILE_TYPE_DIRECTORY:
          if (mkdir(path.c_str(), 0777) != 0) {
            PDIE("mkdir '%s'", path.c_str());
          }
          break;
        case FILE_TYPE_REGULAR:
          {
            int fd = open(path.c_str(), O_CREAT|O_EXCL|O_WRONLY, 0555);
            if (fd < 0) {
              PDIE("creating empty file '%s'", path.c_str());
            }
            close(fd);
          }
          break;
        case FILE_TYPE_SYMLINK:
          {
            LinkAlgorithm algorithm;
            const std::string& target = it->second.symlink_target;
            if (windows_compatible_) {
              struct stat st;
              StatOrDie(target.c_str(), &st);
              algorithm = S_ISDIR(st.st_mode)
                  ? LINK_ALGORITHM_JUNCTION : LINK_ALGORITHM_HARDLINK;
            } else {
              algorithm = LINK_ALGORITHM_SYMLINK;
            }

            int (*link_function)(const char *oldpath, const char *newpath);

            switch (algorithm) {
              case LINK_ALGORITHM_JUNCTION:  // Emulated using symlinks
              case LINK_ALGORITHM_SYMLINK: link_function = symlink; break;
              case LINK_ALGORITHM_HARDLINK: link_function = link; break;
              default: PDIE("Unknown link algoritm for '%s'", target.c_str());
            }
            if (link_function(target.c_str(), path.c_str()) != 0) {
              PDIE("symlinking '%s' -> '%s'", path.c_str(), target.c_str());
            }
          }
          break;
      }
    }
  }

  FileType DentryToFileType(const std::string &path, char d_type) {
    if (d_type == DT_UNKNOWN) {
      struct stat st;
      LStatOrDie(path, &st);
      if (S_ISDIR(st.st_mode)) {
        return FILE_TYPE_DIRECTORY;
      } else if (S_ISLNK(st.st_mode)) {
        return FILE_TYPE_SYMLINK;
      } else {
        return FILE_TYPE_REGULAR;
      }
    } else if (d_type == DT_DIR) {
      return FILE_TYPE_DIRECTORY;
    } else if (d_type == DT_LNK) {
      return FILE_TYPE_SYMLINK;
    } else {
      return FILE_TYPE_REGULAR;
    }
  }

  void LStatOrDie(const std::string &path, struct stat *st) {
    if (lstat(path.c_str(), st) != 0) {
      PDIE("lstating file '%s'", path.c_str());
    }
  }

  void StatOrDie(const std::string &path, struct stat *st) {
    if (stat(path.c_str(), st) != 0) {
      PDIE("stating file '%s'", path.c_str());
    }
  }

  void ReadLinkOrDie(const std::string &path, std::string *output) {
    char readlink_buffer[PATH_MAX];
    int sz = readlink(path.c_str(), readlink_buffer, sizeof(readlink_buffer));
    if (sz < 0) {
      PDIE("reading symlink '%s'", path.c_str());
    }
    // readlink returns a non-null terminated string.
    std::string(readlink_buffer, sz).swap(*output);
  }

  void EnsureDirReadAndWritePerms(const std::string &path) {
    const int kMode = 0700;
    struct stat st;
    LStatOrDie(path, &st);
    if ((st.st_mode & kMode) != kMode) {
      int new_mode = (st.st_mode | kMode) & ALLPERMS;
      if (chmod(path.c_str(), new_mode) != 0) {
        PDIE("chmod '%s'", path.c_str());
      }
    }
  }

  void DelTree(const std::string &path, FileType file_type) {
    if (file_type != FILE_TYPE_DIRECTORY) {
      if (unlink(path.c_str()) != 0) {
#ifdef __CYGWIN__        
        MoveToTrash(path);
#else
        PDIE("unlinking '%s'", path.c_str());
#endif        
      }
      return;
    }

    EnsureDirReadAndWritePerms(path);

    struct dirent *entry;
    DIR *dh = opendir(path.c_str());
    if (!dh) {
      PDIE("opendir '%s'", path.c_str());
    }
    errno = 0;
    while ((entry = readdir(dh)) != NULL) {
      if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
      const std::string entry_path = path + '/' + entry->d_name;
      FileType entry_file_type = DentryToFileType(entry_path, entry->d_type);
      DelTree(entry_path, entry_file_type);
      errno = 0;
    }
    if (errno != 0) {
      PDIE("readdir '%s'", path.c_str());
    }
    closedir(dh);
    if (rmdir(path.c_str()) != 0) {
      PDIE("rmdir '%s'", path.c_str());
    }
  }

 private:
  std::string output_base_;
  bool windows_compatible_;
  std::string output_filename_;
  std::string temp_filename_;

  FileInfoMap manifest_;
};

int main(int argc, char **argv) {
  srand(time(NULL));
  argv0 = argv[0];

  argc--; argv++;
  bool allow_relative = false;
  bool use_metadata = false;
  bool windows_compatible = false;

  while (argc >= 1) {
    if (strcmp(argv[0], "--allow_relative") == 0) {
      allow_relative = true;
      argc--; argv++;
    } else if (strcmp(argv[0], "--use_metadata") == 0) {
      use_metadata = true;
      argc--; argv++;
    } else if (strcmp(argv[0], "--windows_compatible") == 0) {
      windows_compatible = true;
      argc--; argv++;
    } else {
      break;
    }
  }

  if (argc != 2) {
    fprintf(stderr, "usage: %s "
            "[--allow_relative] [--use_metadata] [--windows_compatible] "
            "INPUT RUNFILES\n",
            argv0);
    return 1;
  }

  input_filename = argv[0];
  output_base_dir = argv[1];

  std::string manifest_file = input_filename;
  if (input_filename[0] != '/') {
    char cwd_buf[PATH_MAX];
    if (getcwd(cwd_buf, sizeof(cwd_buf)) == NULL) {
      PDIE("getcwd failed");
    }
    manifest_file = std::string(cwd_buf) + '/' + manifest_file;
  }

  RunfilesCreator runfiles_creator(output_base_dir, windows_compatible);
  runfiles_creator.ReadManifest(manifest_file, allow_relative, use_metadata);
  runfiles_creator.CreateRunfiles();

  return 0;
}
