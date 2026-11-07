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
  // 词首多打删除（translator/enable_word_initial_deletion）：首遍限定
  // "输入首位置的删除边"——hshangxian（shang 前多打 h）类错字在二遍
  // 修不到。仅词首单边，且只在输入**不是**合法全拼前缀时触发（合法
  // 全拼不怀疑首字母，否则整句全拼 d5 被假路径拉低实测 -4.4）。
  RIME_DLL void EnableWordInitialDeletion(double credibility =
                                              -4.605170185988091) {
    enable_word_initial_deletion_ = true;
    word_initial_deletion_credibility_ = credibility;
  }
  // 非法全拼前缀的首遍纠错边：当输入串不能被完整全拼解释时，
  // 在首遍也放行 del/sub/ins——用户确实在打错字，首遍候选本来就是垃圾，
  // "主列表零污染"的保护此时无意义，应让纠错候选同池竞争。
  RIME_DLL void EnableDegenerateFirstPass(
      bool enable_del, double del_cred,
      bool enable_sub, double sub_cred,
      bool enable_ins, double ins_cred) {
    degenerate_first_pass_ = true;
    if (enable_del) {
      degenerate_del_cred_ = del_cred;
      has_degenerate_del_ = true;
    }
    if (enable_sub) {
      degenerate_sub_cred_ = sub_cred;
      has_degenerate_sub_ = true;
    }
    if (enable_ins) {
      degenerate_ins_cred_ = ins_cred;
      has_degenerate_ins_ = true;
    }
  }
  // 纠错边扫描窗口（translator/correction_scan_window，默认 2）：四类纠错边
  // 在每个顶点探测剩余输入前 N 个字母位置。2=历史行为（只罩下一音节声母
  // 区，韵母区盲区见 bug#26）；4/5 覆盖韵母起始区。候选边数随窗口线性增长。
  RIME_DLL void SetCorrectionScanWindow(int window) {
    correction_scan_window_ = window < 1 ? 1 : window;
  }
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
  int correction_scan_window_ = 2;
  double transpose_credibility_ = -4.605170185988091;
  bool enable_word_initial_deletion_ = false;
  double word_initial_deletion_credibility_ = -4.605170185988091;
  bool wid_normal_prefix_checked_ = false;
  bool wid_input_is_normal_prefix_ = false;
  bool degenerate_first_pass_ = false;
  bool has_degenerate_del_ = false;
  double degenerate_del_cred_ = -4.605170185988091;
  bool has_degenerate_sub_ = false;
  double degenerate_sub_cred_ = -4.605170185988091;
  bool has_degenerate_ins_ = false;
  double degenerate_ins_cred_ = -4.605170185988091;
  bool dfp_normal_prefix_checked_ = false;
  bool dfp_input_is_normal_prefix_ = false;
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
