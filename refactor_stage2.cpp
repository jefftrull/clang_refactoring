/**
 *   Copyright (C) 2015 Jeff Trull <edaskel@att.net>
 *
 *   Distributed under the Boost Software License, Version 1.0. (See accompanying
 *   file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
 *
 *
 */

// Performing the second refactoring stage of my example: finding our special lambda
// expressions and analyzing how they use variables from outer scopes

// sample command line:
// ./rs2 -p=. -extra-arg='-I/usr/lib/gcc/x86_64-linux-gnu/8/include' -extra-arg='-std=c++11' ../test.cpp --

#include <iostream>
#include <map>
#include <string>

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
    using replacement_map = std::map<std::string, clang::tooling::Replacements>;
public:
    LambdaHandler(replacement_map const * replace) : replace_(replace) {}

    virtual void run(clang::ast_matchers::MatchFinder::MatchResult const& result) override {
        using namespace clang;
        if (LambdaExpr const * lambda = result.Nodes.getNodeAs<LambdaExpr>("lambda")) {
            VarDecl    const * lambda_var = result.Nodes.getNodeAs<VarDecl>("lambdavar");
            // display lambda contents
            auto body      = lambda->getBody();
            auto bodyStart = body->getBeginLoc().getLocWithOffset(1);   // skip left brace
            auto bodyEnd   = body->getEndLoc().getLocWithOffset(-1);    // drop right brace
            auto bodyRange = CharSourceRange::getTokenRange(bodyStart, bodyEnd);
            lambda_bodies[lambda_var->getQualifiedNameAsString()] =
                Lexer::getSourceText(bodyRange,
                                     *result.SourceManager,
                                     result.Context->getLangOpts()).str();
        }
    }
    std::map<std::string, std::string> lambda_bodies;
private:
    replacement_map const * replace_;
};

class CaptureHandler : public clang::ast_matchers::MatchFinder::MatchCallback {
    using replacement_map = std::map<std::string, clang::tooling::Replacements>;
public:
    typedef std::map<std::string,
                     std::vector<std::pair<std::string, std::string> > > capture_map_t;
    CaptureHandler(replacement_map const * replace,
                   capture_map_t & captured_vars) : replace_(replace), captured_vars_(captured_vars) {}

    virtual void run(clang::ast_matchers::MatchFinder::MatchResult const& result) override {
        using namespace clang;
        if (VarDecl const * capture = result.Nodes.getNodeAs<VarDecl>("capture")) {
            VarDecl const * lambda_var = result.Nodes.getNodeAs<VarDecl>("lambdavar");
            captured_vars_[lambda_var->getQualifiedNameAsString()].emplace_back(capture->getQualifiedNameAsString(),
                                                                               capture->getType().getAsString());
        }
    }
private:
    replacement_map const * replace_;
    capture_map_t & captured_vars_;
};

// a matcher for our special lambdas with binders to help us extract body code
template<typename M>
clang::ast_matchers::DeclarationMatcher make_lambda_matcher(M const& child_matcher) {
    using namespace clang;
    using namespace clang::ast_matchers;
    return varDecl(hasType(autoType()),
                   matchesName("expression_capture_[0-9]+"),
                   hasInitializer(
                       hasDescendant(cxxConstructExpr(
                                         hasDescendant(lambdaExpr(child_matcher).bind("lambda"))))),
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
        if (!lc.capturesVariable()) {
            continue;
        }
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

// Clang has no matcher for simultaneously matching a caller's argument with the callee parameter
// This one will return true, with associated matches, on the first argument/parameter pair it finds
// that pass the supplied inner matchers
AST_MATCHER_P2(clang::CallExpr, hasArgParameter,
               clang::ast_matchers::internal::Matcher<clang::Expr>, ArgMatcher,
               clang::ast_matchers::internal::Matcher<clang::ParmVarDecl>, ParamMatcher) {
    using namespace clang::ast_matchers::internal;

    auto callee = Node.getDirectCallee();

    for (unsigned argno = 0;
         (argno < Node.getNumArgs()) && (argno < callee->getNumParams());
         ++argno) {
        BoundNodesTreeBuilder Result(*Builder);

        auto arg   = Node.getArg(argno);
        bool ArgMatched = ArgMatcher.matches(*arg, Finder, &Result);

        auto param = callee->getParamDecl(argno);
        bool ParamMatched = ParamMatcher.matches(*param, Finder, &Result);

        if (ArgMatched && ParamMatched) {
            *Builder = std::move(Result);
            return true;
        }
    }
    return false;
}

}

int main(int argc, char const **argv) {
    using namespace clang;
    using namespace clang::tooling;
    using namespace clang::ast_matchers;

    auto opt = CommonOptionsParser::create(argc, argv, ToolingSampleCategory);
    RefactoringTool     tool(opt->getCompilations(), opt->getSourcePathList());

    MatchFinder  finder;

    // set up callbacks
    LambdaHandler       lambda_handler(&tool.getReplacements());

    // lambda bodies
    auto lambda_matcher = make_lambda_matcher(anything());
    finder.addMatcher(lambda_matcher, &lambda_handler);

    // usage of captured variables
    using custom_matchers::forEachCaptureVar;
    using custom_matchers::hasArgParameter;
    // lambda captures used as non-const reference parameters in called functions
    auto refparm_capture_matcher =
        make_lambda_matcher(lambdaExpr(
                                // remember captures
                                forEachCaptureVar(decl().bind("capture")),
                                forEachDescendant(
                                    // look for calls within the lambda body
                                    callExpr(
                                        hasArgParameter(
                                            // with some argument that is a captured variable
                                            declRefExpr(to(equalsBoundNode("capture"))),
                                            // bound to a non-const ref parameter
                                            parmVarDecl(hasType(lValueReferenceType(
                                                                    unless(pointee(isConstQualified()))))))))));

    CaptureHandler::capture_map_t captured_ref_params;
    CaptureHandler      refparm_capture_handler(&tool.getReplacements(), captured_ref_params);
    finder.addMatcher(refparm_capture_matcher, &refparm_capture_handler);

    // lambda captures used as LHS of assignments or mutating operators
    auto assignment_capture_matcher =
        make_lambda_matcher(lambdaExpr(
                                // remember captures
                                forEachCaptureVar(decl().bind("capture")),
                                forEachDescendant(
                                    stmt(anyOf(
                                             // mutating binary operators
                                             binaryOperator(
                                                 // that use one of our captures
                                                 hasLHS(declRefExpr(to(equalsBoundNode("capture")))),
                                                 anyOf(hasOperatorName("="),
                                                       hasOperatorName("+="),
                                                       hasOperatorName("-="),
                                                       hasOperatorName("&="),
                                                       hasOperatorName("|="))),
                                             // mutating unary operators
                                             unaryOperator(
                                                 hasUnaryOperand(declRefExpr(to(equalsBoundNode("capture")))),
                                                 anyOf(hasOperatorName("++"),
                                                       hasOperatorName("--"))))))));
                                                   
    CaptureHandler::capture_map_t captured_assignments;
    CaptureHandler      assignment_capture_handler(&tool.getReplacements(), captured_assignments);
    finder.addMatcher(assignment_capture_matcher, &assignment_capture_handler);

    if (int result = tool.run(newFrontendActionFactory(&finder).get())) {
        return result;
    }

    // report accumulated data
    for (auto lb : lambda_handler.lambda_bodies) {
        std::cout << "lambda " << lb.first << " has body:\n" << lb.second << "\n";
        if (captured_ref_params.find(lb.first) != captured_ref_params.end()) {
            std::cout << "    and lvalue ref captures:\n";
            for (auto capture : captured_ref_params[lb.first]) {
                std::cout << "\t" << capture.first << " of type " << capture.second << "\n";
            }
        }
        if (captured_assignments.find(lb.first) != captured_assignments.end()) {
            std::cout << "    and assignment lhs captures:\n";
            for (auto capture : captured_assignments[lb.first]) {
                std::cout << "\t" << capture.first << " of type " << capture.second << "\n";
            }
        }
        
    }

    std::cout << "Collected replacements:\n";
    for (auto const & rs : tool.getReplacements()) {
        std::cout << "in file " << rs.first << ":\n";
        for (auto const & r : rs.second) {
            std::cout << r.toString() << "\n";
        }
    }

    return 0;
}
