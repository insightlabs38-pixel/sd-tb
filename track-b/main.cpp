#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <queue>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

using namespace std;

struct MappedFile {
    int fd = -1;
    size_t size = 0;
    const char *data = nullptr;
    explicit MappedFile(const string &path) {
        fd = open(path.c_str(), O_RDONLY);
        if (fd < 0) throw runtime_error("cannot open " + path);
        struct stat st{};
        if (fstat(fd, &st)) throw runtime_error("fstat failed");
        size = static_cast<size_t>(st.st_size);
        if (size) {
            void *p = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
            if (p == MAP_FAILED) throw runtime_error("mmap failed");
            data = static_cast<const char *>(p);
        }
    }
    ~MappedFile() { if (data) munmap(const_cast<char *>(data), size); if (fd >= 0) close(fd); }
    MappedFile(const MappedFile &) = delete;
    MappedFile &operator=(const MappedFile &) = delete;
};

static inline unsigned char lower_ascii(unsigned char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<unsigned char>(c + 32) : c;
}
static inline bool token_char(unsigned char c) {
    c = lower_ascii(c);
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
}
struct FoldHash {
    using is_transparent = void;
    size_t operator()(string_view s) const noexcept {
        uint64_t h = 14695981039346656037ULL;
        for (unsigned char c : s) { h ^= lower_ascii(c); h *= 1099511628211ULL; }
        return static_cast<size_t>(h);
    }
};
struct FoldEqual {
    using is_transparent = void;
    bool operator()(string_view a, string_view b) const noexcept {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i)
            if (lower_ascii(static_cast<unsigned char>(a[i])) != lower_ascii(static_cast<unsigned char>(b[i]))) return false;
        return true;
    }
};

struct Query {
    bool phrase = false;
    vector<uint32_t> terms;
    vector<char> ops;
};
struct Result {
    uint32_t count = 0;
    uint64_t sum = 0;
    array<uint32_t, 10> first{};
    void add(uint32_t id) {
        if (count < first.size()) first[count] = id;
        ++count;
        sum += id;
    }
    void print(ostream &os) const {
        os << count << ' ' << sum;
        for (size_t i = 0; i < min<size_t>(count, first.size()); ++i) os << ' ' << first[i];
        os << '\n';
    }
};

class Engine {
    unordered_map<string_view, uint32_t, FoldHash, FoldEqual> dictionary;
    vector<vector<uint32_t>> postings;
    vector<uint32_t> seen;
    vector<uint32_t> tape;
    vector<uint32_t> offsets;
    vector<Query> queries;
    vector<int32_t> precomputed_pattern;
    vector<Result> pattern_results;

    struct ACNode {
        unordered_map<uint32_t, uint32_t> edges;
        uint32_t fail = 0;
        vector<uint32_t> outputs;
    };

    void precompute_common_phrases() {
        const size_t threshold = max<size_t>(1024, (offsets.size() - 1) / 200);
        precomputed_pattern.assign(queries.size(), -1);
        vector<ACNode> nodes(1);
        unordered_map<string, uint32_t> dedup;
        for (size_t qi = 0; qi < queries.size(); ++qi) {
            const Query &q = queries[qi];
            if (!q.phrase) continue;
            size_t least = numeric_limits<size_t>::max();
            for (uint32_t t : q.terms) least = min(least, postings[t].size());
            if (least < threshold) continue;
            string key;
            key.reserve(q.terms.size() * sizeof(uint32_t));
            for (uint32_t t : q.terms)
                key.append(reinterpret_cast<const char *>(&t), sizeof(t));
            auto it = dedup.find(key);
            if (it != dedup.end()) {
                precomputed_pattern[qi] = static_cast<int32_t>(it->second);
                continue;
            }
            uint32_t pid = static_cast<uint32_t>(pattern_results.size());
            dedup.emplace(move(key), pid);
            pattern_results.emplace_back();
            precomputed_pattern[qi] = static_cast<int32_t>(pid);
            uint32_t state = 0;
            for (uint32_t t : q.terms) {
                const uint32_t tok = t + 1;
                auto edge = nodes[state].edges.find(tok);
                if (edge == nodes[state].edges.end()) {
                    uint32_t next = static_cast<uint32_t>(nodes.size());
                    nodes[state].edges.emplace(tok, next);
                    nodes.emplace_back();
                    state = next;
                } else state = edge->second;
            }
            nodes[state].outputs.push_back(pid);
        }
        if (pattern_results.empty()) return;
        queue<uint32_t> bfs;
        for (const auto &[tok, next] : nodes[0].edges) {
            nodes[next].fail = 0;
            bfs.push(next);
        }
        while (!bfs.empty()) {
            uint32_t state = bfs.front(); bfs.pop();
            for (const auto &[tok, next] : nodes[state].edges) {
                uint32_t f = nodes[state].fail;
                while (f && !nodes[f].edges.contains(tok)) f = nodes[f].fail;
                auto it = nodes[f].edges.find(tok);
                nodes[next].fail = it == nodes[f].edges.end() ? 0 : it->second;
                const auto &suffix = nodes[nodes[next].fail].outputs;
                nodes[next].outputs.insert(nodes[next].outputs.end(), suffix.begin(), suffix.end());
                bfs.push(next);
            }
        }
        vector<uint32_t> last(pattern_results.size(), numeric_limits<uint32_t>::max());
        for (uint32_t doc = 0; doc + 1 < offsets.size(); ++doc) {
            uint32_t state = 0;
            for (uint32_t i = offsets[doc]; i < offsets[doc + 1]; ++i) {
                uint32_t tok = tape[i];
                while (state && !nodes[state].edges.contains(tok)) state = nodes[state].fail;
                auto edge = nodes[state].edges.find(tok);
                state = edge == nodes[state].edges.end() ? 0 : edge->second;
                for (uint32_t pid : nodes[state].outputs) {
                    if (last[pid] != doc) {
                        last[pid] = doc;
                        pattern_results[pid].add(doc);
                    }
                }
            }
        }
    }


    uint32_t intern(string_view word) {
        auto it = dictionary.find(word);
        if (it != dictionary.end()) return it->second;
        uint32_t id = static_cast<uint32_t>(postings.size());
        dictionary.emplace(word, id);
        postings.emplace_back();
        seen.push_back(numeric_limits<uint32_t>::max());
        return id;
    }

    void parse_queries(const MappedFile &input) {
        const char *p = input.data, *end = p + input.size;
        dictionary.reserve(160000);
        while (p < end) {
            const char *line_end = static_cast<const char *>(memchr(p, '\n', end - p));
            if (!line_end) line_end = end;
            string_view line(p, line_end - p);
            Query q;
            if (line.starts_with("BOOL ")) {
                size_t start = 5;
                bool term = true;
                while (start < line.size()) {
                    size_t stop = line.find(' ', start);
                    if (stop == string_view::npos) stop = line.size();
                    string_view word = line.substr(start, stop - start);
                    if (term) q.terms.push_back(intern(word));
                    else q.ops.push_back(word[0]); // A, O, N are unique
                    term = !term;
                    start = stop + 1;
                }
            } else if (line.starts_with("PHRASE \"") && line.size() >= 9 && line.back() == '"') {
                q.phrase = true;
                size_t start = 8, last = line.size() - 1;
                while (start < last) {
                    size_t stop = line.find(' ', start);
                    if (stop == string_view::npos || stop > last) stop = last;
                    q.terms.push_back(intern(line.substr(start, stop - start)));
                    start = stop + 1;
                }
            } else throw runtime_error("invalid query line");
            if (q.terms.empty() || (!q.phrase && q.ops.size() + 1 != q.terms.size()))
                throw runtime_error("invalid query grammar");
            queries.push_back(move(q));
            p = line_end < end ? line_end + 1 : end;
        }
    }

    void build_index(const MappedFile &input) {
        const char *p = input.data, *end = p + input.size;
        tape.reserve(input.size / 7);
        offsets.reserve(input.size / 500);
        uint32_t doc = 0;
        while (p < end) {
            offsets.push_back(static_cast<uint32_t>(tape.size()));
            const char *line_end = static_cast<const char *>(memchr(p, '\n', end - p));
            if (!line_end) line_end = end;
            const char *tab = static_cast<const char *>(memchr(p, '\t', line_end - p));
            if (!tab) throw runtime_error("missing document tab");
            uint32_t parsed = 0;
            auto [ptr, ec] = from_chars(p, tab, parsed);
            if (ec != errc() || ptr != tab || parsed != doc) throw runtime_error("invalid document id");
            const char *s = tab + 1;
            while (s < line_end) {
                while (s < line_end && !token_char(static_cast<unsigned char>(*s))) ++s;
                if (s == line_end) break;
                const char *start = s;
                do { ++s; } while (s < line_end && token_char(static_cast<unsigned char>(*s)));
                auto it = dictionary.find(string_view(start, s - start));
                uint32_t id = it == dictionary.end() ? 0 : it->second + 1;
                tape.push_back(id);
                if (id && seen[id - 1] != doc) {
                    seen[id - 1] = doc;
                    postings[id - 1].push_back(doc);
                }
            }
            ++doc;
            p = line_end < end ? line_end + 1 : end;
        }
        offsets.push_back(static_cast<uint32_t>(tape.size()));
    }

    bool contains_phrase(uint32_t doc, const vector<uint32_t> &terms) const {
        const uint32_t begin = offsets[doc], end = offsets[doc + 1];
        const size_t k = terms.size();
        if (end - begin < k) return false;
        for (uint32_t i = begin; i + k <= end; ++i) {
            if (tape[i] != terms[0] + 1) continue;
            size_t j = 1;
            while (j < k && tape[i + j] == terms[j] + 1) ++j;
            if (j == k) return true;
        }
        return false;
    }

    Result run_phrase(const Query &q) const {
        size_t best = 0;
        for (size_t i = 1; i < q.terms.size(); ++i)
            if (postings[q.terms[i]].size() < postings[q.terms[best]].size()) best = i;
        Result r;
        for (uint32_t doc : postings[q.terms[best]])
            if (contains_phrase(doc, q.terms)) r.add(doc);
        return r;
    }

    Result run_bool(const Query &q, vector<uint32_t> &work, vector<uint32_t> &tmp) const {
        const vector<uint32_t> &initial = postings[q.terms[0]];
        if (q.ops.empty()) {
            Result r;
            for (uint32_t id : initial) r.add(id);
            return r;
        }
        work.assign(initial.begin(), initial.end());
        for (size_t step = 0; step < q.ops.size(); ++step) {
            const vector<uint32_t> &rhs = postings[q.terms[step + 1]];
            char op = q.ops[step];
            tmp.clear();
            if (op == 'A') {
                size_t i = 0, j = 0;
                while (i < work.size() && j < rhs.size()) {
                    if (work[i] < rhs[j]) ++i;
                    else if (rhs[j] < work[i]) ++j;
                    else { tmp.push_back(work[i]); ++i; ++j; }
                }
            } else if (op == 'O') {
                size_t i = 0, j = 0;
                while (i < work.size() && j < rhs.size()) {
                    if (work[i] < rhs[j]) tmp.push_back(work[i++]);
                    else if (rhs[j] < work[i]) tmp.push_back(rhs[j++]);
                    else { tmp.push_back(work[i]); ++i; ++j; }
                }
                tmp.insert(tmp.end(), work.begin() + i, work.end());
                tmp.insert(tmp.end(), rhs.begin() + j, rhs.end());
            } else if (op == 'N') {
                size_t i = 0, j = 0;
                while (i < work.size() && j < rhs.size()) {
                    if (work[i] < rhs[j]) tmp.push_back(work[i++]);
                    else if (rhs[j] < work[i]) ++j;
                    else { ++i; ++j; }
                }
                tmp.insert(tmp.end(), work.begin() + i, work.end());
            } else throw runtime_error("invalid boolean operator");
            work.swap(tmp);
        }
        Result r;
        for (uint32_t id : work) r.add(id);
        return r;
    }

public:
    void run(const string &data_dir, const string &out_dir) {
        MappedFile query_file(data_dir + "/queries.txt");
        parse_queries(query_file);
        MappedFile docs_file(data_dir + "/docs.txt");
        build_index(docs_file);
        precompute_common_phrases();
        filesystem::create_directories(out_dir);
        ofstream output(out_dir + "/results.txt", ios::binary);
        if (!output) throw runtime_error("cannot open output");
        vector<uint32_t> work, tmp;
        for (size_t qi = 0; qi < queries.size(); ++qi) {
            const Query &q = queries[qi];
            Result r = precomputed_pattern[qi] >= 0
                ? pattern_results[precomputed_pattern[qi]]
                : (q.phrase ? run_phrase(q) : run_bool(q, work, tmp));
            r.print(output);
        }
        if (!output) throw runtime_error("output write failure");
    }
};

int main(int argc, char **argv) {
    string data_dir = "/data", out_dir = "/out";
    for (int i = 1; i + 1 < argc; i += 2) {
        if (strcmp(argv[i], "--data") == 0) data_dir = argv[i + 1];
        else if (strcmp(argv[i], "--out") == 0) out_dir = argv[i + 1];
        else { cerr << "unknown argument: " << argv[i] << '\n'; return 2; }
    }
    try { Engine().run(data_dir, out_dir); }
    catch (const exception &e) { cerr << e.what() << '\n'; return 1; }
    return 0;
}
