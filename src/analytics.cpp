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
		Notify(_Notify) {
	LoadStructs();
}

double TLinRegWrapper::Predict(const TFltV& Sample) {
	return LinReg.Predict(Sample);
}

void TLinRegWrapper::Learn(const TFltV& Sample, const TFlt& Val) {
	LinReg.Learn(Sample, Val);
	SaveStructs();
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
			Notify->OnNotify(TNotifyType::ntWarn, "TLinRegWrapper::LoadStructs: Failed to load linreg model! Creating empty model...");
			LinReg = TRecLinReg(FeatDim, RegFact, ForgetFact);
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
