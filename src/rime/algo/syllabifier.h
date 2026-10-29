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
  // 互换变体的前缀搜索，命中边按纠错边处理（is_correction + credibility 惩罚）。
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
  RIME_DLL void EnableSubstitution(double credibility = -4.605170185988091,
                                   bool horizontal_only = false) {
    enable_substitution_ = true;
    substitution_credibility_ = credibility;
    substitution_horizontal_only_ = horizontal_only;
  }
  // 漏打容错（translator/enable_insertion）：剩余输入第 i 位（i∈{0,1}）插入
  // 任一字母后做前缀搜索，插入字符是虚拟的（边少消耗一个原字符）。
  RIME_DLL void EnableInsertion(double credibility = -4.605170185988091) {
    enable_insertion_ = true;
    insertion_credibility_ = credibility;
  }
  // 退化顶点门控（translator/typo_gate_degenerate，默认 false）：删除/替换/
  // 插入三类纠错边仅在"退化顶点"（该位置开不出任何 kNormalSpelling 匹配，
  // 即全拼结构已断）触发。换位边不受门控（已产品化）。依据 2026-10-29 d7
  // 逐句归因：无门控删除边打坏 d5 的 68 句中 23 句直接删除路径有 22 句
  // 落在非退化顶点，此门可挡；同音排序涟漪随边消失。
  RIME_DLL void EnableDegenerateGate() { typo_gate_degenerate_ = true; }
  // 退化顶点跟踪（typo_second_pass 二遍触发信号）：BuildSyllableGraph 期间
  // 记录是否出现过"开不出任何全拼拼写"的顶点，供翻译层决定是否跑二遍。
  RIME_DLL void EnableDegenerateTracking() { track_degenerate_vertex_ = true; }
  bool has_degenerate_vertex() const { return found_degenerate_vertex_; }
  // 输入能否按"全拼"解释掉（typo_second_pass 质量门，2026-10-29 用户定
  // "纠错不要做过了：候选已明确靠近目标就不该触发"）：
  // ①整串可被全拼音节完整覆盖 → true（合法拼音，不需要纠错）；
  // ②已覆盖前缀 + 剩余尾巴是某个全拼音节的前缀（wofe→wofei）→ true
  //（用户还在打字，属补全场景）。
  // 只看 kNormalSpelling，简拼/补全边不算——否则 wokde 会被当"合法简拼"放过。
  RIME_DLL bool IsNormalPinyinPrefix(const string& input, Prism& prism);
  // 输入里是否存在"能开出全拼拼写"的顶点（typo_second_pass 触发前置：
  // 纯简拼输入全顶点退化、无全拼顶点，不跑二遍——jp2/d6 守卫与开销双收）
  bool has_normal_vertex() const { return found_normal_vertex_; }
  RIME_DLL void SetTypoEdgesEnabled(bool on) {
    typo_edges_enabled_ = on && (enable_transpose_ || enable_deletion_ ||
                                 enable_substitution_ || enable_insertion_);
  }

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
  bool substitution_horizontal_only_ = false;
  bool enable_insertion_ = false;
  double insertion_credibility_ = -4.605170185988091;
  bool typo_gate_degenerate_ = false;
  bool track_degenerate_vertex_ = false;
  bool found_degenerate_vertex_ = false;
  bool found_normal_vertex_ = false;
  // 纠错边生效开关（translator/typo_gated 二遍查询用；默认 true=行为同旧）
  bool typo_edges_enabled_ = true;
  Corrector* corrector_ = nullptr;
};

}  // namespace rime

#endif  // RIME_SYLLABIFIER_H_
