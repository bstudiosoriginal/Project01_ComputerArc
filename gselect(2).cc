
#include "cpu/pred/gselect.hh"

#include "base/bitfield.hh"
#include "base/intmath.hh"

namespace gem5
{

namespace branch_prediction
{

GSelectBP::GSelectBP(const GSelectBPParams &params)
    : BPredUnit(params),
      globalHistoryReg(params.numThreads, 0),
      globalHistoryBits(params.globalHistoryBits),
      choicePredictorSize(params.PredictorSize),
      choiceCtrBits(params.PHTCtrBits),
      globalPredictorSize(params.PredictorSize),
      globalCtrBits(params.PHTCtrBits),
      choiceCounters(choicePredictorSize, SatCounter8(choiceCtrBits)),
      takenCounters(globalPredictorSize, SatCounter8(globalCtrBits)),
      notTakenCounters(globalPredictorSize, SatCounter8(globalCtrBits))
{
    if (!isPowerOf2(choicePredictorSize))
        fatal("Invalid choice predictor size.\n");
    if (!isPowerOf2(globalPredictorSize))
        fatal("Invalid global history predictor size.\n");

    historyRegisterMask = mask(globalHistoryBits);
    choiceHistoryMask = choicePredictorSize - 1;
    globalHistoryMask = globalPredictorSize - 1;

    choiceThreshold = (1ULL << (choiceCtrBits - 1)) - 1;
    takenThreshold = (1ULL << (globalCtrBits - 1)) - 1;
    notTakenThreshold = (1ULL << (globalCtrBits - 1)) - 1;
}

/*
 * For an unconditional branch we set its history such that
 * everything is set to taken. I.e., its choice predictor
 * chooses the taken array and the taken array predicts taken.
 */
void
GSelectBP::uncondBranch(ThreadID tid, Addr pc, void * &bpHistory)
{
    GSelectBPHistory *history = new GSelectBPHistory;
    history->globalHistoryReg = globalHistoryReg[tid];
    history->takenUsed = true;
    history->takenPred = true;
    history->notTakenPred = true;
    history->finalPred = true;
    bpHistory = static_cast<void*>(history);
    updateGlobalHistReg(tid, true);
}

void
GSelectBP::squash(ThreadID tid, void *bpHistory)
{
    GSelectBPHistory *history = static_cast<GSelectBPHistory*>(bpHistory);
    globalHistoryReg[tid] = history->globalHistoryReg;

    delete history;
}

/*
 * Here we lookup the actual branch prediction. We use the PC to
 * identify the bias of a particular branch, which is based on the
 * prediction in the choice array. A hash of the global history
 * register and a branch's PC is used to index into both the taken
 * and not-taken predictors, which both present a prediction. The
 * choice array's prediction is used to select between the two
 * direction predictors for the final branch prediction.
 */
bool
GSelectBP::lookup(ThreadID tid, Addr branchAddr, void * &bpHistory)
{
    unsigned choiceHistoryIdx = ((branchAddr >> instShiftAmt)
                                & choiceHistoryMask);
    unsigned globalHistoryIdx = (nand((branchAddr >> instShiftAmt)
                                , globalHistoryReg[tid])
                                & globalHistoryMask);

    assert(choiceHistoryIdx < choicePredictorSize);
    assert(globalHistoryIdx < globalPredictorSize);

    bool choicePrediction = choiceCounters[choiceHistoryIdx]
                            > choiceThreshold;
    bool takenGHBPrediction = takenCounters[globalHistoryIdx]
                              > takenThreshold;
    bool notTakenGHBPrediction = notTakenCounters[globalHistoryIdx]
                                 > notTakenThreshold;
    bool finalPrediction;

    GSelectBPHistory *history = new GSelectBPHistory;
    history->globalHistoryReg = globalHistoryReg[tid];
    history->takenUsed = choicePrediction;
    history->takenPred = takenGHBPrediction;
    history->notTakenPred = notTakenGHBPrediction;

    if (choicePrediction) {
        finalPrediction = takenGHBPrediction;
    } else {
        finalPrediction = notTakenGHBPrediction;
    }

    history->finalPred = finalPrediction;
    bpHistory = static_cast<void*>(history);
    updateGlobalHistReg(tid, finalPrediction);

    return finalPrediction;
}

void
GSelectBP::btbUpdate(ThreadID tid, Addr branchAddr, void * &bpHistory)
{
    globalHistoryReg[tid] &= (historyRegisterMask & ~1ULL);
}

/* Only the selected direction predictor will be updated with the final
 * outcome; the status of the unselected one will not be altered. The choice
 * predictor is always updated with the branch outcome, except when the
 * choice is opposite to the branch outcome but the selected counter of
 * the direction predictors makes a correct final prediction.
 */
void
GSelectBP::update(ThreadID tid, Addr branchAddr, bool taken, void *bpHistory,
                 bool squashed, const StaticInstPtr & inst, Addr corrTarget)
{
    assert(bpHistory);

    GSelectBPHistory *history = static_cast<GSelectBPHistory*>(bpHistory);

    // We do not update the counters speculatively on a squash.
    // We just restore the global history register.
    if (squashed) {
        globalHistoryReg[tid] = (history->globalHistoryReg << 1) | taken;
        return;
    }

    unsigned choiceHistoryIdx = ((branchAddr >> instShiftAmt)
                                & choiceHistoryMask);
    unsigned globalHistoryIdx = (nand((branchAddr >> instShiftAmt)
                                , history->globalHistoryReg)
                                & globalHistoryMask);

    assert(choiceHistoryIdx < choicePredictorSize);
    assert(globalHistoryIdx < globalPredictorSize);

    if (history->takenUsed) {
        // if the taken array's prediction was used, update it
        if (taken) {
            takenCounters[globalHistoryIdx]++;
        } else {
            takenCounters[globalHistoryIdx]--;
        }
    } else {
        // if the not-taken array's prediction was used, update it
        if (taken) {
            notTakenCounters[globalHistoryIdx]++;
        } else {
            notTakenCounters[globalHistoryIdx]--;
        }
    }

    if (history->finalPred == taken) {
       /* If the final prediction matches the actual branch's
        * outcome and the choice predictor matches the final
        * outcome, we update the choice predictor, otherwise it
        * is not updated. While the designers of the bi-mode
        * predictor don't explicity say why this is done, one
        * can infer that it is to preserve the choice predictor's
        * bias with respect to the branch being predicted; afterall,
        * the whole point of the bi-mode predictor is to identify the
        * atypical case when a branch deviates from its bias.
        */
        if (history->finalPred == history->takenUsed) {
            if (taken) {
                choiceCounters[choiceHistoryIdx]++;
            } else {
                choiceCounters[choiceHistoryIdx]--;
            }
        }
    } else {
        // always update the choice predictor on an incorrect prediction
        if (taken) {
            choiceCounters[choiceHistoryIdx]++;
        } else {
            choiceCounters[choiceHistoryIdx]--;
        }
    }

    delete history;
}

void
GSelectBP::updateGlobalHistReg(ThreadID tid, bool taken)
{
    globalHistoryReg[tid] = taken ? (globalHistoryReg[tid] << 1) | 1 :
                               (globalHistoryReg[tid] << 1);
    globalHistoryReg[tid] &= historyRegisterMask;
}

bool
GSelectBP::nand(bool a, bool b){
  return !(a && b);
}


} // namespace branch_prediction
} // namespace gem5
