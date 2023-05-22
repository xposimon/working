#include <iostream>
#include <memory>
#include <string>
#include <stdexcept>
#include <set>
#include <vector>
#include <ctime> 
#include <clang/Tooling/CommonOptionsParser.h>
#include <clang/Tooling/Tooling.h>
#include <clang/ASTMatchers/ASTMatchers.h>
#include <clang/ASTMatchers/ASTMatchFinder.h>
#include <clang/Frontend/ASTConsumers.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Rewrite/Core/Rewriter.h>
#include <llvm/Support/raw_ostream.h>

using namespace clang;
using namespace clang::ast_matchers;
using namespace clang::tooling;

template<typename ... Args>
std::string string_format( const std::string& format, Args ... args )
{
    int size_s = std::snprintf( nullptr, 0, format.c_str(), args ... ) + 1; // Extra space for '\0'
    if( size_s <= 0 ){ return std::string("");}
    auto size = static_cast<size_t>( size_s );
    std::unique_ptr<char[]> buf( new char[ size ] );
    std::snprintf( buf.get(), size, format.c_str(), args ... );
    return std::string( buf.get(), buf.get() + size - 1 ); // We don't want the '\0' inside
}

// Matcher to find if statements
StatementMatcher IfStmtMatcher = ifStmt().bind("ifStmt");

// Matcher to find for loops
StatementMatcher ForStmtMatcher = forStmt().bind("forStmt");

// Matcher to find while loops
StatementMatcher WhileStmtMatcher = whileStmt().bind("whileStmt");

// Matcher to find do-while loops
StatementMatcher DoWhileStmtMatcher = doStmt().bind("doWhileStmt");

// Matcher to find switch statements
StatementMatcher SwitchStmtMatcher = switchStmt().bind("switchStmt");

class Branch{
public:
    Branch(unsigned int StartLine, unsigned int EndLine, unsigned int branchID)
    {
        StartLine = StartLine;
        EndLine = EndLine;
        branchID = branchID;
        expl_diff = -1;
    }
    void SetDiff(double diff)
    {
        expl_diff = diff;
    }
private:
    unsigned int StartLine, EndLine;
    unsigned int branchID;
    double expl_diff;
};

class BranchInfo{
public:
    // BranchInfo();
    // ~BranchInfo();
    void AddBranch(unsigned int StartLine, unsigned int EndLine, unsigned int brid)
    {
        Branch t_br(StartLine, EndLine, brid);
        brInfo.push_back(t_br);
    }


private:
    std::vector<Branch> brInfo;
};

BranchInfo global_brInfo;

// Callback for matching statements
class StmtPrinter : public MatchFinder::MatchCallback {
public:

    void annotateBr(ASTContext &Context, const Stmt * S){
        SourceManager &SM = Context.getSourceManager();
        SourceRange SR = S->getSourceRange();

        // Get the start and end locations of the source range
        SourceLocation StartLoc = SR.getBegin(), EndLoc = SR.getEnd();
        // Get the line numbers for the start and end locations
        unsigned int StartLine = SM.getSpellingLineNumber(StartLoc),
                    EndLine = SM.getSpellingLineNumber(EndLoc);
        std::cout << "Branch found " << StartLine <<":"<<EndLine << std::endl;
        unsigned int brid = rand();
        global_brInfo.AddBranch(StartLine, EndLine, brid);

        Rewriter Rewrite(SM, Context.getLangOpts());
        std::string Annotation =  string_format(bridAssignmentFmt, brid);
        SourceLocation AnnotationLoc = SM.translateLineCol(SM.getMainFileID(), StartLine, 1);
        Rewrite.InsertTextBefore(AnnotationLoc, Annotation);

        RewriteBuffer &RewriteBuf = Rewrite.getEditBuffer(SM.getMainFileID());
        std::string ModifiedCode = std::string(RewriteBuf.begin(), RewriteBuf.end());

        // Print the modified source code
        llvm::outs() << ModifiedCode;

        return;
    }

    virtual void run(const MatchFinder::MatchResult &Result) {
        if (const Stmt *S = Result.Nodes.getNodeAs<Stmt>("ifStmt")) {
            // Get the source manager and source range for the statement
            annotateBr(*Result.Context, S);
        } else if (const Stmt *S = Result.Nodes.getNodeAs<Stmt>("forStmt")) {
            annotateBr(*Result.Context, S);
        } else if (const Stmt *S = Result.Nodes.getNodeAs<Stmt>("whileStmt")) {
            annotateBr(*Result.Context, S);
        } else if (const Stmt *S = Result.Nodes.getNodeAs<Stmt>("doWhileStmt")) {
            annotateBr(*Result.Context, S);
        } else if (const Stmt *S = Result.Nodes.getNodeAs<Stmt>("switchStmt")) {
            annotateBr(*Result.Context, S);
        }
    }

private: 
    std::string bridAssignmentFmt="*SHADOWBUG_BRID = %d;\n";
};

// ASTConsumer for handling AST matches
class StmtConsumer : public ASTConsumer {
public:
    explicit StmtConsumer(ASTContext *Context)
        : Handler(new StmtPrinter) {
        // Register the callback for the matchers
        Finder.addMatcher(IfStmtMatcher, Handler.get());
        Finder.addMatcher(ForStmtMatcher, Handler.get());
        Finder.addMatcher(WhileStmtMatcher, Handler.get());
        Finder.addMatcher(DoWhileStmtMatcher, Handler.get());
        Finder.addMatcher(SwitchStmtMatcher, Handler.get());
    }

    // Override the ASTConsumer's HandleTranslationUnit method
    void HandleTranslationUnit(ASTContext &Context) override {
        // Run the match finder on the AST
        Finder.matchAST(Context);

        SourceManager &SM = Context.getSourceManager();
        FileID MainFileID = SM.getMainFileID();
        SourceLocation InsertLoc = SM.getLocForStartOfFile(MainFileID);
        Rewriter Rewrite(Context.getSourceManager(), Context.getLangOpts());
        Rewrite.InsertText(InsertLoc, bridDefineStmt, true, true);
        
    }

private:
    std::string bridInitStmt=R"(
        key_t shm_key = 0xdeadbeef;
        int shmid;
        char *data;
        int mode;
        /*  create the segment: */
        if ((shmid = shmget(shm_key, SHM_SIZE, 0644 | IPC_CREAT)) == -1) {
            perror("shmget");
            exit(1);
        }

        /* attach to the segment to get a pointer to it: */
        if ((data = shmat(shmid, NULL, 0)) == (void *)-1) {
            perror("shmat");
            exit(1);
        }
        SHADOWBUG_BRID = (int*)data;
        )", 
        bridDefineStmt=R"(
#define SHADOWBUG_BRID brid
int *SHADOWBUG_BRID = NULL;
        )";
    MatchFinder Finder;
    std::unique_ptr<StmtPrinter> Handler;
};

class MyCommonOptionsParser : public CommonOptionsParser {
public:
    MyCommonOptionsParser(int &argc, const char **argv, llvm::cl::OptionCategory &Category)
: CommonOptionsParser(argc, argv, Category, llvm::cl::ZeroOrMore) {}
};

class StmtAction : public ASTFrontendAction {
public:
    std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &Compiler, llvm::StringRef InFile) override {
    return std::make_unique<StmtConsumer>(&Compiler.getASTContext());
    }
};

int main(int argc, const char **argv) {
    srand (time(NULL));
    // Set up the command-line options
    llvm::cl::OptionCategory Category("Branch Search Tool Options");
    MyCommonOptionsParser OptionsParser(argc, argv, Category);
    ClangTool Tool(OptionsParser.getCompilations(), OptionsParser.getSourcePathList());

    // Run the tool with the custom frontend action
    int Result = Tool.run(newFrontendActionFactory<StmtAction>().get());

    return Result;
}