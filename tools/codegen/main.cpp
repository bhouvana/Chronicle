// chronicle-codegen: docs/10-roadmap.md's v0.5 codegen tool. Scans C++
// source for structs marked CHRONICLE_TRACKABLE (see
// include/chronicle/tracked_type.hpp -- expands to
// [[clang::annotate("chronicle::track")]], not the plain
// [[chronicle::track]] the roadmap sketch originally showed, because that
// form is invisible to any tool: confirmed via a real AST dump that Clang
// drops unrecognized attributes without a trace, before choosing
// clang::annotate as the actual marker) and emits a
// CHRONICLE_TRACK_TYPE(Type, field1, ..., fieldN) call per struct, listing
// every direct data member. Built directly against the Clang/LLVM libraries
// (see tools/codegen/CMakeLists.txt) -- unlike the rest of Chronicle, this
// tool is NOT part of the default build, since most consumers of the
// library won't have a full LLVM/Clang development install available (the
// official prebuilt Windows binaries don't include one at all -- see
// docs/adr/0012-chronicle-codegen-libtooling.md).
//
// Structs with more than 8 fields are skipped with a warning:
// CHRONICLE_TRACK_TYPE's macro-based field list tops out at 8 (see
// tracked_type.hpp) -- a real, documented limit this tool must respect,
// not silently emit code that won't compile.

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/Attr.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/Support/CommandLine.h"

#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using namespace clang;
using namespace clang::tooling;

namespace {

constexpr char kMarker[] = "chronicle::track";
constexpr std::size_t kMaxFields = 8; // must match CHRONICLE_PP_MEMBER_8 in tracked_type.hpp

struct TrackableStruct {
    std::string name;
    std::vector<std::string> fields;
};

bool hasTrackMarker(CXXRecordDecl const* decl) {
    for (auto const* attr : decl->specific_attrs<AnnotateAttr>()) {
        if (attr->getAnnotation() == kMarker) {
            return true;
        }
    }
    return false;
}

class TrackVisitor : public RecursiveASTVisitor<TrackVisitor> {
public:
    explicit TrackVisitor(std::vector<TrackableStruct>& out) : out_(out) {}

    bool VisitCXXRecordDecl(CXXRecordDecl* decl) {
        if (!decl->isCompleteDefinition() || !hasTrackMarker(decl)) {
            return true;
        }

        TrackableStruct s;
        s.name = decl->getQualifiedNameAsString();
        for (auto const* field : decl->fields()) {
            s.fields.push_back(field->getNameAsString());
        }

        if (s.fields.empty()) {
            llvm::errs() << "chronicle-codegen: warning: " << s.name
                         << " is marked CHRONICLE_TRACKABLE but has no data members, skipping\n";
            return true;
        }
        if (s.fields.size() > kMaxFields) {
            llvm::errs() << "chronicle-codegen: warning: " << s.name << " has " << s.fields.size()
                         << " fields, exceeding CHRONICLE_TRACK_TYPE's " << kMaxFields
                         << "-field limit -- skipping\n";
            return true;
        }
        out_.push_back(std::move(s));
        return true;
    }

private:
    std::vector<TrackableStruct>& out_;
};

class TrackConsumer : public ASTConsumer {
public:
    explicit TrackConsumer(std::vector<TrackableStruct>& out) : visitor_(out) {}
    void HandleTranslationUnit(ASTContext& context) override {
        visitor_.TraverseDecl(context.getTranslationUnitDecl());
    }

private:
    TrackVisitor visitor_;
};

class TrackAction : public ASTFrontendAction {
public:
    explicit TrackAction(std::vector<TrackableStruct>& out) : out_(out) {}
    std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance&, StringRef) override {
        return std::make_unique<TrackConsumer>(out_);
    }

private:
    std::vector<TrackableStruct>& out_;
};

class TrackActionFactory : public FrontendActionFactory {
public:
    explicit TrackActionFactory(std::vector<TrackableStruct>& out) : out_(out) {}
    std::unique_ptr<FrontendAction> create() override { return std::make_unique<TrackAction>(out_); }

private:
    std::vector<TrackableStruct>& out_;
};

} // namespace

static llvm::cl::OptionCategory ToolCategory("chronicle-codegen options");
static llvm::cl::opt<std::string> OutputPath(
    "o", llvm::cl::desc("Output header path (defaults to stdout)"), llvm::cl::cat(ToolCategory));

int main(int argc, char const** argv) {
    auto expectedParser = CommonOptionsParser::create(argc, argv, ToolCategory);
    if (!expectedParser) {
        llvm::errs() << expectedParser.takeError();
        return 1;
    }
    CommonOptionsParser& optionsParser = expectedParser.get();
    ClangTool tool(optionsParser.getCompilations(), optionsParser.getSourcePathList());

    std::vector<TrackableStruct> results;
    TrackActionFactory factory(results);
    int const ret = tool.run(&factory);
    if (ret != 0) {
        return ret;
    }

    std::ofstream outFile;
    if (!OutputPath.empty()) {
        outFile.open(OutputPath);
        if (!outFile) {
            llvm::errs() << "chronicle-codegen: cannot open output file: " << OutputPath << "\n";
            return 1;
        }
    }
    std::ostream& out = OutputPath.empty() ? std::cout : outFile;

    out << "// Generated by chronicle-codegen -- do not edit by hand.\n"
        << "#pragma once\n\n"
        << "#include <chronicle/chronicle.hpp>\n\n";
    for (auto const& s : results) {
        out << "CHRONICLE_TRACK_TYPE(" << s.name;
        for (auto const& field : s.fields) {
            out << ", " << field;
        }
        out << ");\n";
    }

    llvm::errs() << "chronicle-codegen: generated " << results.size() << " registration(s)\n";
    return 0;
}
