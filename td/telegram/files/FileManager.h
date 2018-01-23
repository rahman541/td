//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/telegram/DialogId.h"
#include "td/telegram/files/FileDb.h"
#include "td/telegram/files/FileGenerateManager.h"
#include "td/telegram/files/FileId.h"
#include "td/telegram/files/FileLoadManager.h"
#include "td/telegram/files/FileLocation.h"
#include "td/telegram/files/FileStats.h"

#include "td/utils/buffer.h"
#include "td/utils/common.h"
#include "td/utils/Container.h"
#include "td/utils/Slice.h"
#include "td/utils/Status.h"

#include <map>
#include <memory>
#include <set>
#include <utility>

namespace td {

enum class FileLocationSource : int8 { None, FromUser, FromDb, FromServer };

class FileNode {
 public:
  FileNode(LocalFileLocation local, RemoteFileLocation remote, GenerateFileLocation generate, int64 size,
           int64 expected_size, string name, string url, DialogId owner_dialog_id, FileEncryptionKey key,
           FileId main_file_id, int8 main_file_id_priority)
      : local_(std::move(local))
      , remote_(std::move(remote))
      , generate_(std::move(generate))
      , size_(size)
      , expected_size_(expected_size)
      , name_(std::move(name))
      , url_(std::move(url))
      , owner_dialog_id_(owner_dialog_id)
      , encryption_key_(std::move(key))
      , main_file_id_(main_file_id)
      , main_file_id_priority_(main_file_id_priority) {
  }
  void set_local_location(const LocalFileLocation &local, int64 ready_size);
  void set_remote_location(const RemoteFileLocation &remote, FileLocationSource source, int64 ready_size);
  void set_generate_location(const GenerateFileLocation &generate);
  void set_size(int64 size);
  void set_expected_size(int64 expected_size);
  void set_name(string name);
  void set_url(string url);
  void set_owner_dialog_id(DialogId owner_id);
  void set_encryption_key(FileEncryptionKey key);

  void set_download_priority(int8 priority);
  void set_upload_priority(int8 priority);
  void set_generate_priority(int8 download_priority, int8 upload_priority);

  void on_changed();
  void on_info_changed();
  void on_pmc_changed();

  bool need_info_flush() const;
  bool need_pmc_flush() const;

  void on_pmc_flushed();
  void on_info_flushed();

 private:
  friend class FileView;
  friend class FileManager;

  LocalFileLocation local_;
  FileLoadManager::QueryId upload_id_ = 0;
  int64 local_ready_size_ = 0;

  RemoteFileLocation remote_;
  FileLoadManager::QueryId download_id_ = 0;
  int64 remote_ready_size_ = 0;

  GenerateFileLocation generate_;
  FileLoadManager::QueryId generate_id_ = 0;

  int64 size_ = 0;
  int64 expected_size_ = 0;
  string name_;
  string url_;
  DialogId owner_dialog_id_;
  FileEncryptionKey encryption_key_;
  FileDbId pmc_id_ = 0;
  std::vector<FileId> file_ids_;

  FileId main_file_id_;

  FileId upload_pause_;
  int8 upload_priority_ = 0;
  int8 download_priority_ = 0;
  int8 generate_priority_ = 0;

  int8 generate_download_priority_ = 0;
  int8 generate_upload_priority_ = 0;

  int8 main_file_id_priority_ = 0;

  FileLocationSource remote_source_ = FileLocationSource::FromUser;

  bool get_by_hash_ = false;

  bool is_download_started_ = false;
  bool generate_was_update_ = false;

  bool need_load_from_pmc_ = false;

  bool pmc_changed_flag_{false};
  bool info_changed_flag_{false};
};

class FileView {
 public:
  FileView() = default;
  explicit FileView(const FileNode *node);

  bool empty() const;

  bool has_local_location() const;
  const FullLocalFileLocation &local_location() const;
  bool has_remote_location() const;
  const FullRemoteFileLocation &remote_location() const;
  bool has_generate_location() const;
  const FullGenerateFileLocation &generate_location() const;

  bool has_url() const;
  const string &url() const;

  const string &name() const;

  DialogId owner_dialog_id() const;

  bool get_by_hash() const;

  FileId file_id() const {
    return node_->main_file_id_;
  }

  int64 size() const;
  int64 expected_size() const;
  bool is_downloading() const;
  int64 local_size() const;
  int64 local_total_size() const;
  bool is_uploading() const;
  int64 remote_size() const;
  string path() const;

  bool can_download_from_server() const;
  bool can_generate() const;
  bool can_delete() const;

  FileType get_type() const {
    if (has_local_location()) {
      return local_location().file_type_;
    }
    if (has_remote_location()) {
      return remote_location().file_type_;
    }
    if (has_generate_location()) {
      return generate_location().file_type_;
    }
    return FileType::Temp;
  }
  bool is_encrypted() const {
    return get_type() == FileType::Encrypted;
  }
  const FileEncryptionKey &encryption_key() const {
    return node_->encryption_key_;
  }

 private:
  const FileNode *node_ = nullptr;
};

class Td;
class FileManager : public FileLoadManager::Callback {
 public:
  class DownloadCallback {
   public:
    DownloadCallback() = default;
    DownloadCallback(const DownloadCallback &) = delete;
    DownloadCallback &operator=(const DownloadCallback &) = delete;
    virtual ~DownloadCallback() = default;
    virtual void on_progress(FileId file_id) {
    }

    virtual void on_download_ok(FileId file_id) = 0;
    virtual void on_download_error(FileId file_id, Status error) = 0;
  };

  class UploadCallback {
   public:
    UploadCallback() = default;
    UploadCallback(const UploadCallback &) = delete;
    UploadCallback &operator=(const UploadCallback &) = delete;
    virtual ~UploadCallback() = default;

    virtual void on_progress(FileId file_id) {
    }

    // After on_upload_ok all uploads of this file will be paused till merge, delete_partial_remote_location or
    // explicit upload request with the same file_id.
    // Also upload may be resumed after some other merges.
    virtual void on_upload_ok(FileId file_id, tl_object_ptr<telegram_api::InputFile> input_file) = 0;
    virtual void on_upload_encrypted_ok(FileId file_id, tl_object_ptr<telegram_api::InputEncryptedFile> input_file) = 0;
    virtual void on_upload_error(FileId file_id, Status error) = 0;
  };

  class Context {
   public:
    virtual void on_new_file(int64 size) = 0;
    virtual void on_file_updated(FileId size) = 0;
    virtual ActorShared<> create_reference() = 0;
    Context() = default;
    Context(const Context &) = delete;
    Context &operator=(const Context &) = delete;
    virtual ~Context() = default;
  };

  explicit FileManager(std::unique_ptr<Context> context);
  FileManager(const FileManager &other) = delete;
  FileManager &operator=(const FileManager &other) = delete;
  FileManager(FileManager &&other) = delete;
  FileManager &operator=(FileManager &&other) = delete;
  ~FileManager() override;

  void init_actor();

  FileId dup_file_id(FileId file_id);

  void on_file_unlink(const FullLocalFileLocation &location);

  FileId register_empty(FileType type);
  Result<FileId> register_local(FullLocalFileLocation location, DialogId owner_dialog_id, int64 size,
                                bool get_by_hash = false, bool force = false) TD_WARN_UNUSED_RESULT;
  FileId register_remote(const FullRemoteFileLocation &location, DialogId owner_dialog_id, int64 size,
                         int64 expected_size, string name = "") TD_WARN_UNUSED_RESULT;
  Result<FileId> register_generate(FileType file_type, string original_path, string conversion,
                                   DialogId owner_dialog_id, int64 expected_size) TD_WARN_UNUSED_RESULT;
  Result<FileId> register_file(FileData data, FileLocationSource file_location_source, const char *source,
                               bool force = false);

  Result<FileId> merge(FileId x_file_id, FileId y_file_id, bool no_sync = false) TD_WARN_UNUSED_RESULT;

  bool set_encryption_key(FileId file_id, FileEncryptionKey key);
  bool set_content(FileId file_id, BufferSlice bytes);

  void download(FileId file_id, std::shared_ptr<DownloadCallback> callback, int32 new_priority);
  void upload(FileId file_id, std::shared_ptr<UploadCallback> callback, int32 new_priority, uint64 upload_order);
  void resume_upload(FileId file_id, std::vector<int> bad_parts, std::shared_ptr<UploadCallback> callback,
                     int32 new_priority, uint64 upload_order);
  bool delete_partial_remote_location(FileId file_id);
  void get_content(FileId file_id, Promise<BufferSlice> promise);

  void delete_file(FileId file_id, Promise<Unit> promise, const char *source);

  void external_file_generate_progress(int64 id, int32 expected_size, int32 local_prefix_size, Promise<> promise);
  void external_file_generate_finish(int64 id, Status status, Promise<> promise);

  static constexpr char PERSISTENT_ID_VERSION = 2;
  Result<string> to_persistent_id(FileId file_id) TD_WARN_UNUSED_RESULT;
  Result<FileId> from_persistent_id(CSlice persistent_id, FileType file_type) TD_WARN_UNUSED_RESULT;
  FileView get_file_view(FileId file_id) const;
  FileView get_sync_file_view(FileId file_id);
  tl_object_ptr<td_api::file> get_file_object(FileId file_id, bool with_main_file_id = true);

  Result<FileId> get_input_thumbnail_file_id(const tl_object_ptr<td_api::InputFile> &thumb_input_file,
                                             DialogId owner_dialog_id, bool is_encrypted) TD_WARN_UNUSED_RESULT;
  Result<FileId> get_input_file_id(FileType type, const tl_object_ptr<td_api::InputFile> &file,
                                   DialogId owner_dialog_id, bool allow_zero, bool is_encrypted,
                                   bool get_by_hash = false) TD_WARN_UNUSED_RESULT;

  vector<tl_object_ptr<telegram_api::InputDocument>> get_input_documents(const vector<FileId> &file_ids);

  template <class T>
  void store_file(FileId file_id, T &storer, int32 ttl = 5) const;

  template <class T>
  FileId parse_file(T &parser);

 private:
  Result<FileId> check_input_file_id(FileType type, Result<FileId> result, bool is_encrypted,
                                     bool allow_zero) TD_WARN_UNUSED_RESULT;

  FileId register_url(string url, FileType file_type, DialogId owner_dialog_id);

  static constexpr int8 FROM_BYTES_PRIORITY = 10;
  using FileNodeId = int32;
  using QueryId = FileLoadManager::QueryId;
  class Query {
   public:
    FileId file_id_;
    enum Type { UploadByHash, Upload, Download, SetContent, Generate } type_;
  };
  struct FileIdInfo {
    FileNodeId node_id_{0};
    bool send_updates_flag_{false};
    bool pin_flag_{false};

    int8 download_priority_{0};
    int8 upload_priority_{0};

    uint64 upload_order_;

    std::shared_ptr<DownloadCallback> download_callback_;
    std::shared_ptr<UploadCallback> upload_callback_;
  };

  ActorShared<> parent_;
  std::unique_ptr<Context> context_;
  std::shared_ptr<FileDbInterface> file_db_;

  FileIdInfo *get_file_id_info(FileId file_id);

  std::map<FullRemoteFileLocation, FileId> remote_location_to_file_id_;
  std::map<FullLocalFileLocation, FileId> local_location_to_file_id_;
  std::map<FullGenerateFileLocation, FileId> generate_location_to_file_id_;
  std::map<FileDbId, int32> pmc_id_to_file_node_id_;
  vector<FileIdInfo> file_id_info_;
  vector<FileId> empty_file_ids_;
  vector<std::unique_ptr<FileNode>> file_nodes_;
  ActorOwn<FileLoadManager> file_load_manager_;
  ActorOwn<FileGenerateManager> file_generate_manager_;

  Container<Query> queries_container_;

  std::set<std::string> bad_paths_;

  FileId next_file_id();
  FileNodeId next_file_node_id();
  int32 next_pmc_file_id();
  FileId create_file_id(int32 file_node_id, FileNode *file_node);
  void try_forget_file_id(FileId file_id);

  void load_from_pmc(FileId file_id, FullLocalFileLocation full_local);
  void load_from_pmc(FileId file_id, const FullRemoteFileLocation &full_remote);
  void load_from_pmc(FileId file_id, const FullGenerateFileLocation &full_generate);
  template <class LocationT>
  void load_from_pmc_impl(FileId file_id, const LocationT &location);
  void load_from_pmc_result(FileId file_id, Result<FileData> &&result);
  FileId register_pmc_file_data(FileData &&data);

  Status check_local_location(FileNode *node);
  Status check_local_location(FullLocalFileLocation &location, int64 &size);
  void try_flush_node(FileNode *node, bool new_remote = false, bool new_local = false, bool new_generate = false,
                      FileDbId other_pmc_id = Auto());
  void try_flush_node_info(FileNode *node);
  void clear_from_pmc(FileNode *node);
  void flush_to_pmc(FileNode *node, bool new_remote, bool new_local, bool new_generate);
  bool load_from_pmc(FileNode *node, bool new_remote, bool new_local, bool new_generate);

  string get_persistent_id(const FullRemoteFileLocation &location);

  string fix_file_extension(Slice file_name, Slice file_type, Slice file_extension);
  string get_file_name(FileType file_type, Slice path);

  const FileNode *get_file_node(FileId file_id, FileNodeId *file_node_id = nullptr) const;
  FileNode *get_file_node(FileId file_id, FileNodeId *file_node_id = nullptr);
  FileNode *get_sync_file_node(FileId file_id, FileNodeId *file_node_id = nullptr);

  // void release_file_node(FileNodeId id);
  void cancel_download(FileNode *node);
  void cancel_upload(FileNode *node);
  void cancel_generate(FileNode *node);
  void run_upload(FileNode *node, std::vector<int> bad_parts);
  void run_download(FileNode *node);
  void run_generate(FileNode *node);

  void on_start_download(QueryId query_id) override;
  void on_partial_download(QueryId query_id, const PartialLocalFileLocation &partial_local, int64 ready_size) override;
  void on_partial_upload(QueryId query_id, const PartialRemoteFileLocation &partial_remote, int64 ready_size) override;
  void on_download_ok(QueryId query_id, const FullLocalFileLocation &local, int64 size) override;
  void on_upload_ok(QueryId query_id, FileType file_type, const PartialRemoteFileLocation &partial_remote,
                    int64 size) override;
  void on_upload_full_ok(QueryId query_id, const FullRemoteFileLocation &remote) override;
  void on_error(QueryId query_id, Status status) override;

  void on_error_impl(FileNode *node, Query::Type type, bool was_active, Status status);

  void on_partial_generate(QueryId, const PartialLocalFileLocation &partial_local, int32 expected_size);
  void on_generate_ok(QueryId, const FullLocalFileLocation &local);

  std::pair<Query, bool> finish_query(QueryId query_id);

  void hangup() override;
  void tear_down() override;
};
}  // namespace td
