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

class LambdaHandler : public clang::ast_matchers::MatchFinder::MatchCallback {
public:
    LambdaHandler(clang::tooling::Replacements * replace) : replace_(replace) {}

    virtual void run(clang::ast_matchers::MatchFinder::MatchResult const& result) override {
        using namespace clang;
        if (LambdaExpr const * lambda = result.Nodes.getNodeAs<LambdaExpr>("lambda")) {
            VarDecl    const * lambda_var = result.Nodes.getNodeAs<VarDecl>("lambdavar");
            // display lambda contents
            auto body      = lambda->getBody();
            auto bodyStart = body->getLocStart().getLocWithOffset(1);   // skip left brace
            auto bodyEnd   = body->getLocEnd().getLocWithOffset(-1);    // drop right brace
            auto bodyRange = CharSourceRange::getTokenRange(bodyStart, bodyEnd);
            lambda_bodies[lambda_var->getQualifiedNameAsString()] =
                Lexer::getSourceText(bodyRange,
                                     *result.SourceManager,
                                     result.Context->getLangOpts()).str();
        }
    }
    std::map<std::string, std::string> lambda_bodies;
private:
    clang::tooling::Replacements * replace_;
};

class CaptureHandler : public clang::ast_matchers::MatchFinder::MatchCallback {
public:
    CaptureHandler(clang::tooling::Replacements * replace) : replace_(replace) {}

    virtual void run(clang::ast_matchers::MatchFinder::MatchResult const& result) override {
        using namespace clang;
        if (VarDecl const * capture = result.Nodes.getNodeAs<VarDecl>("capture")) {
            std::cout << "found captured variable " << capture->getQualifiedNameAsString() << " of type " << capture->getType().getAsString() << "\n";
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
                           hasDescendant(lambdaExpr(child_matcher).bind("lambda")))),
                   decl().bind("lambdavar"));
}

namespace custom_matchers {
// Clang does not currently provide a Traversal matcher to go from lambda expressions to
// associated capture variables, so let's make one.  Following the forEachSwitchCase pattern:
AST_MATCHER_P(clang::LambdaExpr, forEachCaptureVar,
              clang::ast_matchers::internal::Matcher<clang::VarDecl>, InnerMatcher) {
    using namespace clang::ast_matchers::internal;
    BoundNodesTreeBuilder Result;
    bool Matched = false;
    for (clang::LambdaCapture const & lc : Node.captures()) {
        BoundNodesTreeBuilder LambdaBuilder(*Builder);
        bool CaptureMatched = InnerMatcher.matches(*lc.getCapturedVar(), Finder, &LambdaBuilder);
        if (CaptureMatched) {
            Matched = true;
            Result.addMatch(LambdaBuilder);
        }
    }
    *Builder = std::move(Result);
    return Matched;
}

}

int main(int argc, char const **argv) {
    using namespace clang;
    using namespace clang::tooling;
    using namespace clang::ast_matchers;

    CommonOptionsParser opt(argc, argv, ToolingSampleCategory);
    RefactoringTool     tool(opt.getCompilations(), opt.getSourcePathList());

    // set up callbacks
    LambdaHandler       lambda_handler(&tool.getReplacements());
    CaptureHandler      capture_handler(&tool.getReplacements());

    MatchFinder  finder;

    // lambda bodies
    auto lambda_matcher = make_lambda_matcher(anything());
    finder.addMatcher(lambda_matcher, &lambda_handler);

    // usage of captured variables
    using custom_matchers::forEachCaptureVar;
    auto capture_matcher = make_lambda_matcher(forEachCaptureVar(decl().bind("capture")));
    finder.addMatcher(capture_matcher, &capture_handler);

    if (int result = tool.run(newFrontendActionFactory(&finder).get())) {
        return result;
    }

    for (auto lb : lambda_handler.lambda_bodies) {
        std::cout << "lambda " << lb.first << " has body:\n" << lb.second << "\n";
    }

    std::cout << "Collected replacements:\n";
    for (auto const & r : tool.getReplacements()) {
        std::cout << r.toString() << "\n";
    }

    return 0;
}
