// Performing the second refactoring stage of my example: finding our special lambda
// expressions and analyzing how they use variables from outer scopes

#include <iostream>

#include "clang/AST/AST.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Refactoring.h"
#include "clang/Tooling/Tooling.h"

static llvm::cl::OptionCategory ToolingSampleCategory("Lambda extractor");

class CaptureHandler : public clang::ast_matchers::MatchFinder::MatchCallback {
public:
    CaptureHandler(clang::tooling::Replacements * replace) : replace_(replace) {}

    virtual void run(clang::ast_matchers::MatchFinder::MatchResult const& result) override {
        std::cout << "found a match!\n";
    }
private:
    clang::tooling::Replacements * replace_;
};

int main(int argc, char const **argv) {
    using namespace clang;
    using namespace clang::tooling;
    using namespace clang::ast_matchers;

    CommonOptionsParser opt(argc, argv, ToolingSampleCategory);
    RefactoringTool     tool(opt.getCompilations(), opt.getSourcePathList());

    // set up callbacks
    CaptureHandler      capture_handler(&tool.getReplacements());

    MatchFinder  finder;
    finder.addMatcher(varDecl(hasType(autoType()),
                              matchesName("expression_capture_[0-9]+"),
                              hasInitializer(
                                  constructExpr(
                                      hasDescendant(lambdaExpr(
                                                        hasType(recordDecl(
                                                                    forEach(fieldDecl().bind("capture"))))))))),
                      &capture_handler);

    if (int result = tool.run(newFrontendActionFactory(&finder).get())) {
        return result;
    }

    std::cout << "Collected replacements:\n";
    for (auto const & r : tool.getReplacements()) {
        std::cout << r.toString() << "\n";
    }

    return 0;
}
