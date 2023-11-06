#ifndef __CPU_PRED_GSELECT_HH__
#define __CPU_PRED_GSELECT_HH__

/* @file
 * Implementation of a GSelect with NAND branch predictor
 */

#include "base/sat_counter.hh"
#include "cpu/pred/bpred_unit.hh"
#include "params/GSelectBP.hh"

namespace gem5
{

namespace branch_prediction
{


class GSelectBP : public BPredUnit
{
  public:
    GSelectBP(const GSelectBPParams &params);
    void uncondBranch(ThreadID tid, Addr pc, void * &bp_history);
    void squash(ThreadID tid, void *bp_history);
    bool lookup(ThreadID tid, Addr branch_addr, void * &bp_history);
    void btbUpdate(ThreadID tid, Addr branch_addr, void * &bp_history);
    void update(ThreadID tid, Addr branch_addr, bool taken, void *bp_history,
                bool squashed, const StaticInstPtr & inst, Addr corrTarget);

  private:
    void updateGlobalHistReg(ThreadID tid, bool taken);

    struct GSelectBPHistory
    {
        unsigned globalHistoryReg;
        // was the taken array's prediction used?
        // true: takenPred used
        // false: notPred used
        bool takenUsed;
        // prediction of the taken array
        // true: predict taken
        // false: predict not-taken
        bool takenPred;
        // prediction of the not-taken array
        // true: predict taken
        // false: predict not-taken
        bool notTakenPred;
        // the final taken/not-taken prediction
        // true: predict taken
        // false: predict not-taken
        bool finalPred;
    };

    std::vector<unsigned> globalHistoryReg;
    unsigned globalHistoryBits;
    unsigned historyRegisterMask;

    unsigned choicePredictorSize;
    unsigned choiceCtrBits;
    unsigned choiceHistoryMask;
    unsigned globalPredictorSize;
    unsigned globalCtrBits;
    unsigned globalHistoryMask;

    // choice predictors
    std::vector<SatCounter8> choiceCounters;
    // taken direction predictors
    std::vector<SatCounter8> takenCounters;
    // not-taken direction predictors
    std::vector<SatCounter8> notTakenCounters;
c
    unsigned choiceThreshold;
    unsigned takenThreshold;
    unsigned notTakenThreshold;

    bool nand(bool a, bool b);
};

} // namespace branch_prediction
} // namespace gem5

#endif // __CPU_PRED_BI_MODE_PRED_HH__
