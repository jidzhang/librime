//
// Copyright RIME Developers
// Distributed under the BSD License
//
// 2011-07-12 Zou Xu <zouivex@gmail.com>
// 2012-02-11 GONG Chen <chen.sst@gmail.com>
//
#include <algorithm>
#include <queue>
#include <string_view>
#include <boost/range/adaptor/reversed.hpp>
#include <rime/algo/syllabifier.h>
#include <rime/dict/corrector.h>
#include <rime/dict/prism.h>
#include "syllabifier.h"

namespace rime {
using namespace corrector;

using Vertex = pair<size_t, SpellingType>;
using VertexQueue =
    std::priority_queue<Vertex, vector<Vertex>, std::greater<Vertex>>;

// 權重階梯：
// 1. 全拼：用戶完整輸入了所有編碼。Penalty = 0
// 2. 簡拼：用戶輸入了縮寫，明確希望匹配某個字。Penalty ≈ -2.3
// 3. 補全 (Completion)：用戶還沒打完，算法瞎猜的。Penalty ≈ -3.0
const double kCompletionPenalty = -2.995732273553991;      // log(0.05)
const double kCorrectionCredibility = -4.605170185988091;  // log(0.01)

// QWERTY 物理邻键表（d7 口径，2026-10-29）：上/中/下三行按 0.5 键宽错位，
// 欧氏距离 <=1.2 键宽为邻。'a' -> q/s/w/z，与评测注入器 probe-neighbor.go
// 同一几何，保证"注入什么就修什么"。librime 自带 corrector.cc 的
// keyboard_map 只有同行左右邻（'a'->'s'），不覆盖上下行，故此处独立成表。
static const char* const kQwertyNeighbors[26] = {
    /* a */ "qswz",          /* b */ "ghnv",         /* c */ "dfvx",
    /* d */ "cefrsx",        /* e */ "drsw",         /* f */ "cdgrtv",
    /* g */ "bfhtvy",        /* h */ "bgjnuy",       /* i */ "jkou",
    /* j */ "hikmnu",        /* k */ "ijlmo",        /* l */ "kop",
    /* m */ "jkn",           /* n */ "bhjm",         /* o */ "iklp",
    /* p */ "lo",            /* q */ "aw",           /* r */ "deft",
    /* s */ "adewxz",        /* t */ "fgry",         /* u */ "hijy",
    /* v */ "bcfg",          /* w */ "aeqs",         /* x */ "cdsz",
    /* y */ "ghtu",          /* z */ "asx",
};

int Syllabifier::BuildSyllableGraph(const string& input,
                                    Prism& prism,
                                    SyllableGraph* graph) {
  if (input.empty())
    return 0;

  size_t farthest = 0;
  VertexQueue queue;
  queue.push(Vertex{0, kNormalSpelling});  // start

  while (!queue.empty()) {
    Vertex vertex(queue.top());
    queue.pop();
    size_t current_pos = vertex.first;

    // record a visit to the vertex
    if (graph->vertices.find(current_pos) == graph->vertices.end()) {
      graph->vertices.insert(vertex);  // preferred spelling type comes first
    } else {
      //      graph->vertices[current_pos] =
      //          std::min(vertex.second, graph->vertices[current_pos]);
      continue;  // discard worse spelling types
    }

    if (current_pos > farthest)
      farthest = current_pos;

    // consume leading delimiters
    size_t begin_pos = current_pos;
    while (begin_pos < input.length() &&
           delimiters_.find(input[begin_pos]) != string::npos)
      ++begin_pos;
    DLOG(INFO) << "current_pos: " << current_pos
               << ", begin_pos: " << begin_pos;

    // see where we can go by advancing a syllable
    vector<Prism::Match> matches;
    set<SyllableId> exact_match_syllables;
    std::string_view current_input(input.data() + begin_pos,
                                   input.length() - begin_pos);
    if (canonicalizer_) {
      const size_t limit =
          (std::min)(current_input.length(), prism.max_key_length());
      string syllable;
      syllable.reserve(limit);
      for (size_t len = 1; len <= limit; ++len) {
        // TODO: 須重構 Calculus 爲寫入時複製以避免複製子串.
        syllable.assign(current_input.data(), len);
        canonicalizer_->Apply(&syllable);
        // 重排打破了前綴單調性, 故不能用前綴下行搜索, 改爲每次步進精確查詢.
        int value = -1;
        if (prism.GetValue(syllable, &value)) {
          matches.emplace_back(Prism::Match{value, len});
        }
      }
    } else {
      prism.CommonPrefixSearch(current_input, &matches);
    }
    bool correction_enabled = corrector_ || enable_transpose_ ||
                              enable_deletion_ || enable_substitution_ ||
                              enable_insertion_;
    if (correction_enabled) {
      for (auto& m : matches) {
        exact_match_syllables.insert(m.value);
      }
    }
    // 退化顶点判定（typo_gate_degenerate）：该位置开不出任何全拼拼写
    //（kNormalSpelling，含 a/o/e/m/n 单字母音节）→ 全拼结构已断，
    // 是"用户打错了"的结构信号。d7 实测：正确输入 22/23 的删除边损伤
    // 路径落在非退化顶点。门控只作用于 del/sub/ins 三边，换位边不受控。
    bool gated_edges_allowed = true;
    if (typo_gate_degenerate_ && typo_edges_enabled_) {
      gated_edges_allowed = false;
      for (const auto& m : matches) {
        if (m.length == 0)
          continue;
        for (auto accessor = prism.QuerySpelling(m.value);
             !accessor.exhausted(); accessor.Next()) {
          if (accessor.properties().type == kNormalSpelling) {
            gated_edges_allowed = true;
            break;
          }
        }
        if (gated_edges_allowed)
          break;
      }
    }
    if (corrector_) {
      Corrections corrections;
      corrector_->ToleranceSearch(prism, string{current_input}, &corrections,
                                  5);
      for (const auto& m : corrections) {
        for (auto accessor = prism.QuerySpelling(m.first);
             !accessor.exhausted(); accessor.Next()) {
          auto props = accessor.properties();
          if (props.type == kNormalSpelling && !props.is_correction) {
            matches.push_back({m.first, m.second.length});
            break;
          }
        }
      }
    }
    // 换位容错（enable_transpose）：对剩余输入的相邻字符互换变体做前缀搜索。
    // 位置 i 的换位变体只接受长度 > i+1 的匹配（须同时覆盖两个被换字符，
    // 否则与正常搜索重复）。命中走纠错边（is_correction + credibility 惩罚）；
    // 若正常拼写已覆盖该音节，下方 spellings 合并时 correction 自然被忽略。
    set<int> transposed_match_values;
    set<int> deleted_match_values;
    set<int> substituted_match_values;
    set<int> inserted_match_values;
    if (enable_transpose_ && typo_edges_enabled_) {
      // 只试前两个换位点：简拼边使每个位置都是图顶点，任意位置的换位都
      // 能从其起始顶点以 i=0（或前一顶点 i=1）覆盖；全长度枚举实测使每键
      // 延迟 +40%（O(n^3) trie 步），限两位后与正常搜索同量级。
      for (size_t i = 0; i + 1 < current_input.size() && i < 2; ++i) {
        if (current_input[i] == current_input[i + 1] ||
            delimiters_.find(current_input[i]) != string::npos ||
            delimiters_.find(current_input[i + 1]) != string::npos) {
          continue;
        }
        string key(current_input);
        std::swap(key[i], key[i + 1]);
        vector<Prism::Match> tmatches;
        prism.CommonPrefixSearch(key, &tmatches);
        // 每个换位只取最长匹配：多前缀匹配会按候选数平方放大翻译开销
        //（实测每键 +40% 延迟），最长拼写通常即用户意图音节。
        const Prism::Match* best = nullptr;
        for (const auto& m : tmatches) {
          if (m.length == 0 || m.length <= i + 1)
            continue;
          if (!best || m.length > best->length)
            best = &m;
        }
        if (best) {
          transposed_match_values.insert(best->value);
          matches.push_back(*best);
        }
      }
    }
    // 漏字容错（用户多打了一个键）：剩余输入删去第 i 位（i∈{0,1}，顶点密度
    // 同换位论证）后做前缀搜索。命中的边需多消耗一个原字符：push length+1
    // 使 end_pos 正确。只取最长匹配（同换位的延迟教训）。
    // typo_gate_degenerate 开启时仅退化顶点触发。
    if (enable_deletion_ && typo_edges_enabled_ && gated_edges_allowed) {
      for (size_t i = 0; i < current_input.size() && i < 2; ++i) {
        if (delimiters_.find(current_input[i]) != string::npos)
          continue;
        string key(current_input);
        key.erase(i, 1);
        vector<Prism::Match> tmatches;
        prism.CommonPrefixSearch(key, &tmatches);
        const Prism::Match* best = nullptr;
        for (const auto& m : tmatches) {
          // 长度必须 >=2 且越过删除点：单字符匹配（o/w 等单字母音节）
          // 会在每个顶点产出垃圾纠错边，候选路径爆炸（实测每键 +89%）。
          if (m.length < 2 || m.length <= i)
            continue;
          if (!best || m.length > best->length)
            best = &m;
        }
        if (best) {
          deleted_match_values.insert(best->value);
          Prism::Match m = *best;
          m.length += 1;  // 边跨越被删字符
          matches.push_back(m);
        }
      }
    }
    // 邻键替换容错（用户打错了旁边的键）：第 i 位（i∈{0,1}）换成物理邻键
    // 后做前缀搜索，取最长匹配（须覆盖被替换位：length >= i+1）。
    // 邻键展开每位置 2~6 个变体，只允许在退化顶点触发（门控），否则
    // 正确输入的每个音节都会派生替换边，候选爆炸。
    if (enable_substitution_ && typo_edges_enabled_ && gated_edges_allowed) {
      for (size_t i = 0; i < current_input.size() && i < 2; ++i) {
        if (current_input[i] < 'a' || current_input[i] > 'z' ||
            delimiters_.find(current_input[i]) != string::npos)
          continue;  // alphabet 含大写；邻键表按小写索引，越界即踩内存
        const char* nbrs = kQwertyNeighbors[current_input[i] - 'a'];
        for (const char* p = nbrs; *p; ++p) {
          string key(current_input);
          key[i] = *p;
          vector<Prism::Match> tmatches;
          prism.CommonPrefixSearch(key, &tmatches);
          const Prism::Match* best = nullptr;
          for (const auto& m : tmatches) {
            if (m.length < 2 || m.length < i + 1)
              continue;
            if (!best || m.length > best->length)
              best = &m;
          }
          if (best) {
            substituted_match_values.insert(best->value);
            matches.push_back(*best);
          }
        }
      }
    }
    // 漏打容错（用户少打了一个字母）：第 i 位（i∈{0,1}）插入任一字母后做
    // 前缀搜索，取最长匹配（须覆盖插入点：length >= i+2）。插入字符是
    // 虚拟的，push length-1 使边少消耗一个原字符。仅退化顶点触发。
    if (enable_insertion_ && typo_edges_enabled_ && gated_edges_allowed) {
      for (size_t i = 0; i < current_input.size() && i < 2; ++i) {
        if (delimiters_.find(current_input[i]) != string::npos)
          continue;
        for (char c = 'a'; c <= 'z'; ++c) {
          string key(current_input);
          key.insert(i, 1, c);
          vector<Prism::Match> tmatches;
          prism.CommonPrefixSearch(key, &tmatches);
          const Prism::Match* best = nullptr;
          for (const auto& m : tmatches) {
            if (m.length < 3 || m.length < i + 2)
              continue;
            if (!best || m.length > best->length)
              best = &m;
          }
          if (best) {
            inserted_match_values.insert(best->value);
            Prism::Match m = *best;
            m.length -= 1;  // 插入字符不消耗原输入
            if (m.length == 0)
              continue;
            matches.push_back(m);
          }
        }
      }
    }

    size_t leading_gap = begin_pos - current_pos;
    if (!matches.empty()) {
      auto& end_vertices(graph->edges[current_pos]);
      for (const auto& m : matches) {
        if (m.length == 0)
          continue;
        size_t end_pos = current_pos + leading_gap + m.length;
        // consume trailing delimiters
        while (end_pos < input.length() &&
               delimiters_.find(input[end_pos]) != string::npos)
          ++end_pos;
        DLOG(INFO) << "end_pos: " << end_pos;
        bool matches_input = (current_pos == 0 && end_pos == input.length());
        SpellingMap& spellings(end_vertices[end_pos]);
        SpellingType end_vertex_type = kInvalidSpelling;
        // when spelling algebra is enabled,
        // a spelling evaluates to a set of syllables;
        // otherwise, it resembles exactly the syllable itself.
        SpellingAccessor accessor(prism.QuerySpelling(m.value));
        while (!accessor.exhausted()) {
          SyllableId syllable_id = accessor.syllable_id();
          EdgeProperties props(accessor.properties());
          if (strict_spelling_ && matches_input &&
              props.type != kNormalSpelling) {
            // disqualify fuzzy spelling or abbreviation as single word
          } else {
            props.end_pos = end_pos;
            // add a syllable with properties to the edge's
            // spelling-to-syllable map
            if (correction_enabled &&
                exact_match_syllables.find(m.value) ==
                    exact_match_syllables.end()) {
              props.is_correction = true;
              props.credibility =
                  transposed_match_values.count(m.value)
                      ? transpose_credibility_
                  : deleted_match_values.count(m.value)
                      ? deletion_credibility_
                  : substituted_match_values.count(m.value)
                      ? substitution_credibility_
                  : inserted_match_values.count(m.value)
                      ? insertion_credibility_
                      : kCorrectionCredibility;
            }
            auto it = spellings.find(syllable_id);
            if (it == spellings.end()) {
              spellings.insert({syllable_id, props});
            } else {
              it->second.type = (std::min)(it->second.type, props.type);
            }
            // let end_vertex_type be the best (smaller) type of spelling
            // that ends at the vertex
            if (end_vertex_type > props.type && !props.is_correction) {
              end_vertex_type = props.type;
            }
          }
          accessor.Next();
        }
        if (spellings.empty()) {
          DLOG(INFO) << "not spelled.";
          end_vertices.erase(end_pos);
          continue;
        }
        // find the best common type in a path up to the end vertex
        // eg. pinyin "shurfa" has vertex type kNormalSpelling at position 3,
        // kAbbreviation at position 4 and kAbbreviation at position 6
        if (end_vertex_type < vertex.second) {
          end_vertex_type = vertex.second;
        }
        queue.push(Vertex{end_pos, end_vertex_type});
        DLOG(INFO) << "added to syllable graph, edge: [" << current_pos << ", "
                   << end_pos << ")";
      }
    }
  }

  DLOG(INFO) << "remove stale vertices and edges";
  set<int> good;
  good.insert(farthest);
  // fuzzy spellings are immune to invalidation by normal spellings
  SpellingType last_type =
      (std::max)(graph->vertices[farthest], kFuzzySpelling);
  for (int i = farthest - 1; i >= 0; --i) {
    if (graph->vertices.find(i) == graph->vertices.end())
      continue;
    // remove stale edges
    for (auto j = graph->edges[i].begin(); j != graph->edges[i].end();) {
      if (good.find(j->first) == good.end()) {
        // not connected
        graph->edges[i].erase(j++);
        continue;
      }
      // remove disqualified syllables (eg. matching abbreviated spellings)
      // when there is a path of more favored type
      SpellingType edge_type = kInvalidSpelling;
      for (auto k = j->second.begin(); k != j->second.end();) {
        if (k->second.is_correction) {
          ++k;
          continue;  // Don't care correction edges
        }
        if (k->second.type > last_type) {
          j->second.erase(k++);
        } else {
          if (k->second.type < edge_type)
            edge_type = k->second.type;
          ++k;
        }
      }
      if (j->second.empty()) {
        graph->edges[i].erase(j++);
      } else {
        if (edge_type < kAbbreviation)
          CheckOverlappedSpellings(graph, i, j->first);
        ++j;
      }
    }
    if (graph->vertices[i] > last_type || graph->edges[i].empty()) {
      DLOG(INFO) << "remove stale vertex at " << i;
      graph->vertices.erase(i);
      graph->edges.erase(i);
      continue;
    }
    // keep the valid vertex
    good.insert(i);
  }

  if (enable_completion_ && farthest < input.length()) {
    DLOG(INFO) << "completion enabled";
    const size_t kExpandSearchLimit = 512;
    vector<Prism::Match> keys;
    prism.ExpandSearch(input.substr(farthest), &keys, kExpandSearchLimit);
    if (!keys.empty()) {
      size_t current_pos = farthest;
      size_t end_pos = input.length();
      size_t code_length = end_pos - current_pos;
      auto& end_vertices(graph->edges[current_pos]);
      auto& spellings(end_vertices[end_pos]);
      for (const auto& m : keys) {
        if (m.length < code_length)
          continue;
        // when spelling algebra is enabled,
        // a spelling evaluates to a set of syllables;
        // otherwise, it resembles exactly the syllable itself.
        SpellingAccessor accessor(prism.QuerySpelling(m.value));
        while (!accessor.exhausted()) {
          SyllableId syllable_id = accessor.syllable_id();
          SpellingProperties props = accessor.properties();
          if (props.type < kAbbreviation) {
            props.type = kCompletion;
            props.credibility += kCompletionPenalty;
            props.end_pos = end_pos;
            // add a syllable with properties to the edge's
            // spelling-to-syllable map
            spellings.insert({syllable_id, props});
          }
          accessor.Next();
        }
      }
      if (spellings.empty()) {
        DLOG(INFO) << "no completion could be made.";
        end_vertices.erase(end_pos);
      } else {
        DLOG(INFO) << "added to syllable graph, completion: [" << current_pos
                   << ", " << end_pos << ")";
        farthest = end_pos;
      }
    }
  }

  graph->input_length = input.length();
  graph->interpreted_length = farthest;
  DLOG(INFO) << "input length: " << graph->input_length;
  DLOG(INFO) << "syllabified length: " << graph->interpreted_length;

  Transpose(graph);

  return farthest;
}

void Syllabifier::CheckOverlappedSpellings(SyllableGraph* graph,
                                           size_t start,
                                           size_t end) {
  if (!graph || graph->edges.find(start) == graph->edges.end())
    return;
  // if "Z" = "YX", mark the vertex between Y and X an ambiguous syllable joint
  auto& y_end_vertices(graph->edges[start]);
  // enumerate Ys
  for (const auto& y : y_end_vertices) {
    size_t joint = y.first;
    if (joint >= end)
      break;
    // test X
    if (graph->edges.find(joint) == graph->edges.end())
      continue;
    auto& x_end_vertices(graph->edges[joint]);
    for (auto& x : x_end_vertices) {
      if (x.first < end)
        continue;
      if (x.first == end) {
        // discourage syllables at an ambiguous joint
        // bad cases include pinyin syllabification "niju'ede"
        for (auto& spelling : x.second) {
          // 這條邊（X）相對於起點構成歧義
          spelling.second.ambiguous_source_positions.insert(start);
        }
        graph->vertices[joint] = kAmbiguousSpelling;
        DLOG(INFO) << "ambiguous syllable joint at position " << joint << ".";
      }
      break;
    }
  }
}

void Syllabifier::Transpose(SyllableGraph* graph) {
  for (const auto& start : graph->edges) {
    auto& index(graph->indices[start.first]);
    for (const auto& end : boost::adaptors::reverse(start.second)) {
      for (const auto& spelling : end.second) {
        SyllableId syll_id = spelling.first;
        index[syll_id].push_back(&spelling.second);
      }
    }
  }
}

void Syllabifier::EnableCorrection(Corrector* corrector) {
  corrector_ = corrector;
}

}  // namespace rime
