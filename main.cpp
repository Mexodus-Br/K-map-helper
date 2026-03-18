#include <algorithm>
#include <cctype>
#include <clocale>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

using namespace std;

namespace {

struct Cube {
    uint32_t fixedMask = 0;
    uint32_t valueMask = 0;

    bool operator==(const Cube& other) const {
        return fixedMask == other.fixedMask && valueMask == other.valueMask;
    }
};

struct CubeHash {
    size_t operator()(const Cube& cube) const {
        return (static_cast<uint64_t>(cube.fixedMask) << 32) ^ cube.valueMask;
    }
};

uint64_t cubeKey(const Cube& cube) {
    return (static_cast<uint64_t>(cube.fixedMask) << 32) | cube.valueMask;
}

bool subsumes(const Cube& lhs, const Cube& rhs) {
    if ((lhs.fixedMask & ~rhs.fixedMask) != 0) {
        return false;
    }
    return (((lhs.valueMask ^ rhs.valueMask) & lhs.fixedMask) == 0);
}

string trimSpaces(const string& input) {
    string out;
    out.reserve(input.size());
    for (char ch : input) {
        if (!isspace(static_cast<unsigned char>(ch))) {
            out.push_back(ch);
        }
    }
    return out;
}

void initConsoleEncoding() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
    setlocale(LC_ALL, ".UTF-8");
}

struct HeaderParseResult {
    string functionName;
    vector<char> variables;
    string expression;
    string normalizedInput;
    string error;
};

HeaderParseResult parseHeader(const string& rawInput) {
    HeaderParseResult result;
    const string input = trimSpaces(rawInput);
    result.normalizedInput = input;
    if (input.empty()) {
        result.error = u8"输入为空。";
        return result;
    }

    const size_t leftParen = input.find('(');
    const size_t rightParen = input.find(')');
    const size_t equalPos = input.find('=');
    if (leftParen == string::npos || rightParen == string::npos || equalPos == string::npos ||
        !(leftParen < rightParen && rightParen < equalPos)) {
        result.error = u8"输入格式应类似 Z(a,b,c)=aB+cdE 或 Z(a,b,c)=(a+b)(C+d)。";
        return result;
    }

    result.functionName = input.substr(0, leftParen);
    if (result.functionName.empty()) {
        result.error = u8"函数名不能为空。";
        return result;
    }

    string varPart = input.substr(leftParen + 1, rightParen - leftParen - 1);
    if (varPart.empty()) {
        result.error = u8"变量列表不能为空。";
        return result;
    }

    vector<int> seen(26, 0);
    string token;
    for (size_t i = 0; i <= varPart.size(); ++i) {
        if (i == varPart.size() || varPart[i] == ',') {
            if (token.size() != 1 || token[0] < 'a' || token[0] > 'y') {
                result.error = u8"变量必须是 a-y 之间的不重复小写字母。";
                return result;
            }
            int idx = token[0] - 'a';
            if (seen[idx]) {
                result.error = u8"变量列表中存在重复变量。";
                return result;
            }
            seen[idx] = 1;
            result.variables.push_back(token[0]);
            token.clear();
        } else {
            token.push_back(varPart[i]);
        }
    }

    if (result.variables.size() > 25) {
        result.error = u8"变量个数不能超过 25。";
        return result;
    }

    result.expression = input.substr(equalPos + 1);
    if (result.expression.empty()) {
        result.error = u8"等号右边不能为空。";
        return result;
    }

    return result;
}

struct BDDManager {
    struct Node {
        int var = -1;
        int low = 0;
        int high = 0;
    };

    struct TripleKey {
        int var;
        int low;
        int high;

        bool operator==(const TripleKey& other) const {
            return var == other.var && low == other.low && high == other.high;
        }
    };

    struct TripleHash {
        size_t operator()(const TripleKey& key) const {
            size_t h = static_cast<size_t>(key.var);
            h = h * 1315423911u + static_cast<size_t>(key.low);
            h = h * 1315423911u + static_cast<size_t>(key.high);
            return h;
        }
    };

    struct ApplyKey {
        char op;
        int lhs;
        int rhs;

        bool operator==(const ApplyKey& other) const {
            return op == other.op && lhs == other.lhs && rhs == other.rhs;
        }
    };

    struct ApplyHash {
        size_t operator()(const ApplyKey& key) const {
            size_t h = static_cast<size_t>(key.op);
            h = h * 1315423911u + static_cast<size_t>(key.lhs);
            h = h * 1315423911u + static_cast<size_t>(key.rhs);
            return h;
        }
    };

    explicit BDDManager(int variableCount) : variableCount(variableCount) {
        nodes.push_back(Node{-1, 0, 0});
        nodes.push_back(Node{-1, 1, 1});
    }

    int makeNode(int var, int low, int high) {
        if (low == high) {
            return low;
        }
        TripleKey key{var, low, high};
        auto it = uniqueTable.find(key);
        if (it != uniqueTable.end()) {
            return it->second;
        }
        int id = static_cast<int>(nodes.size());
        nodes.push_back(Node{var, low, high});
        uniqueTable.emplace(key, id);
        return id;
    }

    int variableNode(int var) {
        return makeNode(var, 0, 1);
    }

    int negate(int root) {
        if (root == 0) {
            return 1;
        }
        if (root == 1) {
            return 0;
        }
        auto it = notMemo.find(root);
        if (it != notMemo.end()) {
            return it->second;
        }
        const Node& node = nodes[root];
        int value = makeNode(node.var, negate(node.low), negate(node.high));
        notMemo.emplace(root, value);
        return value;
    }

    int apply(char op, int lhs, int rhs) {
        if (op == '&') {
            if (lhs == 0 || rhs == 0) {
                return 0;
            }
            if (lhs == 1) {
                return rhs;
            }
            if (rhs == 1) {
                return lhs;
            }
            if (lhs == rhs) {
                return lhs;
            }
        } else if (op == '|') {
            if (lhs == 1 || rhs == 1) {
                return 1;
            }
            if (lhs == 0) {
                return rhs;
            }
            if (rhs == 0) {
                return lhs;
            }
            if (lhs == rhs) {
                return lhs;
            }
        }

        if ((op == '&' || op == '|') && lhs > rhs) {
            swap(lhs, rhs);
        }

        ApplyKey key{op, lhs, rhs};
        auto it = applyMemo.find(key);
        if (it != applyMemo.end()) {
            return it->second;
        }

        int topVar = min(variableOf(lhs), variableOf(rhs));
        int lhsLow = cofactor(lhs, topVar, false);
        int lhsHigh = cofactor(lhs, topVar, true);
        int rhsLow = cofactor(rhs, topVar, false);
        int rhsHigh = cofactor(rhs, topVar, true);

        int low = apply(op, lhsLow, rhsLow);
        int high = apply(op, lhsHigh, rhsHigh);
        int value = makeNode(topVar, low, high);
        applyMemo.emplace(key, value);
        return value;
    }

    int cubeToBDD(const Cube& cube) {
        uint64_t key = cubeKey(cube);
        auto it = cubeMemo.find(key);
        if (it != cubeMemo.end()) {
            return it->second;
        }

        int root = 1;
        for (int var = 0; var < variableCount; ++var) {
            uint32_t bit = 1u << var;
            if ((cube.fixedMask & bit) == 0) {
                continue;
            }
            int literal = variableNode(var);
            if ((cube.valueMask & bit) == 0) {
                literal = negate(literal);
            }
            root = apply('&', root, literal);
            if (root == 0) {
                break;
            }
        }

        cubeMemo.emplace(key, root);
        return root;
    }

    int coverToBDD(const vector<Cube>& cover) {
        int root = 0;
        for (const Cube& cube : cover) {
            root = apply('|', root, cubeToBDD(cube));
            if (root == 1) {
                break;
            }
        }
        return root;
    }

    bool evaluate(int root, uint32_t assignment) const {
        int nodeId = root;
        while (nodeId > 1) {
            const Node& node = nodes[nodeId];
            uint32_t bit = 1u << node.var;
            nodeId = (assignment & bit) ? node.high : node.low;
        }
        return nodeId == 1;
    }

    bool collectOnePaths(int root, vector<Cube>& out, size_t limit) const {
        Cube start;
        return collectOnePathsDfs(root, start, out, limit);
    }

private:
    int variableCount;
    vector<Node> nodes;
    unordered_map<TripleKey, int, TripleHash> uniqueTable;
    unordered_map<ApplyKey, int, ApplyHash> applyMemo;
    unordered_map<int, int> notMemo;
    unordered_map<uint64_t, int> cubeMemo;

    int variableOf(int root) const {
        return root <= 1 ? variableCount : nodes[root].var;
    }

    int cofactor(int root, int var, bool value) {
        if (root <= 1) {
            return root;
        }
        const Node& node = nodes[root];
        if (node.var == var) {
            return value ? node.high : node.low;
        }
        if (node.var > var) {
            return root;
        }
        int low = cofactor(node.low, var, value);
        int high = cofactor(node.high, var, value);
        return makeNode(node.var, low, high);
    }

    bool collectOnePathsDfs(int root, Cube current, vector<Cube>& out, size_t limit) const {
        if (out.size() > limit) {
            return false;
        }
        if (root == 0) {
            return true;
        }
        if (root == 1) {
            out.push_back(current);
            return out.size() <= limit;
        }

        const Node& node = nodes[root];
        uint32_t bit = 1u << node.var;

        Cube lowCube = current;
        lowCube.fixedMask |= bit;
        if (!collectOnePathsDfs(node.low, lowCube, out, limit)) {
            return false;
        }

        Cube highCube = current;
        highCube.fixedMask |= bit;
        highCube.valueMask |= bit;
        if (!collectOnePathsDfs(node.high, highCube, out, limit)) {
            return false;
        }

        return out.size() <= limit;
    }
};

class ExpressionParser {
public:
    ExpressionParser(const string& expr, const vector<char>& variables, BDDManager& manager)
        : expr(expr), manager(manager) {
        varIndex.assign(26, -1);
        for (size_t i = 0; i < variables.size(); ++i) {
            varIndex[variables[i] - 'a'] = static_cast<int>(i);
        }
    }

    int parse() {
        int root = parseOr();
        if (!error.empty()) {
            return 0;
        }
        if (pos != expr.size()) {
            error = string(u8"表达式中存在无法识别的后缀，从位置 ") + to_string(pos + 1) + u8" 开始。";
            return 0;
        }
        return root;
    }

    const string& getError() const {
        return error;
    }

private:
    const string& expr;
    BDDManager& manager;
    vector<int> varIndex;
    size_t pos = 0;
    string error;

    int parseOr() {
        int root = parseAnd();
        while (error.empty() && match('+')) {
            int rhs = parseAnd();
            root = manager.apply('|', root, rhs);
        }
        return root;
    }

    int parseAnd() {
        int root = parsePrimary();
        while (error.empty()) {
            if (match('*') || match('.')) {
                int rhs = parsePrimary();
                root = manager.apply('&', root, rhs);
                continue;
            }
            if (startsPrimary(current())) {
                int rhs = parsePrimary();
                root = manager.apply('&', root, rhs);
                continue;
            }
            break;
        }
        return root;
    }

    int parsePrimary() {
        char ch = current();
        if (ch == '\0') {
            error = u8"表达式意外结束。";
            return 0;
        }

        if (match('(')) {
            int root = parseOr();
            if (!match(')')) {
                error = u8"括号不匹配。";
                return 0;
            }
            return root;
        }

        if (ch == '0' || ch == '1') {
            ++pos;
            return ch == '1' ? 1 : 0;
        }

        if ((ch >= 'a' && ch <= 'y') || (ch >= 'A' && ch <= 'Y')) {
            ++pos;
            char lower = static_cast<char>(tolower(static_cast<unsigned char>(ch)));
            int idx = varIndex[lower - 'a'];
            if (idx < 0) {
                error = string(u8"表达式中出现了未在变量列表声明的变量: ") + lower;
                return 0;
            }
            int literal = manager.variableNode(idx);
            if (isupper(static_cast<unsigned char>(ch))) {
                literal = manager.negate(literal);
            }
            return literal;
        }

        error = string(u8"表达式中出现非法字符: ") + ch;
        return 0;
    }

    bool match(char ch) {
        if (current() == ch) {
            ++pos;
            return true;
        }
        return false;
    }

    char current() const {
        return pos < expr.size() ? expr[pos] : '\0';
    }

    static bool startsPrimary(char ch) {
        return ch == '(' || ch == '0' || ch == '1' ||
               (ch >= 'a' && ch <= 'y') || (ch >= 'A' && ch <= 'Y');
    }
};

struct CoverSimplifier {
    BDDManager& manager;
    int targetRoot;
    int targetComplement;
    unordered_map<uint64_t, bool> implicantMemo;

    bool isImplicant(const Cube& cube) {
        uint64_t key = cubeKey(cube);
        auto it = implicantMemo.find(key);
        if (it != implicantMemo.end()) {
            return it->second;
        }
        int intersection = manager.apply('&', manager.cubeToBDD(cube), targetComplement);
        bool ok = (intersection == 0);
        implicantMemo.emplace(key, ok);
        return ok;
    }

    static bool cubeLess(const Cube& lhs, const Cube& rhs) {
        if (lhs.fixedMask != rhs.fixedMask) {
            return lhs.fixedMask < rhs.fixedMask;
        }
        return lhs.valueMask < rhs.valueMask;
    }

    static void normalize(vector<Cube>& cover) {
        sort(cover.begin(), cover.end(), cubeLess);
        cover.erase(unique(cover.begin(), cover.end()), cover.end());
    }

    bool removeSubsumed(vector<Cube>& cover) {
        normalize(cover);
        vector<bool> removed(cover.size(), false);
        bool changed = false;
        for (size_t i = 0; i < cover.size(); ++i) {
            if (removed[i]) {
                continue;
            }
            for (size_t j = 0; j < cover.size(); ++j) {
                if (i == j || removed[j]) {
                    continue;
                }
                if (subsumes(cover[i], cover[j])) {
                    removed[j] = true;
                    changed = true;
                }
            }
        }

        if (!changed) {
            return false;
        }

        vector<Cube> next;
        next.reserve(cover.size());
        for (size_t i = 0; i < cover.size(); ++i) {
            if (!removed[i]) {
                next.push_back(cover[i]);
            }
        }
        cover.swap(next);
        return true;
    }

    bool expand(vector<Cube>& cover) {
        for (Cube& cube : cover) {
            uint32_t mask = cube.fixedMask;
            while (mask != 0) {
                uint32_t bit = mask & (~mask + 1u);
                mask ^= bit;
                Cube candidate = cube;
                candidate.fixedMask &= ~bit;
                candidate.valueMask &= ~bit;
                if (candidate == cube) {
                    continue;
                }
                if (isImplicant(candidate)) {
                    cube = candidate;
                    return true;
                }
            }
        }
        return false;
    }

    bool merge(vector<Cube>& cover) {
        normalize(cover);
        for (size_t i = 0; i < cover.size(); ++i) {
            for (size_t j = i + 1; j < cover.size(); ++j) {
                if (cover[i].fixedMask != cover[j].fixedMask) {
                    continue;
                }
                uint32_t diff = (cover[i].valueMask ^ cover[j].valueMask) & cover[i].fixedMask;
                if (diff == 0 || (diff & (diff - 1u)) != 0) {
                    continue;
                }
                Cube merged = cover[i];
                merged.fixedMask &= ~diff;
                merged.valueMask &= ~diff;
                if (!isImplicant(merged)) {
                    continue;
                }

                vector<Cube> next;
                next.reserve(cover.size() - 1);
                for (size_t k = 0; k < cover.size(); ++k) {
                    if (k != i && k != j) {
                        next.push_back(cover[k]);
                    }
                }
                next.push_back(merged);
                cover.swap(next);
                return true;
            }
        }
        return false;
    }

    bool removeRedundant(vector<Cube>& cover) {
        for (size_t i = 0; i < cover.size(); ++i) {
            vector<Cube> candidate;
            candidate.reserve(cover.size() - 1);
            for (size_t j = 0; j < cover.size(); ++j) {
                if (i != j) {
                    candidate.push_back(cover[j]);
                }
            }
            if (manager.coverToBDD(candidate) == targetRoot) {
                cover.swap(candidate);
                return true;
            }
        }
        return false;
    }

    vector<Cube> simplify(vector<Cube> cover) {
        normalize(cover);
        if (cover.empty()) {
            return cover;
        }

        bool changed = true;
        while (changed) {
            changed = false;

            if (removeSubsumed(cover)) {
                changed = true;
                continue;
            }
            if (expand(cover)) {
                changed = true;
                continue;
            }
            if (removeSubsumed(cover)) {
                changed = true;
                continue;
            }
            if (merge(cover)) {
                changed = true;
                continue;
            }
            if (removeSubsumed(cover)) {
                changed = true;
                continue;
            }
            if (removeRedundant(cover)) {
                changed = true;
                continue;
            }
        }

        normalize(cover);
        return cover;
    }
};

string productLiteral(bool positive, char var) {
    return string(1, positive ? var : static_cast<char>(toupper(static_cast<unsigned char>(var))));
}

string cubeToSOPTerm(const Cube& cube, const vector<char>& variables) {
    if (cube.fixedMask == 0) {
        return "1";
    }
    string out;
    for (size_t i = 0; i < variables.size(); ++i) {
        uint32_t bit = 1u << i;
        if ((cube.fixedMask & bit) == 0) {
            continue;
        }
        bool positive = (cube.valueMask & bit) != 0;
        out += productLiteral(positive, variables[i]);
    }
    return out.empty() ? "1" : out;
}

string cubeToPOSClause(const Cube& cube, const vector<char>& variables) {
    if (cube.fixedMask == 0) {
        return "(0)";
    }

    string inside;
    bool first = true;
    for (size_t i = 0; i < variables.size(); ++i) {
        uint32_t bit = 1u << i;
        if ((cube.fixedMask & bit) == 0) {
            continue;
        }
        if (!first) {
            inside.push_back('+');
        }
        first = false;
        bool originalPositive = (cube.valueMask & bit) != 0;
        inside += productLiteral(!originalPositive, variables[i]);
    }
    return "(" + inside + ")";
}

string coverToSOP(const vector<Cube>& cover, const vector<char>& variables) {
    if (cover.empty()) {
        return "0";
    }
    ostringstream oss;
    for (size_t i = 0; i < cover.size(); ++i) {
        if (i != 0) {
            oss << '+';
        }
        oss << cubeToSOPTerm(cover[i], variables);
    }
    return oss.str();
}

string coverToPOS(const vector<Cube>& offCover, const vector<char>& variables) {
    if (offCover.empty()) {
        return "1";
    }
    ostringstream oss;
    for (const Cube& cube : offCover) {
        oss << cubeToPOSClause(cube, variables);
    }
    return oss.str();
}

string bitLabel(int grayValue, int bitCount) {
    if (bitCount == 0) {
        return "-";
    }
    string label(bitCount, '0');
    for (int i = 0; i < bitCount; ++i) {
        int shift = bitCount - 1 - i;
        label[i] = ((grayValue >> shift) & 1) ? '1' : '0';
    }
    return label;
}

void printKMap(ostream& out, BDDManager& manager, int functionRoot, const vector<char>& variables) {
    const int variableCount = static_cast<int>(variables.size());
    const int rowVars = variableCount / 2;
    const int colVars = variableCount - rowVars;
    const size_t rowCount = size_t{1} << rowVars;
    const size_t colCount = size_t{1} << colVars;
    const size_t cellCount = rowCount * colCount;
    const size_t kFullPrintLimit = 1024;

    string rowVarText;
    for (int i = 0; i < rowVars; ++i) {
        rowVarText.push_back(variables[i]);
    }
    if (rowVarText.empty()) {
        rowVarText = "-";
    }

    string colVarText;
    for (int i = rowVars; i < variableCount; ++i) {
        colVarText.push_back(variables[i]);
    }
    if (colVarText.empty()) {
        colVarText = "-";
    }

    out << u8"卡诺图\n";
    out << u8"行变量: " << rowVarText << u8"，列变量: " << colVarText
        << u8"，尺寸: " << rowCount << " x " << colCount << '\n';
    out << u8"Gray 顺序按二进制标签显示。\n";

    if (cellCount > kFullPrintLimit) {
        out << u8"变量过多，完整卡诺图共有 " << cellCount
            << u8" 个单元，已省略具体表格输出。\n";
        return;
    }

    vector<string> rowLabels(rowCount);
    vector<string> colLabels(colCount);
    size_t width = 1;
    for (size_t i = 0; i < rowCount; ++i) {
        rowLabels[i] = bitLabel(static_cast<int>(i ^ (i >> 1)), rowVars);
        width = max(width, rowLabels[i].size());
    }
    for (size_t i = 0; i < colCount; ++i) {
        colLabels[i] = bitLabel(static_cast<int>(i ^ (i >> 1)), colVars);
        width = max(width, colLabels[i].size());
    }
    width = max<size_t>(width, 3);

    out << setw(static_cast<int>(width)) << "";
    for (const string& label : colLabels) {
        out << ' ' << setw(static_cast<int>(width)) << label;
    }
    out << '\n';

    for (size_t r = 0; r < rowCount; ++r) {
        out << setw(static_cast<int>(width)) << rowLabels[r];
        int rowGray = static_cast<int>(r ^ (r >> 1));
        for (size_t c = 0; c < colCount; ++c) {
            int colGray = static_cast<int>(c ^ (c >> 1));
            uint32_t assignment = 0;
            for (int i = 0; i < rowVars; ++i) {
                int shift = rowVars - 1 - i;
                if ((rowGray >> shift) & 1) {
                    assignment |= (1u << i);
                }
            }
            for (int i = 0; i < colVars; ++i) {
                int shift = colVars - 1 - i;
                if ((colGray >> shift) & 1) {
                    assignment |= (1u << (rowVars + i));
                }
            }
            out << ' ' << setw(static_cast<int>(width))
                << (manager.evaluate(functionRoot, assignment) ? '1' : '0');
        }
        out << '\n';
    }
}

bool buildAndSimplifyCover(BDDManager& manager, int root, int complementRoot, vector<Cube>& simplifiedCover) {
    const size_t kPathLimit = 50000;
    vector<Cube> cover;
    if (!manager.collectOnePaths(root, cover, kPathLimit)) {
        return false;
    }
    CoverSimplifier simplifier{manager, root, complementRoot, {}};
    simplifiedCover = simplifier.simplify(cover);
    return true;
}

void printInteractiveHint() {
    cout << u8"逻辑表达式卡诺图与化简程序\n";
    cout << u8"输入格式提示:\n";
    cout << u8"1. 必须以 F(a,b,c,...)= 开头，例如 Z(a,b,c)=aB+cdE\n";
    cout << u8"2. 小写字母表示原变量，大写字母表示反变量，例如 aB 表示 a&~b\n";
    cout << u8"3. 支持 + 表示或，* 或 . 表示与，相邻项默认也表示与\n";
    cout << u8"4. 支持括号，例如 Z(a,b,c,d,e)=(d+e)(a+b+c)(B+C+D+E)(a+B+c+D+e)\n";
    cout << u8"5. 输入空行或 exit/quit/q 结束\n";
}

bool isExitCommand(const string& normalized) {
    return normalized.empty() || normalized == "q" || normalized == "quit" || normalized == "exit";
}

void processExpression(const string& rawInput) {
    HeaderParseResult header = parseHeader(rawInput);
    if (!header.error.empty()) {
        cout << u8"解析失败: " << header.error << "\n\n";
        return;
    }

    BDDManager manager(static_cast<int>(header.variables.size()));
    ExpressionParser parser(header.expression, header.variables, manager);
    int functionRoot = parser.parse();
    if (!parser.getError().empty()) {
        cout << u8"解析失败: " << parser.getError() << "\n\n";
        return;
    }

    int notFunctionRoot = manager.negate(functionRoot);

    cout << u8"输入表达式: " << header.normalizedInput << "\n\n";
    printKMap(cout, manager, functionRoot, header.variables);
    cout << '\n';

    if (functionRoot == 0) {
        cout << u8"最简与或表达式(SOP): 0\n";
    } else if (functionRoot == 1) {
        cout << u8"最简与或表达式(SOP): 1\n";
    } else {
        vector<Cube> simplifiedSOP;
        if (buildAndSimplifyCover(manager, functionRoot, notFunctionRoot, simplifiedSOP)) {
            cout << u8"最简与或表达式(SOP): " << coverToSOP(simplifiedSOP, header.variables) << '\n';
        } else {
            cout << u8"最简与或表达式(SOP): 路径数过多，未能在限制内展开。\n";
        }
    }

    if (functionRoot == 0) {
        cout << u8"最简或与表达式(POS): 0\n\n";
        return;
    }
    if (functionRoot == 1) {
        cout << u8"最简或与表达式(POS): 1\n\n";
        return;
    }

    vector<Cube> simplifiedPOSCover;
    if (buildAndSimplifyCover(manager, notFunctionRoot, functionRoot, simplifiedPOSCover)) {
        cout << u8"最简或与表达式(POS): " << coverToPOS(simplifiedPOSCover, header.variables) << "\n\n";
    } else {
        cout << u8"最简或与表达式(POS): 路径数过多，未能在限制内展开。\n\n";
    }
}

}  // namespace

int main() {
    initConsoleEncoding();
    printInteractiveHint();
    cout << flush;

    string line;
    while (true) {
        cout << u8"\n请输入逻辑表达式> " << flush;
        if (!getline(cin, line)) {
            cout << "\n";
            break;
        }

        string normalized = trimSpaces(line);
        if (isExitCommand(normalized)) {
            cout << u8"程序结束。\n";
            break;
        }

        processExpression(line);
        cout << flush;
    }

    return 0;
}
