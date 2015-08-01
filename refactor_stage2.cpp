// Performing the second refactoring stage of my example: finding our special lambda
// expressions and analyzing how they use variables from outer scopes

// sample command line:
// ./rs2 -p=. -extra-arg='-I/usr/lib/gcc/x86_64-linux-gnu/4.9/include' -extra-arg='-std=c++11' ../test.cpp --

#include <iostream>

#include "clang/AST/AST.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Refactoring.h"
#include "clang/Tooling/Tooling.h"
#include "clang/Lex/Lexer.h"

static llvm::cl::OptionCategory ToolingSampleCategory("Lambda extractor");

class CaptureHandler : public clang::ast_matchers::MatchFinder::MatchCallback {
public:
    CaptureHandler(clang::tooling::Replacements * replace) : replace_(replace) {}

    virtual void run(clang::ast_matchers::MatchFinder::MatchResult const& result) override {
        using namespace clang;
        if (LambdaExpr const * lambda = result.Nodes.getNodeAs<LambdaExpr>("lambda")) {
            // display lambda contents
            auto body      = lambda->getBody();
            auto bodyStart = body->getLocStart().getLocWithOffset(1);   // skip left brace
            auto bodyEnd   = body->getLocEnd().getLocWithOffset(-1);    // drop right brace
            std::cout << Lexer::getSourceText(CharSourceRange::getTokenRange(bodyStart, bodyEnd),
                                              *result.SourceManager,
                                              result.Context->getLangOpts()).str() << "\n";
                                              
            for (auto c : lambda->captures()) {
                if (c.capturesVariable()) {
                    auto var = c.getCapturedVar();
                    std::cout << "found captured variable " << var->getQualifiedNameAsString() << " of type " << var->getType().getAsString() << "\n";
                    // next step: find uses of this variable and determine if any are mutating
                    
                }
            }
        }
    }
private:
    clang::tooling::Replacements * replace_;
};

template<typename M>
clang::ast_matchers::DeclarationMatcher make_lambda_matcher(M const& child_matcher) {
//clang::Matcher<clang::VarDecl> make_lambda_matcher(M const& child_matcher) {
    using namespace clang;
    using namespace clang::ast_matchers;
    return varDecl(hasType(autoType()),
                              matchesName("expression_capture_[0-9]+"),
                              hasInitializer(
                                  constructExpr(
                                      hasDescendant(lambdaExpr(child_matcher).bind("lambda")))));
}

int main(int argc, char const **argv) {
    using namespace clang;
    using namespace clang::tooling;
    using namespace clang::ast_matchers;

    CommonOptionsParser opt(argc, argv, ToolingSampleCategory);
    RefactoringTool     tool(opt.getCompilations(), opt.getSourcePathList());

    // set up callbacks
    CaptureHandler      capture_handler(&tool.getReplacements());

    MatchFinder  finder;
    auto lambda_matcher = make_lambda_matcher(anything());
    finder.addMatcher(lambda_matcher, &capture_handler);

    if (int result = tool.run(newFrontendActionFactory(&finder).get())) {
        return result;
    }

    std::cout << "Collected replacements:\n";
    for (auto const & r : tool.getReplacements()) {
        std::cout << r.toString() << "\n";
    }

    return 0;
}
