#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#define main kmap_original_main
#include "../main.cpp"
#undef main

namespace {

struct SimplifiedOutput {
    std::string sop;
    std::string pos;
};

bool equivalentExpressions(const std::vector<char>& variables, const std::string& lhsExpr, const std::string& rhsExpr) {
    BDDManager manager(static_cast<int>(variables.size()));
    ExpressionParser lhs(lhsExpr, variables, manager);
    int lhsRoot = lhs.parse();
    if (!lhs.getError().empty()) {
        return false;
    }

    ExpressionParser rhs(rhsExpr, variables, manager);
    int rhsRoot = rhs.parse();
    if (!rhs.getError().empty()) {
        return false;
    }

    const uint32_t total = 1u << static_cast<uint32_t>(variables.size());
    for (uint32_t assignment = 0; assignment < total; ++assignment) {
        if (manager.evaluate(lhsRoot, assignment) != manager.evaluate(rhsRoot, assignment)) {
            return false;
        }
    }
    return true;
}

bool simplifyExpression(const std::string& rawInput, SimplifiedOutput& out) {
    HeaderParseResult header = parseHeader(rawInput);
    if (!header.error.empty()) {
        return false;
    }

    BDDManager manager(static_cast<int>(header.variables.size()));
    ExpressionParser parser(header.expression, header.variables, manager);
    int functionRoot = parser.parse();
    if (!parser.getError().empty()) {
        return false;
    }

    int notFunctionRoot = manager.negate(functionRoot);

    if (functionRoot == 0) {
        out.sop = "0";
        out.pos = "0";
        return true;
    }
    if (functionRoot == 1) {
        out.sop = "1";
        out.pos = "1";
        return true;
    }

    std::vector<Cube> simplifiedSOP;
    if (!buildAndSimplifyCover(manager, functionRoot, notFunctionRoot, simplifiedSOP)) {
        return false;
    }
    out.sop = coverToSOP(simplifiedSOP, header.variables);

    std::vector<Cube> simplifiedPOS;
    if (!buildAndSimplifyCover(manager, notFunctionRoot, functionRoot, simplifiedPOS)) {
        return false;
    }
    out.pos = coverToPOS(simplifiedPOS, header.variables);
    return true;
}

bool runCase(const std::string& rawInput) {
    HeaderParseResult header = parseHeader(rawInput);
    if (!header.error.empty()) {
        std::cerr << "Parse header failed: " << rawInput << "\n";
        return false;
    }

    SimplifiedOutput simplified;
    if (!simplifyExpression(rawInput, simplified)) {
        std::cerr << "Simplification failed: " << rawInput << "\n";
        return false;
    }

    if (!equivalentExpressions(header.variables, header.expression, simplified.sop)) {
        std::cerr << "SOP not equivalent.\nInput: " << rawInput << "\nSOP: " << simplified.sop << "\n";
        return false;
    }

    if (!equivalentExpressions(header.variables, header.expression, simplified.pos)) {
        std::cerr << "POS not equivalent.\nInput: " << rawInput << "\nPOS: " << simplified.pos << "\n";
        return false;
    }

    return true;
}

}  // namespace

int main() {
    const std::vector<std::string> cases = {
        "F(a,b,c,d)=(A+B)(B+c)(a+c+d)",
        "F(a,b,c,d)=(b+a)(c)",
        "F(a,b,c,d)=(a+b+c)(a+c)(b+d)",
    };

    for (const std::string& testCase : cases) {
        if (!runCase(testCase)) {
            return 1;
        }
    }

    std::cout << "All regression POS/SOP cases passed.\n";
    return 0;
}
