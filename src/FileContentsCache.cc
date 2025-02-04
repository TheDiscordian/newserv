#include "FileContentsCache.hh"

#include <unistd.h>

#include <phosg/Filesystem.hh>
#include <phosg/Time.hh>

using namespace std;



FileContentsCache::FileContentsCache(uint64_t ttl_usecs) : ttl_usecs(ttl_usecs) { }

FileContentsCache::File::File(
    const string& name,
    string&& data,
    uint64_t load_time)
  : name(name), data(new string(move(data))), load_time(load_time) { }

shared_ptr<const FileContentsCache::File> FileContentsCache::replace(
    const string& name, string&& data, uint64_t t) {
  if (t == 0) {
    t = now();
  }
  shared_ptr<File> new_file(new File(name, move(data), t));
  auto emplace_ret = this->name_to_file.emplace(name, new_file);
  if (!emplace_ret.second) {
    emplace_ret.first->second = new_file;
  }
  return new_file;
}

shared_ptr<const FileContentsCache::File> FileContentsCache::replace(
    const string& name, const void* data, size_t size, uint64_t t) {
  string s(reinterpret_cast<const char*>(data), size);
  return this->replace(name, move(s), t);
}

FileContentsCache::GetResult FileContentsCache::get_or_load(const std::string& name) {
  return this->get(name, load_file);
}

FileContentsCache::GetResult FileContentsCache::get_or_load(const char* name) {
  return this->get_or_load(string(name));
}

shared_ptr<const FileContentsCache::File> FileContentsCache::get_or_throw(
    const std::string& name) {
  auto throw_fn = +[](const std::string&) -> string {
    throw out_of_range("file missing from cache");
  };
  return this->get(name, throw_fn).file;
}

shared_ptr<const FileContentsCache::File> FileContentsCache::get_or_throw(
    const char* name) {
  return this->get_or_throw(string(name));
}

FileContentsCache::GetResult FileContentsCache::get(const std::string& name,
    std::function<std::string(const std::string&)> generate) {
  uint64_t t = now();
  try {
    auto& entry = this->name_to_file.at(name);
    if (this->ttl_usecs && (t - entry->load_time < this->ttl_usecs)) {
      return {entry, false};
    }
  } catch (const out_of_range& e) { }
  return {this->replace(name, generate(name)), true};
}

FileContentsCache::GetResult FileContentsCache::get(const char* name,
    std::function<std::string(const std::string&)> generate) {
  return this->get(string(name), generate);
}
