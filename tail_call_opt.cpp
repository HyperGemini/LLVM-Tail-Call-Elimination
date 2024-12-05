#include "llvm-c/Core.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/User.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/ValueMapper.h"

using namespace llvm;

namespace {
struct TailCallOptimization : FunctionPass {
  static char ID;
  TailCallOptimization() : FunctionPass(ID) {}

  static bool removeDeadBlocks(Function &F) {
    bool changed = false;

    for (BasicBlock &BB : make_early_inc_range(F)) {
      if (!pred_empty(&BB))
        continue;

      if (&F.getEntryBlock() == &BB)
        continue;

      for (BasicBlock *Succ : successors(&BB))
        Succ->removePredecessor(&BB);

      while (!BB.empty()) {
        Instruction &I = BB.back();
        I.replaceAllUsesWith(UndefValue::get(I.getType()));
        I.eraseFromParent();
      }
      BB.eraseFromParent();
      changed = true;
    }

    return changed;
  }

  bool checkConditions(Instruction *I, CallInst *CI) {
    // Provera uslova prema drugoj stavki (na sajtu gde su pass-ovi pise)
    if (!I->isAssociative() || !I->isCommutative())
      return false;

    // Sve asocijativne i komutativne operacije imaju 2 operanda
    if (I->getNumOperands() != 2)
      return false;

    // Tacno jedan od operanada bi trebalo da bude rezultat call instrukcije
    if ((I->getOperand(0) == CI && I->getOperand(1) == CI) ||
        (I->getOperand(0) != CI && I->getOperand(1) != CI))
      return false;

    return true;
  }

  bool isAccumulatorTailRecursive(Function &F) {
    bool allConditionsSatisfied = false;

    for (BasicBlock &BB : F) {
      // Provera da li je poslednja instrukcija u BB branch instrukcija
      BranchInst *BI = dyn_cast<BranchInst>(BB.getTerminator());
      if (!BI)
        continue;

      for (Instruction &I : BB) {
        if (CallInst *CI = dyn_cast<CallInst>(&I)) {
          // Da li postoji rekurzivni poziv
          Function *calledFunction = CI->getCalledFunction();
          if (calledFunction != &F)
            break;

          // Proveravamo svojstva za dalja izracunavanja gde
          // ulazi i akumulator
          Instruction *nextInst = I.getNextNonDebugInstruction();

          bool accumulatorConditions = checkConditions(nextInst, CI);
          if (!accumulatorConditions)
            break;

          allConditionsSatisfied = true;
        } else {
          continue;
        }
      }
    }

    return allConditionsSatisfied;
  }

  Value *getBaseCaseValue(Function &F) {
    Value *retVal = nullptr;

    for (BasicBlock &BB : F) {
      // Trazimo bezuslovni skok na poslednji BB jer tada znamo da smo dobili
      // BB koji sadrzi base case za nasu rekurziju
      BranchInst *BranchInstr = dyn_cast<BranchInst>(BB.getTerminator());

      if (!BranchInstr)
        continue;

      // Provera da li je skok bezuslovan i da li se skace na poslednji BB
      BasicBlock *lastBB = &F.back();
      if (BranchInstr->isUnconditional() &&
          BranchInstr->getSuccessor(0) == lastBB) {
        Instruction *prevInstruction =
            BranchInstr->getPrevNonDebugInstruction();
        StoreInst *storeInstr = dyn_cast<StoreInst>(prevInstruction);

        retVal = storeInstr->getOperand(0);

        return retVal;
      }
    }
  }

  // Vraca mi call instrukciju koja predstavlja rekurzivni poziv
  CallInst *getTailCall(Function &F) {
    for (BasicBlock &BB : F) {
      for (Instruction &I : BB) {
        if (CallInst *CI = dyn_cast<CallInst>(&I)) {
          // Da li postoji rekurzivni poziv
          Function *calledFunction = CI->getCalledFunction();

          if (calledFunction == &F)
            return CI;
        }
      }
    }
    return nullptr;
  }


  void eliminateTailCall(Function &F) {
    BasicBlock *entryBB = &(F.getEntryBlock());

    /* Kreiramo blok i razmenjujemo imena jer prijavljuje gresku za
     * novokreirani BB kad se ubaci: Entry block to function must not have
     * predecessors!
     */

    BasicBlock *newEntryBB =
        BasicBlock::Create(F.getContext(), "", &F, entryBB);

    newEntryBB->takeName(entryBB);
    entryBB->setName("check_for_condition");

    BranchInst *BI = BranchInst::Create(entryBB, newEntryBB);

    // Pomeramo instrukcije iznad
    for (BasicBlock::iterator OEBI = entryBB->begin(), E = entryBB->end(),
                              NEBI = newEntryBB->begin();
         OEBI != E;)
      if (AllocaInst *AI = dyn_cast<AllocaInst>(OEBI++))
        AI->moveBefore(&*NEBI);

    /* Za svaki argument funkcije, kreiramo po jedan phi node.
     * Za sada cemo postaviti da vrednosti budu argumenti fje */
    Instruction *insertPosition = &(entryBB->front());

    Function::arg_iterator AIBegin = F.arg_begin();
    Function::arg_iterator AIEnd = F.arg_end();

    PHINode *phiNodeTmp = nullptr;

    while (AIBegin != AIEnd) {
      phiNodeTmp = PHINode::Create(
          AIBegin->getType(), 2, "curr_" + AIBegin->getName(), insertPosition);
      AIBegin->replaceAllUsesWith(phiNodeTmp);
      /* Documentation: The addIncoming method is used to associate values
       * with the phi node from different basic blocks*/
      phiNodeTmp->addIncoming(&(*AIBegin), newEntryBB);

      AIBegin++;
    }

    // Uzmi vrednost koja se vraca kao base case
    Value *baseCaseVal = getBaseCaseValue(F);

    // Podesi odgovarajuci phi node
    PHINode *phiNodeAcc =
        PHINode::Create(F.getReturnType(), 2, "accumulator", insertPosition);
    phiNodeAcc->addIncoming(baseCaseVal, newEntryBB);

    CallInst *recCall = getTailCall(F);

    BasicBlock *recBBlock = recCall->getParent();
    Instruction *recBRInstr = recBBlock->getTerminator();
    recBRInstr->setSuccessor(0, entryBB); // 0 jer je obican branch

    Instruction *beforeRecCall = recCall->getPrevNonDebugInstruction(); // korak
    Instruction *afterRecCall =
        recCall->getNextNonDebugInstruction(); // Dodatna operacija

    phiNodeTmp->addIncoming(beforeRecCall, recBBlock);
    phiNodeAcc->addIncoming(afterRecCall, recBBlock);

    // Novi cmp za glavni blok
    Instruction *entryTerminator = entryBB->getTerminator();
    CmpInst *oldEntryCmp =
        dyn_cast<CmpInst>(entryTerminator->getPrevNonDebugInstruction());

    CmpInst *newEntryCmp = CmpInst::Create(
        oldEntryCmp->getOpcode(), oldEntryCmp->getPredicate(), phiNodeTmp,
        oldEntryCmp->getOperand(1), "rec_cmp", entryTerminator);

    oldEntryCmp->replaceAllUsesWith(newEntryCmp);
    oldEntryCmp->eraseFromParent();

    // Novi branch
    BranchInst *newEntryBranch =
        BranchInst::Create(&F.back(), recBBlock, newEntryCmp, entryBB);
    entryTerminator->eraseFromParent();

    // Return BB - popravljam tako da se vraca akumulator jer je tu vrednost
    // koja bi trebalo da bude krajnji rezultat
    BasicBlock *lastBB = &F.back();
    Instruction *retTerminator = lastBB->getTerminator();
    Instruction *beforeRetTerminator =
        retTerminator->getPrevNonDebugInstruction();

    beforeRetTerminator->replaceAllUsesWith(phiNodeAcc);
    beforeRetTerminator->eraseFromParent();

    // Radim na onom BB-u gde se nalazi rek poziv
    BasicBlock *loopBB =
        BasicBlock::Create(F.getContext(), "for_body", &F, &F.back());

    IRBuilder<> Builder(F.getContext());
    Builder.SetInsertPoint(loopBB);
    Builder.CreateBr(entryBB);

    BinaryOperator *newInstr_1 = BinaryOperator::Create(
        static_cast<llvm::Instruction::BinaryOps>(beforeRecCall->getOpcode()),
        phiNodeTmp, beforeRecCall->getOperand(1),
        "r_" + beforeRecCall->getName(), loopBB->getTerminator());

    BinaryOperator *newInstr_2 = BinaryOperator::Create(
        static_cast<llvm::Instruction::BinaryOps>(afterRecCall->getOpcode()),
        phiNodeAcc, phiNodeTmp, "r_" + afterRecCall->getName(),
        loopBB->getTerminator());

    beforeRecCall->replaceAllUsesWith(newInstr_1);
    afterRecCall->replaceAllUsesWith(newInstr_2);

    recBBlock->replaceAllUsesWith(loopBB);
    recBBlock->eraseFromParent();
  }

  bool runOnFunction(Function &F) override {
    if (isAccumulatorTailRecursive(F)) {
      errs() << "Funkcija " << F.getName() << " zadovoljava uslove!\n";
      eliminateTailCall(F);
      removeDeadBlocks(F);
    } else {
      errs() << "Funkcija " << F.getName() << " ne zadovoljava uslove!\n";
    }

    return true;
  }
};
} // namespace

char TailCallOptimization::ID = 0;
static RegisterPass<TailCallOptimization> X("tailCallOptimization",
                                            "Tail Call Optimization");


