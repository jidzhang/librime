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

// QWERTY 物理邻键表：上/中/下三行按 0.5 键宽错位，欧氏距离 <=1.2 键宽为邻
// （'a' -> q/s/w/z）。corrector.cc 的 keyboard_map 只有同行左右邻，不覆盖
// 上下行，故此处独立成表；评测注入器 probe-neighbor.go 用同一几何。
static const char* const kQwertyNeighbors[26] = {
    /* a */ "qswz",   /* b */ "ghnv",   /* c */ "dfvx",
    /* d */ "cefrsx", /* e */ "drsw",   /* f */ "cdgrtv",
    /* g */ "bfhtvy", /* h */ "bgjnuy", /* i */ "jkou",
    /* j */ "hikmnu", /* k */ "ijlmo",  /* l */ "kop",
    /* m */ "jkn",    /* n */ "bhjm",   /* o */ "iklp",
    /* p */ "lo",     /* q */ "aw",     /* r */ "deft",
    /* s */ "adewxz", /* t */ "fgry",   /* u */ "hijy",
    /* v */ "bcfg",   /* w */ "aeqs",   /* x */ "cdsz",
    /* y */ "ghtu",   /* z */ "asx",
};

namespace {
// 该拼写是否"正常全拼"（非简拼/补全/纠错/模糊）。
bool SpellingIsNormal(Prism& prism, int value) {
  for (auto accessor = prism.QuerySpelling(value); !accessor.exhausted();
       accessor.Next()) {
    if (accessor.properties().type == kNormalSpelling)
      return true;
  }
  return false;
}
}  // namespace

bool Syllabifier::IsNormalPinyinPrefix(const string& input, Prism& prism) {
  if (input.empty())
    return true;
  const size_t n = input.length();
  // reach[i] = 前 i 个字符能被全拼音节完整覆盖（跳过分隔符，与图构建同口径）
  vector<char> reach(n + 1, 0);
  reach[0] = 1;
  size_t farthest = 0;  // 最远的可达覆盖点
  vector<Prism::Match> matches;
  for (size_t i = 0; i < n; ++i) {
    if (!reach[i])
      continue;
    farthest = i;
    while (i + 1 < n && delimiters_.find(input[i + 1]) != string::npos &&
           reach[i + 1]) {
      // 分隔符不消耗语音，直接继承可达性
      reach[i + 1] = 1;
      ++i;
      farthest = i;
    }
    if (delimiters_.find(input[i]) != string::npos)
      continue;
    matches.clear();
    prism.CommonPrefixSearch(std::string_view(input).substr(i), &matches);
    for (const auto& m : matches) {
      // 单字母音节（a/o/e/m/n）不能当"完整音节"用：否则 womn（我们漏打）
      // 会被读成 wo+m+n 而判成"还在打字"。它只允许出现在末尾补全位。
      if (m.length < 2 || i + m.length > n)
        continue;
      if (!SpellingIsNormal(prism, m.value))
        continue;
      reach[i + m.length] = 1;
    }
  }
  if (reach[n])
    return true;  // 整串就是合法全拼
  // 尾巴是否为某个全拼音节的前缀：Darts traverse 的 key_pos 是入出参
  // （调用后推进到 length），必须用独立游标，不能把循环变量传进去。
  auto& trie = prism.trie();
  size_t node = 0;
  for (size_t i = farthest; i < n; ++i) {
    if (delimiters_.find(input[i]) != string::npos)
      continue;
    size_t pos = i;
    if (trie.traverse(input.c_str(), node, pos, i + 1) == -2)
      return false;  // 无路径：不可能是任何音节的前缀
  }
  return true;
}

int Syllabifier::BuildSyllableGraph(const string& input,
                                    Prism& prism,
                                    SyllableGraph* graph) {
  found_degenerate_vertex_ = false;
  found_normal_vertex_ = false;
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
    // 退化顶点跟踪（二遍拼接的信号）：仅在顶点仍有剩余输入时判定——终止
    // 顶点没有匹配是构造使然，不得计入，否则任何完整输入都会被当成"结构
    // 断裂"触发二遍。
    if (track_degenerate_vertex_ && !current_input.empty()) {
      bool vertex_degenerate = true;
      bool vertex_ge2_normal = false;
      for (const auto& m : matches) {
        if (m.length == 0 || !SpellingIsNormal(prism, m.value))
          continue;
        vertex_degenerate = false;
        if (m.length >= 2) {
          vertex_ge2_normal = true;
          break;
        }
      }
      if (vertex_degenerate)
        found_degenerate_vertex_ = true;
      else if (vertex_ge2_normal)
        found_normal_vertex_ = true;
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
    // 纠错边（换位/多打/邻键打错/漏打）：在剩余输入前两位上生成变体、取最长
    // 匹配后按纠正边入图（is_correction + credibility 惩罚）。只试前两位：
    // 简拼边使每个位置都是图顶点，任意位置都能由起始顶点 i∈{0,1} 覆盖；
    // 全长度枚举实测每键 +40% 延迟。correction_values 先到先得，保持换位
    // 惩罚优先级最高。
    std::map<int, double> correction_values;
    vector<Prism::Match> tmatches;
    auto add_correction_edge = [&](const string& key, size_t min_len,
                                   int len_delta, double credibility) {
      tmatches.clear();
      prism.CommonPrefixSearch(key, &tmatches);
      const Prism::Match* best = nullptr;
      for (const auto& m : tmatches) {
        if (m.length < min_len)
          continue;
        if (!best || m.length > best->length)
          best = &m;
      }
      if (!best)
        return;
      Prism::Match m = *best;
      if (len_delta > 0) {
        m.length += len_delta;  // 边多消费被删/被覆盖的原字符
      } else if (len_delta < 0) {
        if (m.length < static_cast<size_t>(-len_delta))
          return;
        m.length -= static_cast<size_t>(-len_delta);  // 插入字符是虚拟的
      }
      if (m.length == 0)
        return;
      correction_values.emplace(m.value, credibility);
      matches.push_back(m);
    };
    if (enable_transpose_) {
      // 换位：相邻两字符互换，匹配须覆盖被换的两字符（>= i+2）。
      for (size_t i = 0; i + 1 < current_input.size() && i < 2; ++i) {
        if (current_input[i] == current_input[i + 1] ||
            delimiters_.find(current_input[i]) != string::npos ||
            delimiters_.find(current_input[i + 1]) != string::npos)
          continue;
        string key(current_input);
        std::swap(key[i], key[i + 1]);
        add_correction_edge(key, i + 2, 0, transpose_credibility_);
      }
    }
    if (enable_deletion_) {
      // 多打一个键：删去第 i 位后搜索，边多消费一个原字符（+1）。
      // 匹配须 >=2 且越过删除点，否则单字母音节会产出垃圾边（实测 +89%）。
      for (size_t i = 0; i < current_input.size() && i < 2; ++i) {
        if (delimiters_.find(current_input[i]) != string::npos)
          continue;
        string key(current_input);
        key.erase(i, 1);
        add_correction_edge(key, (std::max)(size_t(2), i + 1), 1,
                            deletion_credibility_);
      }
    }
    if (enable_substitution_) {
      // 打错键：第 i 位换成物理邻键。邻键表按小写索引，非字母键跳过。
      for (size_t i = 0; i < current_input.size() && i < 2; ++i) {
        if (current_input[i] < 'a' || current_input[i] > 'z' ||
            delimiters_.find(current_input[i]) != string::npos)
          continue;
        const char* nbrs = kQwertyNeighbors[current_input[i] - 'a'];
        for (const char* p = nbrs; *p; ++p) {
          string key(current_input);
          key[i] = *p;
          add_correction_edge(key, (std::max)(size_t(2), i + 1), 0,
                              substitution_credibility_);
        }
      }
    }
    if (enable_insertion_) {
      // 漏打一个字母：第 i 位插入任一字母后搜索，插入字符不消耗原输入（-1）。
      for (size_t i = 0; i < current_input.size() && i < 2; ++i) {
        if (delimiters_.find(current_input[i]) != string::npos)
          continue;
        for (char c = 'a'; c <= 'z'; ++c) {
          string key(current_input);
          key.insert(i, 1, c);
          add_correction_edge(key, i + 1, -1, insertion_credibility_);
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
            if (correction_enabled && exact_match_syllables.find(m.value) ==
                                          exact_match_syllables.end()) {
              props.is_correction = true;
              auto credit = correction_values.find(m.value);
              props.credibility = credit != correction_values.end()
                                      ? credit->second
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
