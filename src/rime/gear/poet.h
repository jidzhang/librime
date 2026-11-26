//
// Copyright RIME Developers
// Distributed under the BSD License
//
// simplistic sentence-making
//
// 2011-10-06 GONG Chen <chen.sst@gmail.com>
//

#ifndef RIME_POET_H_
#define RIME_POET_H_

#include <rime/common.h>
#include <rime/translation.h>
#include <rime/gear/translator_commons.h>
#include <rime/gear/contextual_translation.h>

namespace rime {

using WordGraph = map<int, map<int, DictEntryList>>;

class Grammar;
class Language;
struct Line;

// 前缀惯性（2026-11-25）：逐键整句重算时，给"与上一帧显示文字保持一致"的
// 路径按字加分。引擎要改掉已经显示给用户的字，必须赢得比这份惯性分更多；
// 分数优势足够大时照常改写（自我纠错不受影响）。惯性分按"该字已连续显示
// 的帧数"分档，由配置决定哪一档设防多强——刚显示的字被改写多半是纠正，
// 是否设防、设多强是产品调参问题，不在这里定。
struct StablePrefix {
  string text;             // 上一帧首位候选的文字；空=关闭
  vector<size_t> offsets;  // text 的字边界字节偏移，长度=字数+1
  vector<double> bias;     // 每个字的惯性分
  vector<double> remain;   // remain[j] = 第 j 个字及之后的惯性分之和

  bool active() const { return remain.size() > 1 && remain.front() > 0.; }
  size_t runes() const { return offsets.empty() ? 0 : offsets.size() - 1; }
};

class Poet {
 public:
  // Line "less", used to compare composed line of the same input range.
  using Compare = function<bool(const Line&, const Line&)>;

  // 已偏离上一帧显示：此后不再计惯性分。
  static constexpr size_t kStableDiverged = static_cast<size_t>(-1);

  // 设定本次组句的显示文字。只对紧随其后的 MakeSentence* 生效，用完即清。
  // ages[j] = 第 j 个字已连续显示的帧数（不足按 1 计）；
  // bias_by_age[age-1] = 该档惯性分（超出取末档，空表=关闭）。
  void set_stable_prefix(const string& text,
                         const vector<size_t>& ages,
                         const vector<double>& bias_by_age);
  void clear_stable_prefix() {
    set_stable_prefix(string(), vector<size_t>(), vector<double>());
  }
  // 惯性接续：已对上 agree 个字的路径再接一个 word。
  // 返回 {本次加分, 接续后对上的字数}。
  pair<double, size_t> ExtendStablePrefix(size_t agree,
                                          const string& word) const;

  static bool CompareWeight(const Line& one, const Line& other);
  static bool LeftAssociateCompare(const Line& one, const Line& other);

  Poet(const Language* language,
       Config* config,
       Compare compare = CompareWeight);
  ~Poet();

  an<Sentence> MakeSentence(const WordGraph& graph,
                            size_t total_length,
                            const string& preceding_text);
  deque<an<Sentence>> MakeSentences(const WordGraph& graph,
                                    size_t total_length,
                                    const string& preceding_text,
                                    size_t count,
                                    double cutoff_threshold);

  template <class TranslatorT>
  an<Translation> ContextualWeighted(an<Translation> translation,
                                     const string& input,
                                     size_t start,
                                     TranslatorT* translator) {
    if (!translator->contextual_suggestions() || !grammar_) {
      return translation;
    }
    auto preceding_text = translator->GetPrecedingText(start);
    if (preceding_text.empty()) {
      return translation;
    }
    return New<ContextualTranslation>(translation, input, preceding_text,
                                      grammar_.get());
  }

 private:
  template <class Strategy>
  an<Sentence> MakeSentenceWithStrategy(const WordGraph& graph,
                                        size_t total_length,
                                        const string& preceding_text);

  const Language* language_;
  the<Grammar> grammar_;

 public:
  bool has_grammar() const;

 private:
  Compare compare_;
  StablePrefix stable_;

  // 比较键：已累计分 + 余下显示文字若全对上还能拿到的惯性分（乐观键，
  // 不提前剪掉还有机会翻盘的一致路径）。
  double StableKey(const Line& line) const;
};

}  // namespace rime

#endif  // RIME_POET_H_
