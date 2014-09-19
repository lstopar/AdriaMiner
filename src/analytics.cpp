#include "analytics.h"

using namespace TAdriaAnalytics;
using namespace TAdriaUtils;

double TSupport::Supp(const TIntVV& Mat, const TIntV& ItemIdxV) {
	const int NItems = ItemIdxV.Len();
	const int NInst = Mat.GetRows();

	double NOr = 0;
	double NAnd = 0;

	for (int i = 0; i < NInst; i++) {
		int And = 1;
		int Or = 0;

		for (int j = 0; j < NItems; j++) {
			const int& ItemIdx = ItemIdxV[j];

			And *= Mat(i, ItemIdx);
			Or = (Or == 1 || Mat(i, ItemIdx) == 1) ? 1 : 0;
		}

		NAnd += And;
		NOr += Or;
	}

	return NOr == 0 ? 0 :NAnd / NOr;
}

double TConfidence::Conf(const TIntVV& EventMat, const TIntVV& ObsMat,
			const TIntV& CauseIdxV, const TIntV& EffectIdxV) {

	const int NInst = ObsMat.GetXDim();
	const int NCauses = CauseIdxV.Len();
	const int NEffects = EffectIdxV.Len();
	const int TotalEvents = EventMat.GetYDim();

	double NTotal = 0;
	double NSame = 0;

	for (int i = 0; i < NInst; i++) {
		int AndCause = 1;
		int AndEffect = 1;

		for (int j = 0; j < NCauses; j++) {
			const int& ItemIdx = CauseIdxV[j];

			if (ItemIdx < TotalEvents) {
				AndCause *= EventMat(i, ItemIdx);
			} else {
				AndCause *= ObsMat(i, ItemIdx - TotalEvents);
			}
		}

		for (int j = 0; j < NEffects; j++) {
			const int& ItemIdx = EffectIdxV[j];

			if (ItemIdx < TotalEvents) {
				AndEffect *= EventMat(i, ItemIdx);
			} else {
				AndEffect *= ObsMat(i, ItemIdx - TotalEvents);
			}
		}

		if (AndCause == 1) {
			NTotal += 1;
			if (AndEffect == 1) {
				NSame += 1;
			}
		}
	}

	return NTotal == 0 ? 0 : NSame / NTotal;
}

//////////////////////////////////////////////////////////////
// Linear regression wrapper
const double TLinRegWrapper::RegFact = 1;
const double TLinRegWrapper::ForgetFact = .99;
const int TLinRegWrapper::FeatDim = 2;

TLinRegWrapper::TLinRegWrapper(const TStr& _DbPath, const PNotify& _Notify):
		DbPath(_DbPath),
		LinReg(),
		DataSection(cstRecursive),
		Notify(_Notify) {
	LoadStructs();
}

double TLinRegWrapper::Predict(const TFltV& Sample) {
	return LinReg.Predict(Sample);
}

void TLinRegWrapper::Learn(const TFltV& Sample, const TFlt& Val) {
	TVec<TFltV> InstV;
	TFltV ValV;

	InstV.Add(Sample);
	ValV.Add(Val);

	Learn(InstV, ValV);
}

void TLinRegWrapper::Learn(const TVec<TFltV>& InstV, const TFltV& ValV) {
	if (InstV.Len() != ValV.Len()) {
		throw TExcept::New("Instance and value vector of different lengths!");
	}

	try {
		const int NRecs = InstV.Len();
		for (int i = 0; i < NRecs; i++) {
			LinReg.Learn(InstV[i], ValV[i]);
		}

		LogInstVValV(InstV, ValV);
		SaveStructs();
	} catch (const PExcept& Except) {
		Notify->OnNotify(TNotifyType::ntErr, "TLinRegWrapper::Learn: Failed to learn instances!");
		Notify->OnNotify(TNotifyType::ntErr, Except->GetMsgStr());
	}
}

void TLinRegWrapper::LoadStructs() {
	try {
		TLock Lck(DataSection);

		const TStr FNm = TAdriaUtils::TUtils::GetWaterLevelRegFNm(DbPath);
		const TStr BackupFNm = TAdriaUtils::TUtils::GetBackupWaterLevelRegFNm(DbPath);

		if (!TUtils::LoadStruct(FNm, BackupFNm, LinReg, Notify)) {
			Notify->OnNotify(TNotifyType::ntWarn, "TLinRegWrapper::LoadStructs: Failed to load linreg model! Creating default model...");

			InitDefaultModel();
			SaveStructs();
		}
	} catch (const PExcept& Except) {
		Notify->OnNotify(TNotifyType::ntErr, "TLinRegWrapper::LoadStructs: Failed to load structures!");
		Notify->OnNotify(TNotifyType::ntErr, Except->GetMsgStr());
	}
}

void TLinRegWrapper::SaveStructs() {
	try {
		TLock Lck(DataSection);

		const TStr FNm = TAdriaUtils::TUtils::GetWaterLevelRegFNm(DbPath);
		const TStr BackupFNm = TAdriaUtils::TUtils::GetBackupWaterLevelRegFNm(DbPath);

		TUtils::PersistStruct(FNm, BackupFNm, LinReg, Notify);
	} catch (const PExcept& Except) {
		Notify->OnNotify(TNotifyType::ntErr, "TLinRegWrapper::SaveStructs: Failed to save structures!");
		Notify->OnNotify(TNotifyType::ntErr, Except->GetMsgStr());
	}
}

void TLinRegWrapper::InitDefaultModel() {
	try {
		TFltV Wgts;
		Wgts.Add(1.101491169989339);
		Wgts.Add(-0.035451271447368);

		TFltVV P(TLinRegWrapper::FeatDim, TLinRegWrapper::FeatDim);
		P.PutXY(0, 0, 0.316484968010463);	P.PutXY(0, 1, -0.004930254923017);
		P.PutXY(1, 0, -0.004930254923017);	P.PutXY(1, 1, 0.000083217571561);

		LinReg = TRecLinReg(Wgts, P, TLinRegWrapper::ForgetFact, TLinRegWrapper::RegFact);
	} catch (const PExcept& Except) {
		Notify->OnNotify(TNotifyType::ntErr, "TLinRegWrapper::InitDefaultModel: Failed to initialize model!");
		Notify->OnNotify(TNotifyType::ntErr, Except->GetMsgStr());
	}
}

void TLinRegWrapper::LogInstVValV(const TVec<TFltV>& InstV, const TFltV& ValV) {
	try {
		TLock Lck(DataSection);

		const TStr FNm = TUtils::GetWaterLevelInstancesLogFNm(DbPath);
		const int NInst = InstV.Len();

		TFOut SOut(FNm, true);

		for (int i = 0; i < NInst; i++) {
			TStr InstStr = TStrUtil::GetStr(InstV[i], ",");
			SOut.PutStrFmt("%s,%s\n", InstStr.CStr(), ValV[i].GetStr().CStr());
		}
	} catch (const PExcept& Except) {
		Notify->OnNotify(TNotifyType::ntErr, "TLinRegWrapper::InitDefaultModel: Failed to log instances!");
		Notify->OnNotify(TNotifyType::ntErr, Except->GetMsgStr());
	}
}

//////////////////////////////////////////////////////////////
// Online rule generator
const double TOnlineRuleGenerator::LUM_LOW = 10;
const double TOnlineRuleGenerator::LUM_HIGH = 403;

const double TOnlineRuleGenerator::TEMP_LOW = 10;
const double TOnlineRuleGenerator::TEMP_HIGH = 28;

const int TOnlineRuleGenerator::MX_ITEMSET_SIZE = 3;

const double TOnlineRuleGenerator::THRESHOLD = .8;
const double TOnlineRuleGenerator::FORGET_FACT = .999;

TIntV TOnlineRuleGenerator::ActCanIdV;// = InitCanV();
TIntV TOnlineRuleGenerator::LumCanIdV;
TIntV TOnlineRuleGenerator::TempCanIdV;

int TOnlineRuleGenerator::ActMatDim;
int TOnlineRuleGenerator::ObsMatRows;
int TOnlineRuleGenerator::ObsMatCols;

int TOnlineRuleGenerator::NActAttrs;
int TOnlineRuleGenerator::NObsAttrs;

TIntIntVH TOnlineRuleGenerator::ActRowIdxAttrIdxVH;
TIntIntVH TOnlineRuleGenerator::ObsRowIdxAttrIdxVH;
TIntPrV TOnlineRuleGenerator::ActAttrIdxRowIdxPrV;
TIntPrV TOnlineRuleGenerator::ObsAttrIdxRowIdxPrV;

bool TOnlineRuleGenerator::Initialized = InitCanV();

TOnlineRuleGenerator::TOnlineRuleGenerator(const TStr& _DbPath, const PNotify& _Notify):
		DbPath(_DbPath),
		MatSection(TCriticalSectionType::cstRecursive),
		Notify(_Notify) {
	LoadStatMat();
}

void TOnlineRuleGenerator::Update(const TFltV& StateTblV) {
	Notify->OnNotify(TNotifyType::ntInfo, "TOnlineRuleGenerator::Update: Updating rule matrix...");

	// extract the state
	TFltV ActStateV, LumStateV, TempStateV;
	ExtractState(StateTblV, ActStateV, LumStateV, TempStateV);

	TIntV ActValV, ObsValV;
	CalcAttrValV(ActStateV, LumStateV, TempStateV, ActValV, ObsValV);

	{
		TLock Lck(MatSection);

		TStatMat& ActStatMat = StatMatPr.Val1;
		TStatMat& ObsStatMat = StatMatPr.Val2;

		// update the actuators
		for (int i = 0; i < ActMatDim; i++) {
			const TIntV& ItemSetI = ActRowIdxAttrIdxVH.GetDat(i);

			if (IsActive(ItemSetI, ActValV)) {
				for (int j = 0; j < ActMatDim; j++) {
					// update the statistics at position i,j
					const TIntV& ItemSetJ = ActRowIdxAttrIdxVH.GetDat(j);

					ActStatMat(i,j).Val2 = (IsActive(ItemSetJ, ActValV) ? 1 : 0) + FORGET_FACT * ActStatMat(i,j).Val2;
					ActStatMat(i,j).Val1 += 1;
				}
			}
		}

		// update the observations
		for (int i = 0; i < ObsMatRows; i++) {
			const TIntV& ItemSetI = ObsRowIdxAttrIdxVH.GetDat(i);

			if (IsActive(ItemSetI, ObsValV)) {
				for (int j = 0; j < ObsMatCols; j++) {
					const TIntV& ItemSetJ = ActRowIdxAttrIdxVH.GetDat(j);

					ObsStatMat(i,j).Val2 = (IsActive(ItemSetJ, ActValV) ? 1 : 0) + FORGET_FACT * ObsStatMat(i,j).Val2;
					ObsStatMat(i,j).Val1 += 1;
				}
			}
		}

		PersistStatMat();
	}

	Notify->OnNotify(TNotifyType::ntInfo, "TOnlineRuleGenerator::Update: Done!");
}

void TOnlineRuleGenerator::GetAllRules(TRuleV& RuleV) const {
	const TStatMat& ActStatMat = StatMatPr.Val1;
	const TStatMat& ObsActStatMat = StatMatPr.Val2;

	TRuleV CandRuleV;
	TIntV EffectIdxV;

	// generate rules based only on actuators
	for (int i = 0; i < ActAttrIdxRowIdxPrV.Len(); i++) {
		const int EffectIdx = ActAttrIdxRowIdxPrV[i].Val2;
		const int EffectCanId = ActIdxToCanId(EffectIdx);

		for (int CondIdx = 0; CondIdx < ActMatDim; CondIdx++) {
			// add rule if the conditions are fulfilled
			if (GetProb(ActStatMat, CondIdx, EffectIdx) > THRESHOLD && GetProb(ActStatMat, EffectIdx, CondIdx) > THRESHOLD) {
				const TItemSet& CondIdxV = ActRowIdxAttrIdxVH.GetDat(CondIdx);

				// transform indexes into CAN IDs
				TItemSet CondSet(CondIdxV.Len(), 0);
				for (int k = 0; k < CondIdxV.Len(); k++) {
					CondSet.Add(ActIdxToCanId(CondIdxV[k]));
				}

				CandRuleV.Add(TRule(CondSet, EffectCanId));
				EffectIdxV.Add(EffectIdx);
			}
		}
	}

	// generate additional rules with observations added to conditions
	for (int i = 0; i < CandRuleV.Len(); i++) {
		const int EffectIdx = EffectIdxV[i];

		RuleV.Add(CandRuleV[i]);

		for (int j = 0; j < ObsMatRows; j++) {
			if (GetProb(ObsActStatMat, j, EffectIdx) > THRESHOLD) {
				const TItemSet& CondIdxV = ObsRowIdxAttrIdxVH.GetDat(j);

				// transform indexes to CAN IDs
				TItemSet CondSet = CandRuleV[i].Val1;	// copy and add
				for (int k = 0; k < CondIdxV.Len(); k++) {
					CondSet.Add(ObsIdxToCanId(CondIdxV[k]));
				}

				RuleV.Add(TRule(CondSet, CandRuleV[i].Val2));
			}
		}
	}
}

double TOnlineRuleGenerator::GetProb(const TStatMat& StatMat, const int& i, const int& j) const {
	const TUInt64FltPr& Stats = StatMat(i,j);

	const int AllCount = Stats.Val1;
	const double ObservedCount = Stats.Val2;

	return (FORGET_FACT - 1) * ObservedCount / (TMath::Power(FORGET_FACT, AllCount) - 1);
}

void TOnlineRuleGenerator::ExtractState(const TFltV& TableV, TFltV& ActStateV, TFltV& LumStateV, TFltV& TempStateV) const {
	for (int i = 0; i < ActCanIdV.Len(); i++) {
		ActStateV.Add(TableV[ActCanIdV[i]]);
	}
	for (int i = 0; i < LumCanIdV.Len(); i++) {
		LumStateV.Add(TableV[LumCanIdV[i]]);
	}
	for (int i = 0; i < TempCanIdV.Len(); i++) {
		TempStateV.Add(TableV[TempCanIdV[i]]);
	}
}

void TOnlineRuleGenerator::CalcAttrValV(const TFltV& ActStateV, const TFltV& LumStateV, const TFltV& TempStateV, TIntV& ActValV, TIntV& ObsValV) const {
	ActValV.Gen(NActAttrs, NActAttrs);
	ObsValV.Gen(NObsAttrs, NObsAttrs);

	// actuators
	for (int i = 0; i < ActCanIdV.Len(); i++) {
		ActValV[i] = ActStateV[i] > 0 ? 1 : 0;
	}

	// luminocity
	for (int i = 0; i < LumCanIdV.Len(); i++) {
		const double& Val = LumStateV[i];

		if (Val < LUM_LOW)
			ObsValV[3*i] = 1;
		else if (LUM_LOW <= Val && Val <= LUM_HIGH)
			ObsValV[3*i+1] = 1;
		else
			ObsValV[3*i+2] = 1;
	}

	int Offset = 3*LumCanIdV.Len();

	// temperature
	for (int i = 0; i < TempCanIdV.Len(); i++) {
		const double& Val = TempStateV[i];

		if (Val < TEMP_LOW)
			ObsValV[Offset + 3*i] = 1;
		else if (TEMP_LOW <= Val && Val <= TEMP_HIGH)
			ObsValV[Offset + 3*i+1] = 1;
		else
			ObsValV[Offset + 3*i+2] = 1;
	}
}

void TOnlineRuleGenerator::PersistStatMat() {
	Notify->OnNotify(TNotifyType::ntInfo, "Saving rule statistics matrix...");

	try {
		TLock Lck(MatSection);

		const TStr FNm = TUtils::GetRuleStatMatFNm(DbPath);
		const TStr BackupFNm = TUtils::GetBackupRuleStatMatFNm(DbPath);

		TUtils::PersistStruct(FNm, BackupFNm, StatMatPr, Notify);

		Notify->OnNotify(TNotifyType::ntInfo, "Rule statistics matrix saved!");
	} catch (const PExcept& Except) {
		Notify->OnNotify(TNotifyType::ntErr, "TOnlineRuleGenerator::SaveStatMat: Failed to save matrix!");
		Notify->OnNotify(TNotifyType::ntErr, Except->GetMsgStr());
	}
}

void TOnlineRuleGenerator::LoadStatMat() {
	Notify->OnNotify(TNotifyType::ntInfo, "Loading rule statistics matrix...");

	try {
		TLock Lck(MatSection);

		const TStr FNm = TUtils::GetRuleStatMatFNm(DbPath);
		const TStr BackupFNm = TUtils::GetBackupRuleStatMatFNm(DbPath);

		if (!TUtils::LoadStruct(FNm, BackupFNm, StatMatPr, Notify)) {
			Notify->OnNotify(TNotifyType::ntInfo, "Rule statistics matrix doesn't exist or is corrupt! ");
			CreateStatMat();
		}

		// check for inconsistencies
		if ((int) StatMatPr.Val1.GetXDim() != ActMatDim || (int) StatMatPr.Val1.GetYDim() != ActMatDim) {
			Notify->OnNotifyFmt(TNotifyType::ntInfo, "Statistics matrix has invalid dimensions: (%ld,%ld), expected: (%d,%d)", StatMatPr.Val1.GetXDim(), StatMatPr.Val1.GetYDim(), ActMatDim, ActMatDim);
			CreateStatMat();
		}

		if ((int) StatMatPr.Val2.GetXDim() != ObsMatRows || (int) StatMatPr.Val2.GetYDim() != ObsMatCols) {
			Notify->OnNotifyFmt(TNotifyType::ntInfo, "Statistics matrix has invalid dimensions: (%ld,%ld), expected: (%d,%d)", StatMatPr.Val2.GetXDim(), StatMatPr.Val2.GetYDim(), ObsMatRows, ObsMatCols);
			CreateStatMat();
		}
	} catch (const PExcept& Except) {
		Notify->OnNotify(TNotifyType::ntErr, "TDataProvider::LoadHistV: Failed to load matrix!");
		Notify->OnNotify(TNotifyType::ntErr, Except->GetMsgStr());
	}
}

void TOnlineRuleGenerator::CreateStatMat() {
	TLock Lck(MatSection);

	Notify->OnNotify(TNotifyType::ntInfo, "Creating new rule statistics matrix...");

	StatMatPr = TStatMatPr(TStatMat(ActMatDim, ActMatDim), TStatMat(ObsMatRows, ObsMatCols));
	PersistStatMat();
}

bool TOnlineRuleGenerator::IsActive(const TIntV& ItemSet, const TIntV& AttrValV) {
	int Prod = 1;
	for (int i = 0; i < ItemSet.Len(); i++) {
		Prod *= AttrValV[ItemSet[i]];
	}
	return Prod > 1;
}

int TOnlineRuleGenerator::CalcDim(const int& NAttrs) {
	int Sum = 1;
	for (int i = 1; i <= MX_ITEMSET_SIZE; i++) {
		Sum += Nck(NAttrs, i);
	}

	return Sum;
}

void TOnlineRuleGenerator::GenItemSetV(const int& MxIdx, const int& MxItems, TVec<TIntV>& ItemSetV, const int& CurrIdx) {
	if (CurrIdx == MxIdx) {
		ItemSetV.Add(TIntV());
		return;
	}

	TVec<TIntV> ItemSet1;	GenItemSetV(MxIdx, MxItems, ItemSet1, CurrIdx+1);
	for (int i = 0; i < ItemSet1.Len(); i++) {
		ItemSetV.Add(ItemSet1[i]);
	}

	if (MxItems > 0) {
		TVec<TIntV> ItemSet2;	GenItemSetV(MxIdx, MxItems-1, ItemSet2, CurrIdx+1);

		for (int i = 0; i < ItemSet2.Len(); i++) {
			ItemSet2[i].Add(CurrIdx);
			ItemSetV.Add(ItemSet2[i]);
		}
	}
}

int TOnlineRuleGenerator::ActIdxToCanId(const int& ActIdx) {
	IAssertR(ActIdx < ActCanIdV.Len(), "Index of actuator greater then the number of actuators!");
	return ActCanIdV[ActIdx];
}

int TOnlineRuleGenerator::ObsIdxToCanId(const int& ObsIdx) {
	IAssertR(ObsIdx < LumCanIdV.Len() + TempCanIdV.Len(), "Index of observation greater then the number of observations!");
	if (ObsIdx < LumCanIdV.Len())
		return LumCanIdV[ObsIdx];
	else
		return TempCanIdV[ObsIdx - LumCanIdV.Len()];
}

bool TOnlineRuleGenerator::InitCanV() {

	ActCanIdV.Add(133);		// light 5
	ActCanIdV.Add(135);		// light 22
	ActCanIdV.Add(136);		// light 4
	ActCanIdV.Add(137);		// light 8
	ActCanIdV.Add(138);		// light 9
	ActCanIdV.Add(125);		// light 13
	ActCanIdV.Add(127);		// light 21
	ActCanIdV.Add(145);		// nappa
	ActCanIdV.Add(163);		// projector
	ActCanIdV.Add(181);		// stairs state
	ActCanIdV.Add(152);		// stairs state

	LumCanIdV.Add(124);		// luminocity bedroom
	LumCanIdV.Add(149);		// luminocity living space

	TempCanIdV.Add(147);	// temperature living space

	NActAttrs = ActCanIdV.Len();
	NObsAttrs = 3*(LumCanIdV.Len() + TempCanIdV.Len());

	ActMatDim = CalcDim(ActCanIdV.Len());
	ObsMatRows = CalcDim(NObsAttrs);
	ObsMatCols = ActMatDim;

	// initialize the <index,itemset> hash and attr index to row index vector
	TVec<TIntV> ActItemSetV;	GenItemSetV(NActAttrs, MX_ITEMSET_SIZE, ActItemSetV);
	TVec<TIntV> ObsItemSetV;	GenItemSetV(NObsAttrs, MX_ITEMSET_SIZE, ObsItemSetV);

	for (int i = 0; i < ActItemSetV.Len(); i++) {
		if (ActItemSetV[i].Len() == 1) {
			ActRowIdxAttrIdxVH.AddDat(i, ActItemSetV[i]);
			ActAttrIdxRowIdxPrV.Add(TIntPr(ActItemSetV[i][0],i));
		}
	}

	for (int i = 0; i < ObsItemSetV.Len(); i++) {
		if (ObsItemSetV[i].Len() == 1) {
			ObsRowIdxAttrIdxVH.AddDat(i, ObsItemSetV[i]);
			ObsAttrIdxRowIdxPrV.Add(TIntPr(ObsItemSetV[i][0],i));
		}
	}

	return true;
}

long TOnlineRuleGenerator::Fac(const int& N) {
	long Prod = 1;
	for (int i = 2; i <= N; i++) {
		Prod *= i;
	}
	return Prod;
}

int TOnlineRuleGenerator::Nck(const int& N, const int& K) {
	long Prod = 1;
	for (int i = N; i > TMath::Mx(K,N-K); i--) { Prod *= i; }
	return (int) (Prod / Fac(TMath::Mn(K,N-K)));
}
