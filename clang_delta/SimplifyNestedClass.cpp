//===----------------------------------------------------------------------===//
//
// Copyright (c) 2012 The University of Utah
// All rights reserved.
//
// This file is distributed under the University of Illinois Open Source
// License.  See the file COPYING for details.
//
//===----------------------------------------------------------------------===//

#if HAVE_CONFIG_H
#  include <config.h>
#endif

#include "SimplifyNestedClass.h"

#include "clang/Basic/SourceManager.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/ASTContext.h"

#include "TransformationManager.h"

using namespace clang;
using namespace llvm;

static const char *DescriptionMsg = 
"This pass tries to simplify nested classes by replacing the \
outer class with the inner class, if \n\
  * the outer class doesn't have any base class, and \n\
  * the outer class has only one inner class definition, and \n\
  * the outer class does not have any described template, and \n\
  * the outer class does not have any other declarations except \
the inner class \n\
";

static RegisterTransformation<SimplifyNestedClass>
         Trans("simplify-nested-class", DescriptionMsg);

class SimplifyNestedClassVisitor : public 
  RecursiveASTVisitor<SimplifyNestedClassVisitor> {

public:
  explicit SimplifyNestedClassVisitor(
             SimplifyNestedClass *Instance)
    : ConsumerInstance(Instance)
  { }

  bool VisitCXXRecordDecl(CXXRecordDecl *CXXRD);

private:
  SimplifyNestedClass *ConsumerInstance;
};

bool SimplifyNestedClassVisitor::VisitCXXRecordDecl(
       CXXRecordDecl *CXXRD)
{
  if (ConsumerInstance->isSpecialRecordDecl(CXXRD) || !CXXRD->hasDefinition())
    return true;
  ConsumerInstance->handleOneCXXRecordDecl(CXXRD->getDefinition());
  return true;
}

class SimplifyNestedClassRewriteVisitor : public
  RecursiveASTVisitor<SimplifyNestedClassRewriteVisitor> {
public:
  explicit SimplifyNestedClassRewriteVisitor(
             SimplifyNestedClass *Instance)
    : ConsumerInstance(Instance)
  { }

  bool VisitRecordTypeLoc(RecordTypeLoc TLoc);

private:
  SimplifyNestedClass *ConsumerInstance;
};

bool SimplifyNestedClassRewriteVisitor::VisitRecordTypeLoc(RecordTypeLoc TLoc)
{
  const CXXRecordDecl *RD = dyn_cast<CXXRecordDecl>(TLoc.getDecl());
  if (!RD || (RD->getCanonicalDecl() != 
              ConsumerInstance->TheBaseCXXRD->getCanonicalDecl()))
    return true;

  if (ConsumerInstance->isBeforeColonColon(TLoc)) {
    SourceLocation EndLoc = 
      ConsumerInstance->RewriteHelper->getLocationAfter(
        TLoc.getEndLoc(), ':');
    ConsumerInstance->TheRewriter.RemoveText(
                        SourceRange(TLoc.getBeginLoc(), EndLoc));
  }
  else {
    ConsumerInstance->RewriteHelper->replaceRecordType(TLoc,
      ConsumerInstance->TheBaseCXXRD->getNameAsString() + " ");
  }
  return true;
}

void SimplifyNestedClass::Initialize(ASTContext &context) 
{
  Transformation::Initialize(context);
  CollectionVisitor = new SimplifyNestedClassVisitor(this);
  RewriteVisitor = new SimplifyNestedClassRewriteVisitor(this);
}

void SimplifyNestedClass::HandleTranslationUnit(ASTContext &Ctx)
{
  if (TransformationManager::isCLangOpt()) {
    ValidInstanceNum = 0;
  }
  else {
    CollectionVisitor->TraverseDecl(Ctx.getTranslationUnitDecl());
  }

  if (QueryInstanceOnly)
    return;

  if (TransformationCounter > ValidInstanceNum) {
    TransError = TransMaxInstanceError;
    return;
  }

  Ctx.getDiagnostics().setSuppressAllDiagnostics(false);
  TransAssert(RewriteVisitor && "NULL RewriteVisitor!");
  RewriteVisitor->TraverseDecl(Ctx.getTranslationUnitDecl());
  removeOuterClass();

  if (Ctx.getDiagnostics().hasErrorOccurred() ||
      Ctx.getDiagnostics().hasFatalErrorOccurred())
    TransError = TransInternalError;
}

void SimplifyNestedClass::removeOuterClass()
{
  TransAssert(TheBaseCXXRD && "NULL Base CXXRD!");
  SourceLocation LocStart = TheBaseCXXRD->getLocStart();
  SourceLocation LocEnd = 
    RewriteHelper->getLocationUntil(LocStart, '{');
  TransAssert(LocEnd.isValid() && "Invalid Location!");
  TheRewriter.RemoveText(SourceRange(LocStart, LocEnd));

  LocStart = TheBaseCXXRD->getRBraceLoc();
  LocEnd = RewriteHelper->getLocationUntil(LocStart, ';');
  if (LocStart.isInvalid() || LocEnd.isInvalid())
    return;

  TheRewriter.RemoveText(SourceRange(LocStart, LocEnd));
}

void SimplifyNestedClass::handleOneCXXRecordDecl(const CXXRecordDecl *CXXRD)
{
  TransAssert(CXXRD && "NULL CXXRD!");
  TransAssert(CXXRD->isThisDeclarationADefinition() &&  "Not a definition!");
  if (CXXRD->getDescribedClassTemplate() || CXXRD->getNumBases())
    return;

  bool HasClassDef = false;
  const DeclContext *Ctx = dyn_cast<DeclContext>(CXXRD);
  for (DeclContext::decl_iterator I = Ctx->decls_begin(),
       E = Ctx->decls_end(); I != E; ++I) {
    if ((*I)->isImplicit())
      continue;
    if (dyn_cast<CXXRecordDecl>(*I) || dyn_cast<ClassTemplateDecl>(*I)) {
      if (HasClassDef)
        return;
      HasClassDef = true;
    }
  }
  if (!HasClassDef)
    return;

  ValidInstanceNum++;
  if (ValidInstanceNum == TransformationCounter) {
    TheBaseCXXRD = CXXRD;
  }
}

SimplifyNestedClass::~SimplifyNestedClass(void)
{
  delete CollectionVisitor;
  delete RewriteVisitor;
}
