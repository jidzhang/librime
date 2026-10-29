//
// Copyright RIME Developers
// Distributed under the BSD License
//
// 2011-08-07 GONG Chen <chen.sst@gmail.com>
//
#ifndef RIME_SCRIPT_TRANSLATOR_H_
#define RIME_SCRIPT_TRANSLATOR_H_

#include <rime/common.h>
#include <rime/translation.h>
#include <rime/translator.h>
#include <rime/algo/algebra.h>
#include <rime/gear/memory.h>
#include <rime/gear/translator_commons.h>

namespace rime {

class Code;
class Corrector;
struct DictEntry;
class Dictionary;
class Poet;
class UserDictionary;
struct SyllableGraph;

class ScriptTranslator : public Translator,
                         public Memory,
                         public TranslatorOptions {
 public:
  ScriptTranslator(const Ticket& ticket);

  virtual an<Translation> Query(const string& input,
                                const Segment& segment) override;
  virtual bool Memorize(const CommitEntry& commit_entry) override;
  virtual bool ProcessSegmentOnCommit(CommitEntry& commit_entry,
                                      const Segment& seg) override;

  string FormatPreedit(const string& preedit);
  string Spell(const Code& code);
  string GetPrecedingText(size_t start) const;
  bool UpdateElements(const CommitEntry& commit_entry);

  bool ConcatenatePhrases(CommitEntry& commit_entry,
                          const vector<an<Phrase>>& phrases);
  bool SaveCommitEntry(CommitEntry& commit_entry);

  // options
  int max_homophones() const { return max_homophones_; }
  int spelling_hints() const { return spelling_hints_; }
  bool always_show_comments() const { return always_show_comments_; }
  // 简拼/混合输入时，即使存在整段精确匹配词也构建组句候选
  //（translator/abbrev_sentences，需 grammar 支持，默认关）。
  bool abbrev_sentences() const { return abbrev_sentences_; }
  // 混合输入下句子候选的质量加成（translator/sentence_quality_boost，默认 0）。
  // 句子候选此前从未设置 quality（默认 0），跨翻译器合并时系统性吃亏
  //（如 英文词 der 压过 的人）。仅在 abbrev_sentences 门控触发时应用，
  // 不影响全拼组句的既有平衡。
  double sentence_quality_boost() const { return sentence_quality_boost_; }
  bool enable_word_completion() const { return enable_word_completion_; }
  // 换位容错（translator/enable_transpose，默认 false）：音节图加相邻字符
  // 互换的纠错边（credibility 惩罚），使 diam/diama 类换位错字仍可达原词。
  bool enable_transpose() const { return enable_transpose_; }
  // 换位纠错边的 credibility 惩罚（translator/transpose_credibility，
  // 默认 log(0.01)≈-4.6；更负 = 换位候选更难胜过正常解析，用于守卫调参）。
  double transpose_credibility() const { return transpose_credibility_; }
  // 漏字容错（translator/enable_deletion + deletion_credibility，默认关）。
  bool enable_deletion() const { return enable_deletion_; }
  double deletion_credibility() const { return deletion_credibility_; }
  // 邻键替换容错（translator/enable_substitution + substitution_credibility，
  // 默认关）：d7 打错键场景，QWERTY 物理邻键替换的纠错边。
  bool enable_substitution() const { return enable_substitution_; }
  double substitution_credibility() const { return substitution_credibility_; }
  // 漏打容错（translator/enable_insertion + insertion_credibility，默认关）：
  // d7 少打字母场景，虚拟插入一个字母的纠错边。
  bool enable_insertion() const { return enable_insertion_; }
  double insertion_credibility() const { return insertion_credibility_; }
  // 退化顶点门控（translator/typo_gate_degenerate，默认 false）：del/sub/ins
  // 三边仅在开不出任何全拼拼写的位置触发。
  bool typo_gate_degenerate() const { return typo_gate_degenerate_; }
  // 纠错门控（translator/typo_gated，默认 false）：首遍无整段精确词才带纠错边重查。
  bool typo_gated() const { return typo_gated_; }
  int max_word_length() const { return max_word_length_; }
  int core_word_length() const;

 protected:
  bool abbrev_sentences_ = false;
  double sentence_quality_boost_ = 0.;
  bool enable_transpose_ = false;
  double transpose_credibility_ = -4.605170185988091;
  bool enable_deletion_ = false;
  double deletion_credibility_ = -4.605170185988091;
  bool enable_substitution_ = false;
  double substitution_credibility_ = -4.605170185988091;
  bool enable_insertion_ = false;
  double insertion_credibility_ = -4.605170185988091;
  bool typo_gate_degenerate_ = false;
  bool typo_gated_ = false;
  int max_homophones_ = 1;
  int spelling_hints_ = 0;
  int max_word_length_ = 0;
  int core_word_length_ = 0;
  bool always_show_comments_ = false;
  bool enable_correction_ = false;
  bool enable_word_completion_ = false;
  the<Corrector> corrector_;
  the<Poet> poet_;
  vector<an<Phrase>> queue_;
};

}  // namespace rime

#endif  // RIME_SCRIPT_TRANSLATOR_H_
