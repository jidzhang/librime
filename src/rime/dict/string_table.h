//
// Copyright RIME Developers
// Distributed under the BSD License
//
// 2014-06-25 GONG Chen <chen.sst@gmail.com>
//

#ifndef RIME_STRING_TABLE_H_
#define RIME_STRING_TABLE_H_

#include <cstdint>
#include <string_view>
#include <utility>
#include <marisa.h>
#include <rime_api.h>
#include <rime/common.h>

namespace rime {

using StringId = std::uint32_t;

const StringId kInvalidStringId = (StringId)(-1);

class RIME_DLL StringTable {
 public:
  StringTable() = default;
  virtual ~StringTable() = default;
  StringTable(const char* ptr, size_t size);

  bool HasKey(const string& key);
  StringId Lookup(const string& key);
  void CommonPrefixMatch(const string& query, vector<StringId>* result);
  void Predict(const string& query, vector<StringId>* result);
  string GetString(StringId string_id);
  // pgomgr 2026-10-31：GetString 每次构造 marisa::Agent + string(ptr,len) 堆分配，
  // 每键 × N 候选 = 3N 次 string 堆分配。string_view 版指向 trie 内部缓冲区，
  // 零拷贝；调用方仅在需要持有文本时才 assign 进自己的 string。
  std::string_view GetStringView(StringId string_id);

  size_t NumKeys() const;
  size_t BinarySize() const;

 protected:
  marisa::Trie trie_;
  // reverse_lookup 结果的缓冲区复用：Agent 的 key() 指向其内部 key 存储，
  // Agent 析构后指针即悬空——必须持有 Agent 才能用 string_view。
  marisa::Agent lookup_agent_;
};

class RIME_DLL StringTableBuilder : public StringTable {
 public:
  void Add(const string& key,
           double weight = 1.0,
           StringId* reference = nullptr);
  void Clear();
  void Build();
  void Dump(char* ptr, size_t size);

 private:
  void UpdateReferences();

  marisa::Keyset keys_;
  vector<StringId*> references_;
};

}  // namespace rime

#endif  // RIME_STRING_TABLE_H_
