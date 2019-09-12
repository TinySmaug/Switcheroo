#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <vector>
#include <system_error>
#include <iostream>
#include <algorithm>
#include <cctype>
#include <string>

//clang::SyntaxOnlyAction
#include "clang/Frontend/FrontendActions.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
//llvm::cl::extrahelp
#include "llvm/Support/CommandLine.h"

#include "llvm/Support/Host.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/FileSystem.h"

#include "clang/Basic/DiagnosticOptions.h"
#include "clang/Frontend/TextDiagnosticPrinter.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Basic/TargetOptions.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Lex/Lexer.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/Parse/ParseAST.h"
#include "clang/Rewrite/Frontend/Rewriters.h"
#include "clang/Rewrite/Core/Rewriter.h"

using namespace clang;
using namespace clang::tooling;
using namespace llvm;

static llvm::cl::OptionCategory MyToolCategory("my-tool options");
static cl::extrahelp CommonHelp(CommonOptionsParser::HelpMessage);
static cl::extrahelp MoreHelp("\n...\n");

bool hasBreak(std::string &str){
	auto n = str.find("break;");
	return !(n == std::string::npos);
}

void eraseBreak(std::string &str){
	auto n = str.find("break;");
	if(n != std::string::npos)
		str.erase(n, 6);
}
/*
void indentCaseStmt(std::string &str){
	auto strStart = find_if(str.begin(), str.end(), [](char c) { return isalnum(c) || (c == '/'); });
	str.erase(str.begin(), strStart);
	auto firstStmtEnd = find_if(str.begin(), str.end(), [](char c) { return (c == ';'); });
	auto secondStmtStart = find_if(firstStmtEnd, str.end(), [](char c) { return isalnum(c); });
	if(secondStmtStart != str.end()){
		std::string spaces(firstStmtEnd+1, secondStmtStart);
		spaces.erase(std::find(spaces.begin(), spaces.end(), '\n'));
		str.insert(0,spaces);
	}
}
*/

// RecursiveASTVisitor that alows us to specify which AST nodes
// we're interested in by overriding relevant methods
class MyRecursiveASTVisitor: public RecursiveASTVisitor<MyRecursiveASTVisitor>{
	public:
		MyRecursiveASTVisitor(Rewriter &r, CompilerInstance &ci)
		: _rewriter(r), _compiler(ci)
		{

		}
		bool VisitStmt(Stmt *s);
	private:
		Rewriter &_rewriter;
		CompilerInstance &_compiler;
};

bool MyRecursiveASTVisitor::VisitStmt(Stmt *s){
	if(!isa<SwitchStmt>(s)){
		return true; // returning false aborts the traversal
	}

	SwitchStmt *switchStatement = cast<SwitchStmt>(s);
	SourceRange sourceRange;

	//making a string from the switch condition
	Expr *condExpr = switchStatement->getCond();
	bool invalid;
	std::string condition = Lexer::getSourceText(CharSourceRange(condExpr->getSourceRange(), true),
							_compiler.getSourceManager(), _compiler.getLangOpts(), &invalid).str();
	bool isVariable = find_if(condition.begin(), condition.end(),
						[](char c) { return !(isalnum(c)); }) == condition.end();
	if(!isVariable){
		condition.insert(0, 1, '(').append(")");
	}
	condition.append(" == ");

	SwitchCase *caseList = switchStatement->getSwitchCaseList();
	//checking if the switch has a default case
	bool hasDefault = false;
	sourceRange.setBegin(caseList->getKeywordLoc());
	sourceRange.setEnd(caseList->getKeywordLoc().getLocWithOffset(6));
	std::string lastCase = Lexer::getSourceText(CharSourceRange(sourceRange, true),
						   _compiler.getSourceManager(), _compiler.getLangOpts(), &invalid).str();
	if(lastCase.compare("default") == 0){
		hasDefault = true;
	}

	//creating a vector of statement strings
	//Note: starts from bottom to top case
	Stmt *body = switchStatement->getBody();
	sourceRange.setBegin(caseList->getColonLoc().getLocWithOffset(1));
	sourceRange.setEnd(body->getLocEnd().getLocWithOffset(-1));
	std::vector<std::string> caseStatements;

	caseStatements.push_back(Lexer::getSourceText(CharSourceRange(sourceRange, true),
							 _compiler.getSourceManager(), _compiler.getLangOpts(), &invalid).str());
	while(caseList->getNextSwitchCase() != nullptr){
		sourceRange.setBegin(caseList->getNextSwitchCase()->getColonLoc().getLocWithOffset(1));
		sourceRange.setEnd(caseList->getKeywordLoc().getLocWithOffset(-1));
		caseStatements.emplace_back(Lexer::getSourceText(CharSourceRange(sourceRange, true),
									_compiler.getSourceManager(), _compiler.getLangOpts(), &invalid).str());
		caseList = caseList->getNextSwitchCase();
	}

	//Note: for fall throughs we need to move from right to left
	//to respect the case order
	int i;
	for(i = caseStatements.size()-1; i>=0; i--){
		if(!hasBreak(caseStatements[i])){
			for(int j=i-1; j>=0; j--){
				if(hasBreak(caseStatements[j])){
					caseStatements[i].append(caseStatements[j]);
					eraseBreak(caseStatements[i]);
					break;
				}
				caseStatements[i].append(caseStatements[j]);
			}
		}
		else
			eraseBreak(caseStatements[i]);
	}

	//adding if-elseif-else
	//reseting back after making the vector
	caseList = switchStatement->getSwitchCaseList();
	//Note: we are moving through the vector from
	//left to right when falling through
	bool defaultCaseRewritten = false;
	int numberOfStmts=caseStatements.size();
	i=0;
	SourceLocation nextCaseLoc = body->getLocEnd();

	while(caseList != nullptr && i<numberOfStmts){
		if(hasDefault == true && defaultCaseRewritten == false){
			_rewriter.InsertTextBefore(caseList->getKeywordLoc(), "}else{\n");
			_rewriter.ReplaceText(caseList->getKeywordLoc(), 7, "");
			_rewriter.ReplaceText(caseList->getColonLoc(), 1, "");

			sourceRange.setBegin(caseList->getColonLoc().getLocWithOffset(1));
			sourceRange.setEnd(nextCaseLoc.getLocWithOffset(-1));
			//indentCaseStmt(caseStatements[i]);
			_rewriter.ReplaceText(sourceRange, caseStatements[i]);

			defaultCaseRewritten = true;
			nextCaseLoc = caseList->getKeywordLoc();
			caseList = caseList->getNextSwitchCase();
			i++;
			continue;
		}
		if(caseList->getNextSwitchCase() == nullptr){
			_rewriter.InsertTextBefore(caseList->getKeywordLoc(), std::string{"if("}.append(condition));
		}else{
			_rewriter.InsertTextBefore(caseList->getKeywordLoc(), std::string{"}else if("}.append(condition));
		}
		_rewriter.ReplaceText(caseList->getKeywordLoc(), 4, "");
		_rewriter.ReplaceText(caseList->getColonLoc(), 1, "){\n");

		//indentCaseStmt(caseStatements[i]);
		sourceRange.setBegin(caseList->getColonLoc().getLocWithOffset(1));
		sourceRange.setEnd(nextCaseLoc.getLocWithOffset(-1));
		_rewriter.ReplaceText(sourceRange, caseStatements[i]);

		nextCaseLoc = caseList->getKeywordLoc();
		caseList = caseList->getNextSwitchCase();
		i++;
	}
	//removing space from switch to the first case
	sourceRange.setBegin(switchStatement->getSwitchLoc());
	sourceRange.setEnd(nextCaseLoc.getLocWithOffset(-1));
	_rewriter.RemoveText(sourceRange);

	return true; // returning false aborts the traversal
}


// ASTConsumer interface for reading an AST produced
// by the Clang parser
class MyASTConsumer : public ASTConsumer{
	public:
		MyASTConsumer(Rewriter &Rewrite, CompilerInstance &Compiler)
		: _visitor(Rewrite, Compiler)
		{

		}
		virtual bool HandleTopLevelDecl(DeclGroupRef d);
	private:
		MyRecursiveASTVisitor _visitor;
};

// Override the method that gets called for each parsed top-level
// declaration.
bool MyASTConsumer::HandleTopLevelDecl(DeclGroupRef drg){
	for (DeclGroupRef::iterator b = drg.begin(), e = drg.end(); b != e; ++b){
		// Traverse the declaration using our AST visitor.
		_visitor.TraverseDecl(*b);
	}
	return true; //to keep going
}


int main(int argc, const char **argv) {
	if (argc < 2){
		llvm::errs() << "Usage: switcheroo <options> <filename>\n";
		return 1;
    }

	//syntax checker
	CommonOptionsParser OptionsParser(argc, argv, MyToolCategory);
	ClangTool Tool(OptionsParser.getCompilations(),
				   OptionsParser.getSourcePathList());
	int syntaxChecker = Tool.run(newFrontendActionFactory<SyntaxOnlyAction>().get());
	if(syntaxChecker != 0){
		return syntaxChecker;
	}

	//Input file checker
	struct stat sb;
	// Get filename
	std::string fileName(argv[argc - 1]);
	// Make sure it exists
	if (stat(fileName.c_str(), &sb) == -1){
		perror(fileName.c_str());
		exit(EXIT_FAILURE);
	}

	// CompilerInstance will hold the instance of the Clang compiler,
	// managing the various objects needed to run the compiler.
	CompilerInstance compInst;
	compInst.createDiagnostics();

	LangOptions &lo = compInst.getLangOpts();
	lo.CPlusPlus = 1;

	// Initialize target info with the default triple for our platform.
	auto to = std::make_shared<clang::TargetOptions>();
	to->Triple = llvm::sys::getDefaultTargetTriple();
	TargetInfo *ti = TargetInfo::CreateTargetInfo(compInst.getDiagnostics(), to);
	compInst.setTarget(ti);

	HeaderSearchOptions &headerSearchOptions = compInst.getHeaderSearchOpts();

	// <Warning!!> -- Platform Specific Code lives here
	// To see what include paths need to be here, try
	// clang -v -c test.c
	// or clang++ for C++ paths as used below:
	headerSearchOptions.AddPath("/usr/include/c++/4.9",
								clang::frontend::Angled,
								false, false);
	headerSearchOptions.AddPath("/usr/lib/gcc/x86_64-linux-gnu/4.9",
								clang::frontend::Angled,
								false, false);
	headerSearchOptions.AddPath("/usr/include/c++/4.6/backward",
								clang::frontend::Angled,
								false, false);
	headerSearchOptions.AddPath("/usr/local/include",
								clang::frontend::Angled,
								false, false);
	headerSearchOptions.AddPath("/usr/local/bin/../lib/clang/3.7.1/include",
								clang::frontend::Angled,
								false, false);
	headerSearchOptions.AddPath("/usr/include/x86_64-linux-gnu",
								clang::frontend::Angled,
								false, false);
	headerSearchOptions.AddPath("/usr/include",
								clang::frontend::Angled,
								false, false);
	// </Warning!!> -- End of Platform Specific Code

	compInst.createFileManager();
	compInst.createSourceManager(compInst.getFileManager());

	compInst.createPreprocessor(clang::TU_Complete);
	//compiler.getPreprocessorOpts().UsePredefines = false;

	compInst.createASTContext();

	// Initialize rewriter
	Rewriter rewrite;
	rewrite.setSourceMgr(compInst.getSourceManager(), compInst.getLangOpts());

	// Set the main file handled by the source manager to the input file.
	const FileEntry *input = compInst.getFileManager().getFile(fileName);
	SourceManager &sourceMgr = compInst.getSourceManager();
	sourceMgr.setMainFileID(sourceMgr.createFileID(input, SourceLocation(), SrcMgr::C_User));
	compInst.getDiagnosticClient().BeginSourceFile(compInst.getLangOpts(),
												   &compInst.getPreprocessor());

	// Create an AST consumer instance which is going to get called by
	// ParseAST.
	MyASTConsumer astConsumer(rewrite, compInst);

	// Convert <file>.c to <file_out>.c
	std::string outName (fileName);
	size_t ext = outName.rfind(".");
	if (ext == std::string::npos){
		ext = outName.length();
	}
	outName.insert(ext, "_out");

	llvm::errs() << "Output to: " << outName << "\n";
	std::error_code OutErrorInfo;
	std::error_code ok;
	llvm::raw_fd_ostream outFile(llvm::StringRef(outName), OutErrorInfo,
								 llvm::sys::fs::F_None);

	if (OutErrorInfo == ok){
		// Parse the AST
		ParseAST(compInst.getPreprocessor(), &astConsumer, compInst.getASTContext());
		compInst.getDiagnosticClient().EndSourceFile();

		// Now output rewritten source code
		const RewriteBuffer *rewriteBuf = rewrite.getRewriteBufferFor(sourceMgr.getMainFileID());
		outFile << std::string(rewriteBuf->begin(), rewriteBuf->end());
	}
	else{
		llvm::errs() << "Cannot open " << outName << " for writing\n";
	}

	outFile.close();

	return 0;
}
