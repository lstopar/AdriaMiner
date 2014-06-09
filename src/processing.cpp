#include "processing.h"

using namespace TAdriaAlgs;

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
