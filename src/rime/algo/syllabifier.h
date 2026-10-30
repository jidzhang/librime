//
// Copyright RIME Developers
// Distributed under the BSD License
//
// 2011-7-12 Zou xu <zouivex@gmail.com>
//

#ifndef RIME_SYLLABIFIER_H_
#define RIME_SYLLABIFIER_H_

#include <stdint.h>
#include <rime_api.h>
#include <rime/common.h>
#include "spelling.h"

namespace rime {

class Corrector;
class Prism;
class Projection;

using SyllableId = int32_t;

struct EdgeProperties : SpellingProperties {
  EdgeProperties(SpellingProperties sup) : SpellingProperties(sup) {};
  EdgeProperties() = default;
  // 切分歧義編碼段的起始位置
  set<size_t> ambiguous_source_positions;
};

using SpellingMap = map<SyllableId, EdgeProperties>;
using VertexMap = map<size_t, SpellingType>;
using EndVertexMap = map<size_t, SpellingMap>;
using EdgeMap = map<size_t, EndVertexMap>;

using SpellingPropertiesList = vector<const EdgeProperties*>;
using SpellingIndex = map<SyllableId, SpellingPropertiesList>;
using SpellingIndices = map<size_t, SpellingIndex>;

struct SyllableGraph {
  size_t input_length = 0;
  size_t interpreted_length = 0;
  VertexMap vertices;
  EdgeMap edges;
  SpellingIndices indices;
};

class Syllabifier {
 public:
  Syllabifier() = default;
  explicit Syllabifier(const string& delimiters,
                       bool enable_completion = false,
                       bool strict_spelling = false,
                       Projection* canonicalizer = nullptr)
      : delimiters_(delimiters),
        enable_completion_(enable_completion),
        strict_spelling_(strict_spelling),
        canonicalizer_(canonicalizer) {}

  RIME_DLL int BuildSyllableGraph(const string& input,
                                  Prism& prism,
                                  SyllableGraph* graph);
  RIME_DLL void EnableCorrection(Corrector* corrector);
  // 换位容错（translator/enable_transpose）：对每个顶点的剩余输入做相邻字符
  // 互换变体的前缀搜索，命中边按纠错边处理（is_correction + credibility
  // 惩罚）。
  // 与邻键替换类纠错不同：有效输入的换位变体几乎不成有效音节，污染面小。
  RIME_DLL void EnableTranspose(double credibility = -4.605170185988091) {
    enable_transpose_ = true;
    transpose_credibility_ = credibility;
  }
  // 漏字容错（translator/enable_deletion）：剩余输入删去一位后做前缀搜索，
  // 边多消耗一个原字符（end_pos 前移 1）。与换位同款纠错边语义。
  RIME_DLL void EnableDeletion(double credibility = -4.605170185988091) {
    enable_deletion_ = true;
    deletion_credibility_ = credibility;
  }
  // 邻键替换容错（translator/enable_substitution）：剩余输入第 i 位（i∈{0,1}）
  // 换成 QWERTY 物理邻键后做前缀搜索，取最长匹配（须覆盖被替换位）。
  RIME_DLL void EnableSubstitution(double credibility = -4.605170185988091) {
    enable_substitution_ = true;
    substitution_credibility_ = credibility;
  }
  // 漏打容错（translator/enable_insertion）：剩余输入第 i 位（i∈{0,1}）插入
  // 任一字母后做前缀搜索，插入字符是虚拟的（边少消耗一个原字符）。
  RIME_DLL void EnableInsertion(double credibility = -4.605170185988091) {
    enable_insertion_ = true;
    insertion_credibility_ = credibility;
  }
  // 退化顶点跟踪（二遍拼接的触发信号）：记录本次 BuildSyllableGraph 是否
  // 出现过"开不出任何全拼拼写"的顶点（结构断裂），以及是否存在能开出
  // 全拼拼写的顶点（纯简拼输入全部退化，不跑二遍）。
  RIME_DLL void EnableDegenerateTracking() { track_degenerate_vertex_ = true; }
  bool HasDegenerateVertex() const { return found_degenerate_vertex_; }
  bool HasNormalVertex() const { return found_normal_vertex_; }
  // 输入能否按"全拼"解释掉（二遍质量门：能解释=用户还在正常打字/补全中）：
  // ①整串可被全拼音节完整覆盖（合法拼音）；②已覆盖前缀 + 尾巴是某全拼
  // 音节的前缀（wofe→wofei）。只看 kNormalSpelling——简拼/补全边不算，
  // 否则 wokde 会被当成合法简拼放过；分隔符跳过（与图构建同口径）。
  RIME_DLL bool IsNormalPinyinPrefix(const string& input, Prism& prism);

 protected:
  void CheckOverlappedSpellings(SyllableGraph* graph, size_t start, size_t end);
  void Transpose(SyllableGraph* graph);

  string delimiters_;
  bool enable_completion_ = false;
  bool strict_spelling_ = false;
  Projection* canonicalizer_ = nullptr;
  bool enable_transpose_ = false;
  double transpose_credibility_ = -4.605170185988091;
  bool enable_deletion_ = false;
  double deletion_credibility_ = -4.605170185988091;
  bool enable_substitution_ = false;
  double substitution_credibility_ = -4.605170185988091;
  bool enable_insertion_ = false;
  double insertion_credibility_ = -4.605170185988091;
  bool track_degenerate_vertex_ = false;
  bool found_degenerate_vertex_ = false;
  bool found_normal_vertex_ = false;
  Corrector* corrector_ = nullptr;
};

}  // namespace rime

#endif  // RIME_SYLLABIFIER_H_
