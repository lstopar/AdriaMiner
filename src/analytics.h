#ifndef ANALYTICS_H_
#define ANALYTICS_H_

#include <base.h>
#include <net.h>
#include <mine.h>
#include <thread.h>
#include <utils.h>

namespace TAdriaAnalytics {

using namespace TSignalProc;
using namespace TAdriaUtils;

//////////////////////////////////////////////////////////////
// Support
class TSupport {
public:
	static double Supp(const TIntVV& Mat, const TIntV& ItemIdxV);
};

//////////////////////////////////////////////////////////////
// Confidence
class TConfidence {
public:
	static double Conf(const TIntVV& EventMat, const TIntVV& ObsMat,
			const TIntV& CauseIdxV, const TIntV& EffectIdxV);
};

//////////////////////////////////////////////////////////////
// Modified APRIORI algorithm
template <class TSupp, class TConf>
class TApriori {
public:
	static void Run(const TIntVV& EventMat, const TIntVV ObsMat, const double& SuppThreshold,
			const double& ConfThreshold, TVec<TPair<TIntV,TInt>>& RuleV,
			const int& MaxItems = TInt::Mx, const PNotify& Notify = TStdNotify::New());

private:
	static void GenFreqItems(const TIntVV& EventMat, const double& SuppThreshold,
			const int& MaxItems, TVec<TIntV>& FreqItems, const PNotify& Notify);
	static void GenCandV(const TVec<TIntV>& CurrFreqItemV, TVec<TIntV>& NextFreqItemV);
	static bool PrefixEq(const TIntV& Vec1, const TIntV& Vec2, const int& PrefixLen);
};

//////////////////////////////////////////////////////////////
// Linear regression wrapper
class TLinRegWrapper {
private:
	static const double RegFact;
	static const double ForgetFact;
	static const int FeatDim;

	TStr DbPath;
	TRecLinReg LinReg;

	TCriticalSection DataSection;

	PNotify Notify;

public:
	TLinRegWrapper(const TStr& DbPath, const PNotify& Notify);

	double Predict(const TFltV& FeatV);
	void Learn(const TFltV& FeatV, const TFlt& Val);
	void Learn(const TVec<TFltV>& InstV, const TFltV& ValV);

	const TRecLinReg& GetRegModel() { return LinReg; }

private:
	// loads the structures from disk
	void LoadStructs();
	// saves the structures to disk
	void SaveStructs();
	// initializes a default model
	void InitDefaultModel();
	// for logging the instances
	void LogInstVValV(const TVec<TFltV>& InstV, const TFltV& ValV);
};

typedef TVVec<TUInt64FltPr> TStatMat;
typedef TPair<TStatMat, TStatMat> TStatMatPr;
typedef TIntV TItemSet;
typedef TPair<TItemSet,TInt> TRule;
typedef TVec<TRule> TRuleV;

//////////////////////////////////////////////////////////////
// Online rule generator
class TOnlineRuleGenerator {
private:
	const static double LUM_LOW, LUM_HIGH;
	const static double TEMP_LOW, TEMP_HIGH;

	const static int MX_ITEMSET_SIZE;

	const static double THRESHOLD;
	const static double FORGET_FACT;

	static TIntV ActCanIdV;
	static TIntV LumCanIdV;
	static TIntV TempCanIdV;

	static TIntV IdxCanIdV;					// a vector with CAN IDs at the appropriate indexes

	static int ActMatDim;
	static int ObsMatRows, ObsMatCols;

	static int NActAttrs, NObsAttrs;

	static TIntIntVH ActRowIdxAttrIdxVH;		// hash table mapping an index in the actuators matrix to the corresponding actuator item set
	static TIntIntVH ObsRowIdxAttrIdxVH;		// hash table mapping an index in the observations matrix to the corresponding observation item set
	static TIntPrV ActAttrIdxRowIdxPrV;			// a vector mapping an actuator index to an index in the actuators matrix
	static TIntPrV ObsAttrIdxRowIdxPrV;			// a vector mapping an observation index to an index in the observations matrix

	static bool Initialized;

	const TStr DbPath;

	// a pair which holds the statistics matrix for the actuators and the statistics matrix for
	// observations vs actuators
	TStatMatPr StatMatPr;

	TCriticalSection MatSection;

	PNotify Notify;

public:
	TOnlineRuleGenerator(const TStr& DbPath, const PNotify& Notify);

	void Update(const TFltV& StateV);		// TODO unlock ???
	void GetAllRules(TRuleV& RuleV) const;	// TODO lock ???

private:
	// returns the conditional probability P(A_i | A_j)
	double GetProb(const TStatMat& StatMat, const int& i, const int& j) const;

	void ExtractState(const TFltV& TableV, TFltV& ActStateV, TFltV& LumStateV, TFltV& TempStateV) const;
	void CalcAttrValV(const TFltV& ActStateV, const TFltV& LumStateV, const TFltV& TempStateV, TIntV& ActValV, TIntV& ObsValV) const;

	void PersistStatMat();
	void LoadStatMat();
	void CreateStatMat();

	// returns true if all the items in the item set are 1
	static bool IsActive(const TIntV& ItemSet, const TIntV& AttrValV);

	static int CalcDim(const int& NAttrs);
	static void GenItemSetV(const int& MxIdx, const int& MxItems, TVec<TIntV>& ItemSetV, const int& CurrIdx = 0);
	static int ActIdxToCanId(const int& ActIdx);
	static int ObsIdxToCanId(const int& ObsIdx);
	static bool InitCanV();

	static long Fac(const int& N);
	static int Nck(const int& N, const int& K);
};

//===========================================================================================
// IMPLEMENTATION
//===========================================================================================

template <class TSupp, class TConf>
void TApriori<TSupp,TConf>::Run(const TIntVV& EventMat, const TIntVV ObsMat,
		const double& SuppThreshold, const double& ConfThreshold,
		TVec<TPair<TIntV,TInt>>& ResRuleV, const int& MaxItems,
		const PNotify& Notify) {

	Notify->OnNotifyFmt(TNotifyType::ntInfo, "Running Apriori algorithm on %d instances...", ObsMat.GetRows());

	if (ObsMat.GetRows() != EventMat.GetRows())
		throw TExcept::New("Different number of instances for events and observations!", "TApriori::FindRules");
	if (EventMat.GetRows() == 0) {
		Notify->OnNotify(TNotifyType::ntInfo, "No instances available, returning...");
		return;
	}

	// generate frequent item sets
	TVec<TIntV> ItemSetV;
	GenFreqItems(EventMat, SuppThreshold, MaxItems, ItemSetV, Notify);

	Notify->OnNotify(TNotifyType::ntInfo, "Generating rules...");

	TVec<TPair<TIntV,TInt>> TempRuleV;
	TVec<TPair<TFlt,TPair<TIntV,TInt>>> PrintTempRuleV;	// TODO just for debugging

	// generate rules
	for (int ItemSetIdx = 0; ItemSetIdx < ItemSetV.Len(); ItemSetIdx++) {
		TIntV& ItemSet = ItemSetV[ItemSetIdx];

		// generate all the possible rules from this item set
		const int NItems = ItemSet.Len();
		for (int ItemIdx = 0; ItemIdx < NItems; ItemIdx++) {
			// create the causes vector and the effect
			const int EffectIdx = ItemSet[ItemIdx];

			if (EffectIdx >= EventMat.GetYDim()) { continue; }	// effects can only be events

			TIntV CauseIdxV(NItems-1,0);
			TIntV EffectV(1,0);

			for (int i = 0; i < NItems; i++) {
				if (ItemSet[i] != EffectIdx) {
					CauseIdxV.Add(ItemSet[i]);
				}
			}

			EffectV.Add(EffectIdx);
			double Conf = TConf::Conf(EventMat, ObsMat, CauseIdxV, EffectV);
			PrintTempRuleV.Add(TPair<TFlt,TPair<TIntV,TInt>>(Conf, TPair<TIntV,TInt>(CauseIdxV, EffectIdx)));

			// check if this rule has enough confidence
			if (TConf::Conf(EventMat, ObsMat, CauseIdxV, EffectV) >= ConfThreshold) {
				TempRuleV.Add(TPair<TIntV,TInt>(CauseIdxV, EffectIdx));
			}
		}
	}

	TUtils::PrintRuleCandV(PrintTempRuleV, Notify);

	TVec<TIntV> ObsItemSetV;
	GenFreqItems(ObsMat, SuppThreshold, MaxItems, ObsItemSetV, Notify);
	// transform the indexes
	int Offset = EventMat.GetCols();
	for (int i = 0; i < ObsItemSetV.Len(); i++) {
		for (int j = 0; j < ObsItemSetV[i].Len(); j++) {
			ObsItemSetV[i][j] += Offset;
		}
	}

	// append observations to rules
	for (int RuleIdx = 0; RuleIdx < TempRuleV.Len(); RuleIdx++) {
		TPair<TIntV,TInt>& Rule = TempRuleV[RuleIdx];

		const TIntV& EventItemSet = Rule.Val1;

		bool RuleAdded = false;
		for (int ObsSetIdx = 0; ObsSetIdx < ObsItemSetV.Len(); ObsSetIdx++) {
			const TIntV& ObsItemSet = ObsItemSetV[ObsSetIdx];

			TIntV EventFullSet(EventItemSet.Len()+1,0);
			EventFullSet.AddV(EventItemSet);
			EventFullSet.Add(Rule.Val2);

			if (TConf::Conf(EventMat, ObsMat, EventFullSet, ObsItemSet) >= ConfThreshold) {
				TIntV JoinedItemSet(EventItemSet.Len() + ObsItemSet.Len(),0);
				JoinedItemSet.AddV(EventItemSet);
				JoinedItemSet.AddV(ObsItemSet);

				ResRuleV.Add(TPair<TIntV,TInt>(JoinedItemSet, Rule.Val2));
				RuleAdded = true;
			}
		}

		if (!RuleAdded) {
			ResRuleV.Add(Rule);
		}
	}

	Notify->OnNotifyFmt(TNotifyType::ntInfo, "Done, found %d rules!", ResRuleV.Len());
}

template <class TSupp, class TConf>
void TApriori<TSupp,TConf>::GenFreqItems(const TIntVV& Mat, const double& SuppThreshold,
		const int& MaxItems, TVec<TIntV>& ItemSetV, const PNotify& Notify) {

	Notify->OnNotify(TNotifyType::ntInfo, "Generating itemsets...");

	const int NAttrs = Mat.GetCols();

	ItemSetV.Gen(MaxItems,0);
	TVec<TPair<TFlt, TIntV>> ItemSetSuppV(MaxItems, 0);	// TODO just for debugging

	// frequent item sets of size 1
	TVec<TIntV> PrevFreqItems(NAttrs,0);
	for (int i = 0; i < NAttrs; i++) {
		TIntV Cand(1,0);	Cand.Add(i);
		if (TSupp::Supp(Mat, Cand) >= SuppThreshold) {
			PrevFreqItems.Add(Cand);
		}
	}
	ItemSetV.AddV(PrevFreqItems);

	// frequent item sets of size k>1
	// using dynamic programming
	int k = 1;
	while (k < MaxItems) {
		// generate all the candidates
		TVec<TIntV> FreqItemsCand;	GenCandV(PrevFreqItems, FreqItemsCand);

		// keep only the candidates with enough support
		TVec<TIntV> FreqItemsK(FreqItemsCand.Len(),0);
		for (int i = 0; i < FreqItemsCand.Len(); i++) {
			TIntV& Cand = FreqItemsCand[i];

			double Supp = TSupp::Supp(Mat, Cand);
			if (Supp >= SuppThreshold) {
				FreqItemsK.Add(Cand);
				ItemSetSuppV.Add(TPair<TFlt, TIntV>(Supp, Cand));	// TODO just for debugging
			}
		}

		// if no more candidates then break
		if (FreqItemsK.Empty()) {
			break;
		}

		ItemSetV.AddV(FreqItemsK);
		PrevFreqItems.Gen(FreqItemsK.Len(),0);
		PrevFreqItems.AddV(FreqItemsK);

		k++;
	}

	TUtils::PrintItemSetV(ItemSetSuppV, Notify);	// TODO just for debugging
}

template <class TSupp, class TConf>
void TApriori<TSupp,TConf>::GenCandV(const TVec<TIntV>& CurrFreqItemV, TVec<TIntV>& NextFreqItemV) {
	const int NCand = CurrFreqItemV.Len();
	const int PrefixLen = CurrFreqItemV[0].Len()-1;

	for (int i = 0; i < NCand-1; i++) {
		const TIntV& Cand1 = CurrFreqItemV[i];

		for (int j = i+1; j < NCand; j++) {
			const TIntV& Cand2 = CurrFreqItemV[j];
			if (!PrefixEq(Cand1, Cand2, PrefixLen)) {
				break;
			}

			TIntV Cand(Cand1.Len()+1,0);
			for (int k = 0; k < PrefixLen; k++) {
				Cand.Add(Cand1[k]);
			}

			Cand.Add(Cand1.Last());
			Cand.Add(Cand2.Last());
			NextFreqItemV.Add(Cand);
		}
	}
}

template <class TSupp, class TConf>
bool TApriori<TSupp,TConf>::PrefixEq(const TIntV& Vec1, const TIntV& Vec2, const int& PrefixLen) {
	EAssert(Vec1.Len() > PrefixLen && Vec2.Len() > PrefixLen);

	for (int i = 0; i < PrefixLen; i++) {
		if (Vec1[i] != Vec2[i]) {
			return false;
		}
	}

	return true;
}

}

#endif /* ANALYTICS_H_ */
