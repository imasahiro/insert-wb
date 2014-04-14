#include "clang/AST/AST.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Tooling/Tooling.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Driver/Options.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "clang/Rewrite/Frontend/Rewriters.h"
#include "llvm/Support/CommandLine.h"

using namespace clang;
using namespace clang::driver;
using namespace clang::tooling;
using namespace llvm;

static cl::OptionCategory CheckCategory("insert-wb options");
static cl::extrahelp CommonHelp(CommonOptionsParser::HelpMessage);

const Type *GetType(Expr *E) {
  return E->getType().getTypePtr();
}

const Type *Desugar(const Type *Type) {
  if (const TypedefType *TTy = dyn_cast<TypedefType>(Type)) {
    return TTy->desugar().getTypePtr();
  }
  return Type;
}

const Type *Peel(const Type *Type) {
  if (const PointerType *PTy = dyn_cast<PointerType>(Type)) {
    return PTy->getPointeeType().getTypePtr();
  }
  return NULL;
}

bool IsRBasic(FieldDecl *Field) {
  if (Field->getNameAsString() == "flag") {
    //const Type *FTy = Field->getType().getTypePtr();
    //if (const Type *Ty = Desugar(FTy)) {
    //  //llvm::outs() << Field->getNameAsString() << "\n";
    //  //llvm::outs() << Ty << "\n";
    //}
  }
  return true;
}
bool IsVALUEType(QualType Type) {
  SplitQualType TypeName = Type.split();
  if (QualType::getAsString(TypeName) == "VALUE") {
    return true;
  }
  return false;
}

bool IsRubyObjectPtrType(const Type *Ty) {
  // Get typeof(*(struct x *))
  if (const Type *PTy = Peel(Ty)) {
    if (const ElaboratedType *ETy = dyn_cast<ElaboratedType>(PTy)) {
      Ty = ETy->desugar().getTypePtr();
      if (const RecordType *RTy = dyn_cast<RecordType>(Ty)) {
        RecordDecl *Decl = RTy->getDecl();
        if (Decl->field_empty()) {
          // struct does not have any field in current context.
          // e.g) "struct X;"
        }
        else if (IsRBasic(*Decl->field_begin())) {
          return true;
        }
        //for (RecordDecl::field_iterator I = Decl->field_begin(),
        //     E = Decl->field_end(); I != E; ++I) {
        //}
        // VALUE has
        return true;
      }
    }
  }
  return false;
}

bool IsSystemDefinedType(const Type *Ty, SourceManager &SM) {
  if (const Type *PTy = Peel(Ty)) {
    if (const ElaboratedType *ETy = dyn_cast<ElaboratedType>(PTy)) {
      Ty = ETy->desugar().getTypePtr();
      if (const RecordType *RTy = dyn_cast<RecordType>(Ty)) {
        RecordDecl *Decl = RTy->getDecl();
        SourceLocation Loc = Decl->getLocation();
        return SM.isInSystemHeader(Loc) || SM.isInExternCSystemHeader(Loc);
      }
    }
  }
  return false;
}

bool ExprNeedsWriteBarrier(BinaryOperator *OP, SourceManager &SM) {
  Expr *LHS = OP->getLHS();
  // check a->x or a[x]
  if (!isa<MemberExpr>(LHS) && !isa<ArraySubscriptExpr>(LHS)) {
    return false;
  }
  // Type of LHS must be pointer of RObject-like type
  const Type *LHSType = Desugar(GetType(OP->getLHS()));
  if (IsSystemDefinedType(LHSType, SM)) {
    return false;
  }
  if (!IsRubyObjectPtrType(LHSType) && !IsVALUEType(OP->getLHS()->getType())) {
    //if (!(isa<ArraySubscriptExpr>(LHS) && IsRubyObjectPtrType(Peel(LHSType)))) {
    //}
    return false;
  }
  const Type *RHSType = Desugar(GetType(OP->getRHS()));
  // Type of RHS must be pointer of RObject-like type
  if (IsSystemDefinedType(RHSType, SM)) {
    return false;
  }
 if (!IsRubyObjectPtrType(RHSType) && !IsVALUEType(OP->getLHS()->getType())) {
    return false;
  }
  return true;
}

static void dumpRange(Expr *E, ASTContext *Context)
{
  FullSourceLoc Begin = Context->getFullLoc(E->getLocStart());
  FullSourceLoc End   = Context->getFullLoc(E->getLocEnd());
  llvm::outs() << "("
      << Begin.getSpellingLineNumber() << ":"
      << Begin.getSpellingColumnNumber() << ", "
      << End.getSpellingLineNumber() << ":"
      << End.getSpellingColumnNumber() << ")";
}

class InsertWBVisitor : public RecursiveASTVisitor<InsertWBVisitor> {
 public:
  explicit InsertWBVisitor(ASTContext *Context, Rewriter &Rewriter)
      : Context(Context), Rewriter(Rewriter) {}

  bool VisitBinAssign(BinaryOperator *OP) {
    SourceManager &SM = Rewriter.getSourceMgr();
    if (!ExprNeedsWriteBarrier(OP, SM)) {
      return true;
    }
    //fprintf(stderr, "---------------\n");
    //OP->dump();
    //Expr *Expr = OP->getLHS();
    //if (MemberExpr *LHS = dyn_cast<MemberExpr>(Expr)) {
    //}
    //else if (ArraySubscriptExpr *LHS = dyn_cast<ArraySubscriptExpr>(Expr)) {
    //}

    //fprintf(stderr, "%d %d\n",
    //        SM.isMacroBodyExpansion(OP->getLocStart()),
    //        SM.isMacroBodyExpansion(OP->getLocEnd()));
    //dumpRange(OP, Context);
    FullSourceLoc FullLocation = Context->getFullLoc(OP->getLocStart());
    if (FullLocation.isValid()) {
      //fprintf(stderr, "write\n");
      Rewriter.InsertText(OP->getLocStart(), "__write_barrier();\n", true, true);
    }
    return true;
  }

 private:
  ASTContext *Context;
  Rewriter   &Rewriter;
};

class InsertWBConsumer : public clang::ASTConsumer {
 public:
  explicit InsertWBConsumer(ASTContext *Context, clang::CompilerInstance &Compiler)
      : Compiler(Compiler), Visitor(Context, Rewriter) {}

  virtual void HandleTranslationUnit(clang::ASTContext &Context) {
    Rewriter.setSourceMgr(Compiler.getSourceManager(), Compiler.getLangOpts());
    Visitor.TraverseDecl(Context.getTranslationUnitDecl());
    const RewriteBuffer *RewriteBuf =
        Rewriter.getRewriteBufferFor(Compiler.getSourceManager().getMainFileID());
    //fprintf(stderr, "ptr=%p\n", RewriteBuf);
    if (RewriteBuf) {
      llvm::outs() << std::string(RewriteBuf->begin(), RewriteBuf->end());
    }
  }
 private:
  clang::CompilerInstance &Compiler;
  Rewriter         Rewriter;
  InsertWBVisitor  Visitor;
};

class InsertWBAction : public clang::ASTFrontendAction {
 public:
  virtual clang::ASTConsumer *CreateASTConsumer(
      clang::CompilerInstance &Compiler, llvm::StringRef InFile) {
    return new InsertWBConsumer(&Compiler.getASTContext(), Compiler);
  }
};

static bool HasFile(const char *Path) {
  if (FILE *fp = fopen(Path, "r")) {
    fclose(fp);
    return true;
  }
  return false;
}

int main(int argc, char const* argv[]) {
  if (argc < 2) {
    fprintf(stderr, "usage : %s file_name -- \n", argv[0]);
    return EXIT_FAILURE;
  }
  if (!HasFile(argv[1])) {
    llvm::errs() << "input file does not exists\n";
    return EXIT_FAILURE;
  }
  CommonOptionsParser OptionsParser(argc, argv, CheckCategory);

  ClangTool Tool(OptionsParser.getCompilations(),
                 OptionsParser.getSourcePathList());
  return Tool.run(newFrontendActionFactory<InsertWBAction>());
}
