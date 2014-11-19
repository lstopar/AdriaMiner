#include "adria_server.h"

using namespace TAdriaUtils;
using namespace TAdriaServer;
using namespace TAdriaAnalytics;

uint64 TUtils::GetCurrTimeStamp() {
	return TTm::GetCurUniMSecs();
}

TStr TUtils::GetCurrTimeStr() {
	return TTm::GetTmFromMSecs(GetCurrTimeStamp()).GetWebLogDateTimeStr(true, "T");
}

/////////////////////////////////////////////////////////////////////
// TSampleHistThread

//unsigned int TDataProvider::TSampleHistThread::SleepTm = 10*60*1000;
uint64 TDataProvider::TSampleHistThread::SleepTm = 1000*60*10;	// 10min
uint64 TDataProvider::TSampleHistThread::SampleWaterLevelTm = 1000*10;	// 10s

TDataProvider::TSampleHistThread::TSampleHistThread(TDataProvider* Provider, const PNotify& _Notify):
					DataProvider(Provider),
					Running(false),
					Notify(_Notify) {
	Notify->OnNotify(TNotifyType::ntInfo, "SampleHistThread initialized!");
}

void TDataProvider::TSampleHistThread::Run() {
	Running = true;
	TSysProc::Sleep(TSampleHistThread::SleepTm);

	int LoopIdx = 0;

	while (Running) {
		LoopIdx++;

		try {
			uint64 StartTm = TUtils::GetCurrTimeStamp();
			Notify->OnNotify(TNotifyType::ntInfo, "History loop...");

			DataProvider->SampleWaterLevel();

			if (LoopIdx % (SleepTm / SampleWaterLevelTm) == 0) {
				DataProvider->SampleHist();
				DataProvider->LearnFreshWaterLevel();
				DataProvider->MakePredictions();
				LoopIdx = 0;
			}

			uint64 Dur = TUtils::GetCurrTimeStamp() - StartTm;
			TSysProc::Sleep(TMath::Mx(SampleWaterLevelTm - Dur, uint64(1000)));
		} catch (const PExcept& Except) {
			Notify->OnNotify(TNotifyType::ntErr, "TDataProvider::TSampleHistThread::Run: failed to execute loop!");
			Notify->OnNotify(TNotifyType::ntErr, Except->GetMsgStr());
		} catch (...) {
			Notify->OnNotify(TNotifyType::ntErr, "TDataProvider::TSampleHistThread::Run: WTF!? failed to catch exception!");
		}
	}
}

/////////////////////////////////////////////////////////////////////
// TRuleThread
//uint64 TDataProvider::TRuleThread::SleepTm = 1000*60*60*30;	// 30 mins
uint64 TDataProvider::TRuleThread::SleepTm = 1000*60*10;	// 10min

TDataProvider::TRuleThread::TRuleThread(TDataProvider* Provider, const PNotify& _Notify):
		DataProvider(Provider),
		Running(false),
		Notify(_Notify) {
	Notify->OnNotify(TNotifyType::ntInfo, "Rule thread initialized!");
}

void TDataProvider::TRuleThread::Run() {
	Running = true;
//	TSysProc::Sleep(TRuleThread::SleepTm);

	long Count = 0;
	while (Running) {
		try {
			uint64 StartTm = TUtils::GetCurrTimeStamp();
			Notify->OnNotify(TNotifyType::ntInfo, "Starting rule loop...");

			DataProvider->DelOldRuleInst();
			DataProvider->PersistRuleInstV();

			if (Count++ % 3 == 0) {
				DataProvider->GenRules();
			}

			uint64 Dur = TUtils::GetCurrTimeStamp() - StartTm;

			Notify->OnNotifyFmt(TNotifyType::ntInfo, "Rule loop took %ld ms, sleeping...", Dur);

			TSysProc::Sleep(TMath::Mx(TRuleThread::SleepTm-Dur, uint64(1000)));
		} catch (const PExcept& Except) {
			Notify->OnNotify(TNotifyType::ntErr, "TDataProvider::TRuleThread::Run: failed to execute rule loop!");
			Notify->OnNotify(TNotifyType::ntErr, Except->GetMsgStr());
		} catch (...) {
			Notify->OnNotify(TNotifyType::ntErr, "TDataProvider::TRuleThread::Run: WTF!? failed to catch exception!");
		}
	}
}

/////////////////////////////////////////////////////////////////////
// TOnlineRuleThread
//const uint64 TDataProvider::TOnlineRuleThread::SLEEP_TM = 15000;
//
//TDataProvider::TOnlineRuleThread::TOnlineRuleThread(TDataProvider* Provider, const PNotify& _Notify):
//		DataProvider(Provider),
//		Notify(_Notify) {}
//
//void TDataProvider::TOnlineRuleThread::Run() {
//	while (true) {
//		try {
//			uint64 StartTm = TUtils::GetCurrTimeStamp();
//
//			// copy the current state
//			TFltV StateV;	DataProvider->CpyStateV(StateV);
//
//			// update the model
//			DataProvider->RuleGenerator.Update(StateV);
//
//			uint64 Dur = TUtils::GetCurrTimeStamp() - StartTm;
//			TSysProc::Sleep(TMath::Mx(SLEEP_TM - Dur, uint64(1000)));
//		} catch (const PExcept& Except) {
//			Notify->OnNotify(TNotifyType::ntErr, "TDataProvider::TOnlineRuleThread::Run: failed to execute rule loop!");
//			Notify->OnNotify(TNotifyType::ntErr, Except->GetMsgStr());
//		} catch (...) {
//			Notify->OnNotify(TNotifyType::ntErr, "TDataProvider::TOnlineRuleThread::Run: WTF!? failed to catch exception!");
//		}
//	}
//}

/////////////////////////////////////////////////////////////////////
// Data handler
uint64 TDataProvider::HistDur = 1000*60*60*24*7;	// one week
uint64 TDataProvider::RuleWindowTm = 1000*60*60*24*3;	// 3 days
int TDataProvider::EntryTblLen = 256;
TIntStrH TDataProvider::CanIdVarNmH;

TIntIntH TDataProvider::CanIdPredCanIdH;

TIntV TDataProvider::RuleEffectCanV;
TIntV TDataProvider::RuleObsCanV;
TIntIntH TDataProvider::RuleEventCanIdIdxH;
TIntIntH TDataProvider::RuleObsCanIdIdxH;

bool TDataProvider::FillCanHs() {
	CanIdVarNmH.AddDat(103, "temp_cabin");
	CanIdVarNmH.AddDat(104, "temp_ac");
	CanIdVarNmH.AddDat(106, "battery_ls");
	CanIdVarNmH.AddDat(108, "fresh_water");
	CanIdVarNmH.AddDat(122, "temp_bedroom");
	CanIdVarNmH.AddDat(123, "hum_bedroom");
	CanIdVarNmH.AddDat(124, "lum_bedroom");
	CanIdVarNmH.AddDat(147, "temp_ls");
	CanIdVarNmH.AddDat(148, "hum_ls");
	CanIdVarNmH.AddDat(149, "lum_ls");
	CanIdVarNmH.AddDat(159, "temp_sc");
	CanIdVarNmH.AddDat(160, "hum_sc");
	CanIdVarNmH.AddDat(161, "lum_sc");

	CanIdPredCanIdH.AddDat(TUtils::BATTERY_LS_CANID, 212);	// battery living space
	CanIdPredCanIdH.AddDat(TUtils::FRESH_WATER_CANID, 210);	// fresh water
	CanIdPredCanIdH.AddDat(TUtils::WASTE_WATER_CANID, 211);	// waste water

	RuleEffectCanV.Add(133);	// light 5
	RuleEffectCanV.Add(135);	// light 22
	RuleEffectCanV.Add(136);	// light 4
	RuleEffectCanV.Add(137);	// light 8
	RuleEffectCanV.Add(138);	// light 9
	RuleEffectCanV.Add(125);	// light 13
	RuleEffectCanV.Add(127);	// light 21
	RuleEffectCanV.Add(145);	// nappa
	RuleEffectCanV.Add(163);	// projector
	RuleEffectCanV.Add(181);	// stairs state
	RuleEffectCanV.Add(152);	// stairs state

	RuleObsCanV.Add(124);	// luminocity bedroom
	RuleObsCanV.Add(149);	// luminocity living space
	RuleObsCanV.Add(147);	// temperature living space

	// add to sets for faster lookup
	for (int i = 0; i < RuleEffectCanV.Len(); i++) {
		RuleEventCanIdIdxH.AddDat(RuleEffectCanV[i], i);
	}
	for (int i = 0; i < RuleObsCanV.Len(); i++) {
		RuleObsCanIdIdxH.AddDat(RuleObsCanV[i], i);
	}

	return true;
}

bool TDataProvider::Init = TDataProvider::FillCanHs();


TDataProvider::TDataProvider(const TStr& _DbPath, const PNotify& _Notify):
		DbPath(_DbPath),
		EntryTbl(TDataProvider::EntryTblLen, TDataProvider::EntryTblLen),
		HistH(),
		RuleInstV(),
		WaterLevelV(),
		WaterLevelReg(DbPath, _Notify),
//		RuleGenerator(DbPath, _Notify),
		HistThread(),
		RuleThread(),
//		OnlineRuleThread(),
		DataSection(TCriticalSectionType::cstRecursive),
		HistSection(TCriticalSectionType::cstRecursive),
		RuleSection(TCriticalSectionType::cstRecursive),
		Notify(_Notify) {

	LoadStructs();

	Notify->OnNotify(TNotifyType::ntInfo, "Data provider initialized!");
}

void TDataProvider::OnConnected() {
	Notify->OnNotify(TNotifyType::ntInfo, "TDataProvider::OnConnected: starting threads...");

	try {
		// init threads
		HistThread = new TSampleHistThread(this, Notify);
		RuleThread = new TRuleThread(this, Notify);
//		OnlineRuleThread = new TOnlineRuleThread(this, Notify);

		// start threads
		HistThread->Start();
		RuleThread->Start();
//		OnlineRuleThread->Start();
	} catch (const PExcept& Except) {
		Notify->OnNotify(TNotifyType::ntErr, "TDataProvider: failed to start threads!!");
		Notify->OnNotify(TNotifyType::ntErr, Except->GetMsgStr());
	}
}

void TDataProvider::AddRec(const int& CanId, const PJsonVal& Rec) {
	try {
		if (CanId >= TDataProvider::EntryTblLen) { return; }
		{
			TLock Lock(DataSection);

			// put the entry into the state table
			EntryTbl[CanId] = Rec->GetObjNum("value");

			AddRecToLog(CanId, Rec);
		}
		if (RuleEventCanIdIdxH.IsKey(CanId)) {
			AddRuleInstance(CanId);
		}
	} catch (const PExcept& Except) {
		Notify->OnNotify(TNotifyType::ntErr, "Unable to add record to base!");
		Notify->OnNotify(TNotifyType::ntErr, Except->GetMsgStr());
	}
}

void TDataProvider::AddRuleInstance(const int& CanId) {
	try {
		TLock Lock(RuleSection);

		TFltV StateV(RuleEffectCanV.Len() + RuleObsCanV.Len(),0);

		for (int i = 0; i < RuleEffectCanV.Len(); i++) {
			const int& CanId = RuleEffectCanV[i];
			StateV.Add(EntryTbl[CanId]);
		}
		for (int i = 0; i < RuleObsCanV.Len(); i++) {
			const int& CanId = RuleObsCanV[i];
			StateV.Add(EntryTbl[CanId]);
		}

		uint64 Tm = TUtils::GetCurrTimeStamp();
		RuleInstV.Add(TKeyDat<TUInt64,TFltV>(Tm, StateV));
	} catch (const PExcept& Except) {
		Notify->OnNotify(TNotifyType::ntErr, "Unable to add an instance to the rule DB!");
		Notify->OnNotify(TNotifyType::ntErr, Except->GetMsgStr());
	}
}

void TDataProvider::DelOldRuleInst() {
	Notify->OnNotify(TNotifyType::ntInfo, "Deleting old rule instances...");

	try {
		TLock Lck(RuleSection);
		uint64 OldestTm = TUtils::GetCurrTimeStamp() - TDataProvider::RuleWindowTm;

		int StartRuleIdx = 0;
		while (StartRuleIdx < RuleInstV.Len() && RuleInstV[StartRuleIdx].Key < OldestTm) {
			StartRuleIdx++;
		}

		if (StartRuleIdx > 0) {
			RuleInstV.Del(0, StartRuleIdx-1);
			Notify->OnNotifyFmt(TNotifyType::ntInfo, "Deleted %d instances...", StartRuleIdx);
		}
	} catch (const PExcept& Except) {
		Notify->OnNotify(TNotifyType::ntErr, "TDataProvider::DelOldRuleInst: Failed to delete old rule instances!");
		Notify->OnNotify(TNotifyType::ntErr, Except->GetMsgStr());
	}
}

void TDataProvider::GetHistory(const int& CanId, TUInt64FltKdV& HistoryV) {
	Notify->OnNotifyFmt(TNotifyType::ntInfo, "Fetching history for CAN: %d", CanId);

	if (!HistH.IsKey(CanId)) {
		Notify->OnNotifyFmt(TNotifyType::ntWarn, "Tried to fetch untracked history: %d. Ignoring!", CanId);
		return;
	}

	try {
		TLock Lck(HistSection);
		HistoryV.AddV(HistH.GetDat(CanId));
	} catch (const PExcept& Except) {
		Notify->OnNotifyFmt(TNotifyType::ntErr, "Failed to retrieve history for CAN: %d", CanId);
		Notify->OnNotify(TNotifyType::ntErr, Except->GetMsgStr());
	}
}

double TDataProvider::PredictBattery() {
	Notify->OnNotify(TNotifyType::ntInfo, "Predicting bettery...");

	try {
		const double Level0 = 10.5;
		const double Wgt = 26.518113677852998;
		const double CurrLevel = EntryTbl[TUtils::BATTERY_LS_CANID];

		const double BatteryPred = TMath::Mx(Wgt*(CurrLevel - Level0), 1e-5);

		PredictionCallback->OnPrediction(TUtils::BATTERY_LS_CANID, BatteryPred);

		return BatteryPred;
	} catch (const PExcept& Except) {
		Notify->OnNotify(TNotifyType::ntErr, "Failed to predict battery!");
		Notify->OnNotify(TNotifyType::ntErr, Except->GetMsgStr());
		return 0;
	}
}

double TDataProvider::PredictFreshWaterLevel(const bool& NotifySrv) {
	const double Level0 = 5;

	try {
		double CurrLevel = EntryTbl[TUtils::FRESH_WATER_CANID];

		Notify->OnNotifyFmt(TNotifyType::ntInfo, "Predicting fresh water level. Current level: %.2f", CurrLevel);

		TFltV FeatV;
		FeatV.Add(1); FeatV.Add(CurrLevel);

		const TRecLinReg& LinReg = WaterLevelReg.GetRegModel();
		TFltV Wgts;	LinReg.GetCoeffs(Wgts);

		double Beta0 = Wgts[0];
		double Beta1 = Wgts[1];

		//pred = exp(beta_0)*(exp(beta_1*L)-exp(beta_1*L_0))/beta_1
		double Pred = TMath::Mx(exp(Beta0)*(exp(Beta1*CurrLevel) - exp(Beta1*Level0)) / Beta1, 1e-5);

		Notify->OnNotifyFmt(TNotifyType::ntInfo, "Predicted: %.2f", Pred);

		if (NotifySrv) {
			PredictionCallback->OnPrediction(TUtils::FRESH_WATER_CANID, Pred);
		}

		return Pred;
	} catch (const PExcept& Except) {
		Notify->OnNotify(TNotifyType::ntErr, " TDataProvider::PredictFreshWaterLevel: Failed to make a prediction!");
		Notify->OnNotify(TNotifyType::ntErr, Except->GetMsgStr());
		return 0;
	}
}

double TDataProvider::PredictWasteWaterLevel() {
	try {
		Notify->OnNotify(TNotifyType::ntInfo, "Predicting waste water level...");

		double FreshWaterPred = PredictFreshWaterLevel(false);
		PredictionCallback->OnPrediction(TUtils::WASTE_WATER_CANID, FreshWaterPred);
		return FreshWaterPred;
	} catch (const PExcept& Except) {
		Notify->OnNotify(TNotifyType::ntErr, " TDataProvider::PredictWasteWaterLevel: Failed to make a prediction!");
		Notify->OnNotify(TNotifyType::ntErr, Except->GetMsgStr());
		return 0;
	}
}

void TDataProvider::LearnFreshWaterLevel() {
	Notify->OnNotify(TNotifyType::ntInfo, "Updating fresh water level model...");

	try {
		const double MillisToHours = 1.0/(60*60*1000);

		TFltV MaV;
		TFltV DerivV;
		TFltV TmV;
		TBoolV IsFillingV;

		const int MaWindowLen = 2*1200 / TSampleHistThread::SampleWaterLevelTm;

		// copy the reading into another list and operate on them from there
		int NInst;
		{
			TLock Lck(HistSection);

			NInst = WaterLevelV.Len();

			MaV.Gen(NInst, 0);
			DerivV.Gen(NInst, 0);
			TmV.Gen(NInst, 0);
			IsFillingV.Gen(NInst, 0);

			double MovingSum = 0;
			int WindowLen;

			// compute the moving average
			for (int i = 0; i < NInst; i++) {
				if (i >= MaWindowLen) {
					MovingSum -= WaterLevelV[i-MaWindowLen].Val2;
				}

				MovingSum += WaterLevelV[i].Val2;

				WindowLen = TMath::Mn(i+1, MaWindowLen);

				MaV.Add(MovingSum / WindowLen);
				TmV.Add(WaterLevelV[i].Val1 * MillisToHours);
			}

			// compute the derivative
			for (int i = 0; i < NInst-MaWindowLen; i++) {
				DerivV[i] = (MaV[i+MaWindowLen] - MaV[i]) /
						((WaterLevelV[i+MaWindowLen].Val1 - WaterLevelV[i].Val1) * MillisToHours);
			}
		}

		Notify->OnNotify(TNotifyType::ntInfo, "Finding where the water was filled...");

		// compute where the water is being filled
		const double FillThreshold = 6;
		const double DerivThreshold = .5;

		bool IsFilling = false;

		int StartLevel = -1;
		int EndLevel = -1;

		int StartIdx = -1;

		NInst = NInst-MaWindowLen;
		for (int i = 0; i < NInst; i++) {
			if (!IsFilling && DerivV[i] > DerivThreshold) {
				IsFilling = true;
				StartLevel = MaV[i];
				StartIdx = i;
			}

			if (IsFilling && DerivV[i] < -DerivThreshold) {
				IsFilling = false;
				EndLevel = MaV[i];

				if (EndLevel - StartLevel >= FillThreshold) {
					for (int j = StartIdx; j <= i; j++) {
						IsFillingV[j] = true;
					}
				}
			}
		}

		Notify->OnNotify(TNotifyType::ntInfo, "Generating instances...");

		IsFilling = false;
		int FirstIdx = -1;
		int LastIdx = -1;

		// generate instances for linear regression
		TVec<TFltV> InstV;
		TFltV ValV;

		for (int i = 0; i < NInst; i++) {
			// check if the water level is steady
			if (IsFilling && !IsFillingV[i]) {
				FirstIdx = i;
				IsFilling = false;
			}
			else if (!IsFilling && IsFillingV[i]) {	// stopped being steady
				LastIdx = i-1;
				IsFilling = true;

				double CurrLevel = MaV[FirstIdx];
				double LastLevel = MaV[LastIdx];

				double DeltaTm = (TmV[LastIdx] - TmV[FirstIdx]);
				double DeltaLevel = CurrLevel - LastLevel;

				double Deriv = DeltaTm / DeltaLevel;

				TFltV FeatV; FeatV.Add(1); FeatV.Add(CurrLevel);
				InstV.Add(FeatV);
				ValV.Add(log(Deriv));
			}
		}

		// incorporate the new instances into the current model
		WaterLevelReg.Learn(InstV, ValV);

		// delete the unused stuff
		Notify->OnNotifyFmt(TNotifyType::ntInfo, "Deleting water %d level instances...", LastIdx);
		if (LastIdx > 0) {
			TLock Lck(HistSection);
			WaterLevelV.Del(0, LastIdx-1);
		}
	} catch (const PExcept& Except) {
		Notify->OnNotify(TNotifyType::ntErr, " TDataProvider::LearnWaterLevel: Failed to update the model!");
		Notify->OnNotify(TNotifyType::ntErr, Except->GetMsgStr());
	}
}


void TDataProvider::AddRecToLog(const int& CanId, const PJsonVal& Rec) {
	try {
		TLock Lock(DataSection);

		const TStr LogFName = TUtils::GetLogFName(DbPath);

		bool LogExists = TFile::Exists(LogFName);
		TFOut Out(LogFName, true);

		if (LogExists) { Out.PutCh('\n'); }
		Out.PutStr(Rec->GetObjStr("timestamp"));
		Out.PutCh(',');
		Out.PutInt(CanId);
		Out.PutCh(',');
		Out.PutFlt(Rec->GetObjNum("value"));

		Out.Flush();
	} catch (const PExcept& Except) {
		Notify->OnNotify(TNotifyType::ntErr, "Unable to add record to base!");
		Notify->OnNotify(TNotifyType::ntErr, Except->GetMsgStr());
	}
}

void TDataProvider::SampleHistFromV(const TFltV& StateV, const uint64& SampleTm) {
	try {
		TLock Lck(HistSection);

		// add the current values to history
		TIntV KeyV;	HistH.GetKeyV(KeyV);

		for (int KeyIdx = 0; KeyIdx < KeyV.Len(); KeyIdx++) {
			const int& CanId = KeyV[KeyIdx];
			const TFlt Val = StateV[CanId];
			HistH.GetDat(CanId).Ins(0, TUInt64FltKd(SampleTm, Val));
		}

		// remove the outdated entries
		for (int KeyIdx = 0; KeyIdx < KeyV.Len(); KeyIdx++) {
			const int& CanId = KeyV[KeyIdx];

			TUInt64FltKdV& HistV = HistH.GetDat(CanId);
			while (HistV.Last().Key < SampleTm - HistDur) {
				HistV.DelLast();
			}
		}

	} catch (const PExcept& Except) {
		Notify->OnNotify(TNotifyType::ntErr, "Failed to sample history!");
		Notify->OnNotify(TNotifyType::ntErr, Except->GetMsgStr());
	}
}

void TDataProvider::SampleHist() {
	Notify->OnNotify(TNotifyType::ntInfo, "Adding current state to history...");
	SampleHistFromV(EntryTbl, TUtils::GetCurrTimeStamp());
	PersistHist();
}

void TDataProvider::SampleWaterLevel() {
	Notify->OnNotify(TNotifyType::ntInfo, "Sampling fresh water level...");

	try {
		const uint64 Tm = TUtils::GetCurrTimeStamp();
		const TFlt WaterLevel = EntryTbl[TUtils::FRESH_WATER_CANID];

		{
			TLock Lck(HistSection);
			WaterLevelV.Add(TUInt64FltPr(Tm, WaterLevel));
			PersistWaterLevelV();
		}
	} catch (const PExcept& Except) {
		Notify->OnNotify(TNotifyType::ntErr, "Failed to sample fresh water level!");
		Notify->OnNotify(TNotifyType::ntErr, Except->GetMsgStr());
	}
}

void TDataProvider::CpyStateV(TFltV& StateV) {
	try {
		const int NEntries = EntryTbl.Len();

		StateV.Gen(NEntries, 0);

		for (int i = 0; i < NEntries; i++) {
			StateV.Add(EntryTbl[i]);
		}
	} catch (const PExcept& Except) {
		Notify->OnNotify(TNotifyType::ntErr, "Failed to copy state vector!");
		Notify->OnNotify(TNotifyType::ntErr, Except->GetMsgStr());
	}
}

void TDataProvider::MakePredictions() {
	Notify->OnNotify(TNotifyType::ntInfo, "Making predictions and distributing...");

	try {
		PredictBattery();
		PredictFreshWaterLevel();
		PredictWasteWaterLevel();
	} catch (const PExcept& Except) {
		Notify->OnNotify(TNotifyType::ntErr, "Failed to make predictions!");
		Notify->OnNotify(TNotifyType::ntErr, Except->GetMsgStr());
	}
}

void TDataProvider::GenRules() {
	Notify->OnNotify(TNotifyType::ntInfo, "Generating rules...");

	const int MinRuleInst = 20;

	try {
		// put all the instances into a different vector and
		// operate on them from there
		TVec<TFltV> EventInstV;
		TVec<TFltV> ObsInstV;

		{
			Notify->OnNotify(TNotifyType::ntInfo, "Copying instances...");
			TLock Lck(RuleSection);

			if (RuleInstV.Len() < MinRuleInst) {
				Notify->OnNotifyFmt(TNotifyType::ntInfo, "Only %d instances for generating rules present, returning...", RuleInstV.Len());
				return;
			}

			int NInst = RuleInstV.Len();
			EventInstV.Gen(NInst,0);
			ObsInstV.Gen(NInst,0);

			for (int i = 0; i < NInst; i++) {
				TFltV EffectV(RuleEffectCanV.Len(),0);
				TFltV ObsV(RuleObsCanV.Len(),0);

				for (int j = 0; j < RuleEffectCanV.Len(); j++) {
					EffectV.Add(RuleInstV[i].Dat[j]);
				}
				for (int j = 0; j < RuleObsCanV.Len(); j++) {
					ObsV.Add(RuleInstV[i].Dat[j + RuleEffectCanV.Len()]);
				}

				EventInstV.Add(EffectV);
				ObsInstV.Add(ObsV);
			}
		}

		// run the APRIORI algorithm
		double MinSupp = .7;
		double MinConf = .7;
		int MaxItems = 3;

		TIntVV EventMat;
		TIntVV ObsMat;
		TVec<TPair<TIntV,TInt>> RuleIdxV;
		TVec<TPair<TStrV,TStr>> RuleV;

		PreprocessApriori(EventInstV, ObsInstV, EventMat, ObsMat);
		TApriori<TSupport, TConfidence>::Run(EventMat, ObsMat, MinSupp, MinConf, RuleIdxV, MaxItems, Notify);

		if (RuleIdxV.Empty()) { return; }

		InterpretApriori(RuleIdxV, RuleV);

		// send the rules to the bus
		RulesCallback->OnRulesGenerated(RuleV);
	} catch (const PExcept& Except) {
		Notify->OnNotify(TNotifyType::ntErr, "Failed to generate rules!");
		Notify->OnNotify(TNotifyType::ntErr, Except->GetMsgStr());
	}
}

void TDataProvider::PreprocessApriori(const TVec<TFltV>& EventInstV,
				const TVec<TFltV>& ObsInstV, TIntVV& EventMat, TIntVV& ObsMat) {

	if (EventInstV.Empty())
		return;

	Notify->OnNotify(TNotifyType::ntInfo, "Preprocessing for APRIORI...");

	try {
		const int NRows = EventInstV.Len();
		const int NEventCols = EventInstV[0].Len();

		// first fill the event matrix
		// it only consists of 0 and 1
		EventMat.Gen(NRows, NEventCols);
		for (int RowIdx = 0; RowIdx < NRows; RowIdx++) {
			for (int ColIdx = 0; ColIdx < NEventCols; ColIdx++) {
				EventMat.PutXY(RowIdx, ColIdx, EventInstV[RowIdx][ColIdx] > 0 ? 1 : 0);
			}
		}


		if (!ObsInstV.Empty()) {
			// discretize the observations
			const int LumBedCanId = 124;	// luminocity bedroom
			const int LumLsCanId = 149;		// luminocity living space
			const int TempLsCanId = 147;	// temperature living space

			const int LumBedIdx = RuleObsCanIdIdxH.GetDat(LumBedCanId);
			const int LumLsIdx = RuleObsCanIdIdxH.GetDat(LumLsCanId);	// intervals for luminocity
			const int TempLsIdx = RuleObsCanIdIdxH.GetDat(TempLsCanId);	// intervals for temperature

			const int NIntervals = 3;

			const TFltPr LumThrs(2.302585092994046,5.998936561946683);
			const TFltPr TempThrs(10,28);

			ObsMat.Gen(NRows, 3*ObsInstV[0].Len());

			for (int i = 0; i < NRows; i++) {
				double LumBedVal = TMath::Log(ObsInstV[i][LumBedIdx]);
				double LumLsVal = TMath::Log(ObsInstV[i][LumLsIdx]);
				double TempLsVal = ObsInstV[i][TempLsIdx];

				if (LumBedVal < LumThrs.Val1) {
					ObsMat.PutXY(i, 0, 1);
				} else if (LumThrs.Val1 <= LumBedVal && LumBedVal <= LumThrs.Val2) {
					ObsMat.PutXY(i, 1, 1);
				} else {
					ObsMat.PutXY(i, 2, 1);
				}

				if (LumLsVal < LumThrs.Val1) {
					ObsMat.PutXY(i, NIntervals, 1);
				} else if (LumThrs.Val1 <= LumLsVal && LumLsVal <= LumThrs.Val2) {
					ObsMat.PutXY(i, NIntervals+1, 1);
				} else {
					ObsMat.PutXY(i, NIntervals+2, 1);
				}

				if (TempLsVal < TempThrs.Val1) {
					ObsMat.PutXY(i, 2*NIntervals, 1);
				} else if (TempThrs.Val1 <= TempLsVal && TempLsVal <= TempThrs.Val2) {
					ObsMat.PutXY(i, 2*NIntervals+1, 1);
				} else {
					ObsMat.PutXY(i, 2*NIntervals+2, 1);
				}
		}
		}
	} catch (const PExcept& Except) {
		Notify->OnNotify(TNotifyType::ntErr, "Failed to preprocess data for APRIORI!");
		Notify->OnNotify(TNotifyType::ntErr, Except->GetMsgStr());
	}
}

void TDataProvider::InterpretApriori(const TVec<TPair<TIntV,TInt>>& RuleIdxV, TVec<TPair<TStrV,TStr>>& RuleV) const {
	try {
		Notify->OnNotify(TNotifyType::ntInfo, "Interpreting rules...");

		const int NRules = RuleIdxV.Len();

		RuleV.Gen(NRules,0);

		// prepare a lookup table
		TIntIntH IdxCanIdH;
		TIntV EventCanV;	RuleEventCanIdIdxH.GetKeyV(EventCanV);
		TIntV ObsCanV;	RuleObsCanIdIdxH.GetKeyV(ObsCanV);

		for (int i = 0; i < EventCanV.Len(); i++) {
			const int CanId = EventCanV[i];
			const int EventIdx = RuleEventCanIdIdxH.GetDat(CanId);
			IdxCanIdH.AddDat(EventIdx, CanId);
		}
		for (int i = 0; i < ObsCanV.Len(); i++) {
			const int CanId = ObsCanV[i];
			IdxCanIdH.AddDat(EventCanV.Len() + 3*RuleObsCanIdIdxH.GetDat(CanId), CanId);
			IdxCanIdH.AddDat(EventCanV.Len() + 3*RuleObsCanIdIdxH.GetDat(CanId)+1, CanId);
			IdxCanIdH.AddDat(EventCanV.Len() + 3*RuleObsCanIdIdxH.GetDat(CanId)+2, CanId);
		}

		// replace indexes with CAN IDs
		for (int RuleIdx = 0; RuleIdx < NRules; RuleIdx++) {
			const TPair<TIntV,TInt>& RuleIdxPr = RuleIdxV[RuleIdx];

			// effects
			const TInt EffectCan = IdxCanIdH.GetDat(RuleIdxPr.Val2);
			TStr EffectStr = EffectCan.GetStr() + "=1";

			// causes
			const TIntV& CauseIdxV = RuleIdxPr.Val1;
			TStrV CauseStrV(CauseIdxV.Len(),0);
			for (int i = 0; i < CauseIdxV.Len(); i++) {
				const TInt& CauseIdx = CauseIdxV[i];
				const TInt& CanId = IdxCanIdH.GetDat(CauseIdx);

				if (RuleEventCanIdIdxH.IsKey(CanId)) {
					CauseStrV.Add(CanId.GetStr() + "=1");
				} else {
					int Interval = (CauseIdx - EventCanV.Len()) % 3;
					if (Interval == 0) {
						CauseStrV.Add(CanId.GetStr() + "=LOW");
					} else if (Interval == 1) {
						CauseStrV.Add(CanId.GetStr() + "=MEDIUM");
					} else {
						CauseStrV.Add(CanId.GetStr() + "=HIGH");
					}
				}
			}

			RuleV.Add(TPair<TStrV,TStr>(CauseStrV, EffectStr));
		}
	} catch (const PExcept& Except) {
		Notify->OnNotify(TNotifyType::ntErr, "Failed to interpret rules!");
		Notify->OnNotify(TNotifyType::ntErr, Except->GetMsgStr());
	}
}

void TDataProvider::LoadStructs() {
	Notify->OnNotify(TNotifyType::ntInfo, "Initializing history...");

	try {
		TLock Lck(HistSection);

		LoadHistV();
		LoadRuleInstV();
		LoadWaterLevelV();

		Notify->OnNotify(TNotifyType::ntInfo, "History initialized!");
	} catch (const PExcept& Except) {
		Notify->OnNotify(TNotifyType::ntErr, "Failed to initialize history!");
		Notify->OnNotify(TNotifyType::ntErr, Except->GetMsgStr());
	}
}

void TDataProvider::LoadHistV() {
	Notify->OnNotify(TNotifyType::ntInfo, "TDataProvider::LoadHistV: Loading history...");

	try {
		TLock Lck(HistSection);

		const TStr HistFName = TUtils::GetHistFName(DbPath);
		const TStr BackupFName = TUtils::GetHistBackupFName(DbPath);

		if (!TUtils::LoadStruct(HistFName, BackupFName, HistH, Notify)) {
			Notify->OnNotify(TNotifyType::ntInfo, "History doesn't exist or is corrupt! Creating new history vector...");

			// get CAN IDs
			TIntV KeyV;	TDataProvider::CanIdVarNmH.GetKeyV(KeyV);
			HistH.Clr();

			for (int i = 0; i < KeyV.Len(); i++) {
				const TInt& CanId = KeyV[i];

				HistH.AddDat(CanId, TUInt64FltKdV());
			}

			PersistHist();
		}
	} catch (const PExcept& Except) {
		Notify->OnNotify(TNotifyType::ntErr, "TDataProvider::LoadHistV: Failed to load history!");
		Notify->OnNotify(TNotifyType::ntErr, Except->GetMsgStr());
	}
}

void TDataProvider::LoadRuleInstV() {
	Notify->OnNotify(TNotifyType::ntInfo, "Loading instances for learning rules...");

	try {
		TLock Lck(RuleSection);

		const TStr RuleFNm = TUtils::GetRuleFName(DbPath);
		const TStr BackupRuleFNm = TUtils::GetBackupRuleFName(DbPath);

		if (!TUtils::LoadStruct(RuleFNm, BackupRuleFNm, RuleInstV, Notify)) {
			Notify->OnNotify(TNotifyType::ntInfo, "Rule instances don't exist or are corrupt, leaving empty vector...");
			PersistRuleInstV();
		}
	} catch (const PExcept& Except) {
		Notify->OnNotify(TNotifyType::ntErr, "Failed to load instances for learning rules!");
		Notify->OnNotify(TNotifyType::ntErr, Except->GetMsgStr());
	}
}

void TDataProvider::LoadWaterLevelV() {
	Notify->OnNotify(TNotifyType::ntInfo, "Loading instances for predicting water level...");

	try {
		TLock Lck(HistSection);

		const TStr WLevelFNm = TUtils::GetWaterLevelFNm(DbPath);
		const TStr BackupWLevelFNm = TUtils::GetBackupWLevelFNm(DbPath);

		if (!TUtils::LoadStruct(WLevelFNm, BackupWLevelFNm, WaterLevelV, Notify)) {
			Notify->OnNotify(TNotifyType::ntInfo, "TDataProvider::LoadWaterLevelV: Water levels are missing or corrupt, loaded empty vector...");
			PersistWaterLevelV();
		}
	} catch (const PExcept& Except) {
		Notify->OnNotify(TNotifyType::ntErr, "Failed to load instances for predicting water level!");
		Notify->OnNotify(TNotifyType::ntErr, Except->GetMsgStr());
	}
}

void TDataProvider::PersistHist() {
	Notify->OnNotify(TNotifyType::ntInfo, "Persisting history...");

	try {
		TLock Lock(HistSection);

		const TStr HistFName = TUtils::GetHistFName(DbPath);
		const TStr BackupFName = TUtils::GetHistBackupFName(DbPath);

		TUtils::PersistStruct(HistFName, BackupFName, HistH, Notify);

		Notify->OnNotify(TNotifyType::ntInfo, "History persisted!");
	} catch (const PExcept& Except) {
		Notify->OnNotify(TNotifyType::ntErr, "Failed to persist history!");
		Notify->OnNotify(TNotifyType::ntErr, Except->GetMsgStr());
	}
}

void TDataProvider::PersistRuleInstV() {
	Notify->OnNotify(TNotifyType::ntInfo, "Persisting rules...");

	try {
		TLock Lck(RuleSection);

		const TStr RuleFName = TUtils::GetRuleFName(DbPath);
		const TStr BackupRuleFName = TUtils::GetBackupRuleFName(DbPath);

		// remove the file and create a new file
		if (TFile::Exists(RuleFName)) {
			TFile::Del(RuleFName);
		}

		// save a new rule file
		{
			TFOut Out(RuleFName);
			RuleInstV.Save(Out);
		}

		// the new file is created, now create a new backup file
		// first remove the old backup file
		if (TFile::Exists(BackupRuleFName)) {
			TFile::Del(BackupRuleFName);
		}

		// save a new rule file
		{
			TFOut Out(BackupRuleFName);
			RuleInstV.Save(Out);
		}
	} catch (const PExcept& Except) {
		Notify->OnNotify(TNotifyType::ntErr, "Failed to persist rule instances!");
		Notify->OnNotify(TNotifyType::ntErr, Except->GetMsgStr());
	}
}

void TDataProvider::PersistWaterLevelV() {
	Notify->OnNotify(TNotifyType::ntInfo, "Persisting water levels...");

	try {
		TLock Lck(HistSection);

		TUtils::PersistStruct(TUtils::GetWaterLevelFNm(DbPath), TUtils::GetBackupWLevelFNm(DbPath), WaterLevelV, Notify);
	} catch (const PExcept& Except) {
		Notify->OnNotify(TNotifyType::ntErr, "TDataProvider::PersistWaterLevelV: Failed to persist water levels!");
		Notify->OnNotify(TNotifyType::ntErr, Except->GetMsgStr());
	}
}


//////////////////////////////////////////////////////////
// Adria Client
TAdriaCommunicator::TAdriaCommunicator(const TStr& _Url, const int& _Port, const PNotify& _Notify):
		TSockEvent(),
		Url(_Url),
		Port(_Port),
		SockHost(NULL),
		Sock(NULL),
		Notify(_Notify),
		CurrMsg(TAdriaMsg::New(_Notify)),
		MsgCallbacks(5,0),
		SocketSection(TCriticalSectionType::cstRecursive),
		CallbackSection(TCriticalSectionType::cstRecursive),
		IsClosed(false) {

	Connect();
}

void TAdriaCommunicator::OnConnect(const uint64& SockId) {
	// compose adria protocol initialization request
	Notify->OnNotifyFmt(TNotifyType::ntInfo, "Connected socket: %lu, writing desired inputs...", SockId);

	Write("PUSH res_table|GET history,prediction&ANALYTICS,qm1\r\n");
	Write("GET res_table\r\n");			// refresh the table

	OnAdriaConnected();
}

void TAdriaCommunicator::OnRead(const uint64& SockId, const PSIn& SIn) {
	try {
		{
			TLock Lock(SocketSection);

			// parse the protocol
			CurrMsg->Read(SIn);
		}
		if (CurrMsg->IsComplete()) {
			OnMsgReceived(CurrMsg);
		}
	} catch (const PExcept& Except) {
		Notify->OnNotifyFmt(TNotifyType::ntErr, "Failed to Read: %s", Except->GetMsgStr().CStr());
		Notify->OnNotify(TNotifyType::ntErr, "Reseting...");
	}
}

void TAdriaCommunicator::OnGetHost(const PSockHost& SockHost) {
	Notify->OnNotify(TNotifyType::ntInfo, "OnGetHost called...");

	if (SockHost->IsOk()) {
		Notify->OnNotify(TNotifyType::ntInfo, "Creating socket...");

		Sock = TSock::New(this);
		Sock->Connect(SockHost, GetPort());

		Notify->OnNotify(TNotifyType::ntInfo, "Socket created!");
	} else {
		Notify->OnNotifyFmt(TNotifyType::ntErr, "Failed to connect to host %s: %s!", SockHost->GetHostNm().CStr(), SockHost->GetErrMsg().CStr());
	}
}

void TAdriaCommunicator::OnReadEof(const uint64& SockId) {
	Notify->OnNotifyFmt(TNotifyType::ntInfo, "End of file: %lu", SockId);
	CloseConn();
}

void TAdriaCommunicator::OnTimeOut(const uint64& SockId) {
	Notify->OnNotifyFmt(TNotifyType::ntInfo, "Socket timed out: %lu", SockId);
	CloseConn();
}

void TAdriaCommunicator::OnClose(const uint64& SockId) {
	Notify->OnNotifyFmt(TNotifyType::ntInfo, "Socket closed: %lu", SockId);
}

void TAdriaCommunicator::AfterClose(const uint64& SockId) {
	Notify->OnNotifyFmt(TNotifyType::ntInfo, "AfterClose: %lu", SockId);
	Reconnect();
}

void TAdriaCommunicator::OnError(const uint64& SockId, const int& ErrCd, const TStr& ErrStr) {
	Notify->OnNotifyFmt(TNotifyType::ntErr, "Error on socket client: %d, %s", ErrCd, ErrStr.CStr());
	CloseConn();
}


bool TAdriaCommunicator::Write(const PSIn& SIn) {
	try {
		TLock Lock(SocketSection);

		// send to socket
		bool Ok;
		TStr ErrMsg;
		Sock->Send(SIn, Ok, ErrMsg);

		if (!Ok) {
			Notify->OnNotify(TNotifyType::ntErr, ErrMsg);
			Notify->OnNotify(TNotifyType::ntErr, "Should reset socket...");
			return false;
		}

		return true;
	} catch (const PExcept& Except) {
		Notify->OnNotify(TNotifyType::ntErr, "An exception occurred while writing to socket!");
		Notify->OnNotify(TNotifyType::ntErr, Except->GetMsgStr());
		return false;
	}
}

bool TAdriaCommunicator::Write(const TChA& Msg) {
	return Write(TMIn::New(Msg));
}

void TAdriaCommunicator::Connect() {
	try {
		Notify->OnNotify(TNotifyType::ntInfo, "Connecting...");

		TSockEvent::Reg(this);
		TSockHost::GetAsyncSockHost(GetUrl(), this);
	} catch (const PExcept& Except) {
		Notify->OnNotifyFmt(TNotifyType::ntErr, "Failed to connect: %s", Except->GetMsgStr().CStr());
	} catch (...) {
		Notify->OnNotify(TNotifyType::ntErr, "An unknown exception occurred while connecting!");
		throw TExcept::New("Failed to connect socket!", "TAdriaClient::Connect()");
	}
}

void TAdriaCommunicator::CloseConn() {
	Notify->OnNotify(TNotifyType::ntInfo, "TAdriaClient::CloseConn(): Disconnecting...");

	Sock.Clr();

	Notify->OnNotify(TNotifyType::ntInfo, "TAdriaClient::CloseConn(): Disconnected!");
}

void TAdriaCommunicator::Reconnect() {
	Notify->OnNotify(TNotifyType::ntInfo, "Reseting connection...");

	try {
		TSockEvent::UnReg(this);

		sleep(1);

		Connect();

		Notify->OnNotify(TNotifyType::ntInfo, "Connection reset!");
	} catch (const PExcept& Except) {
		Notify->OnNotify(TNotifyType::ntErr, "An unknown exception occurred while reconnecting!");
		Notify->OnNotify(TNotifyType::ntErr, Except->GetMsgStr());
	}
}

void TAdriaCommunicator::ShutDown() {
	IsClosed = true;
	CloseConn();
}

void TAdriaCommunicator::OnMsgReceived(const PAdriaMsg Msg) {
	TVec<PAdriaMsgCallback> TempCallbacks;
	{
		TLock Lock(CallbackSection);
		TempCallbacks.AddV(MsgCallbacks);
	}

	CurrMsg = TAdriaMsg::New(Notify);
	for (int i = 0; i < TempCallbacks.Len(); i++) {
		TempCallbacks[i]->OnMsgReceived(Msg);
	}
}

void TAdriaCommunicator::OnAdriaConnected() {
	Notify->OnNotify(TNotifyType::ntInfo, "OnAdriaConnected called...");

	try {
		for (int i = 0; i < MsgCallbacks.Len(); i++) {
			MsgCallbacks[i]->OnConnected();
		}
	} catch (const PExcept& Except) {
		Notify->OnNotify(TNotifyType::ntErr, "An exception ocurred whole notifying connection!");
		Notify->OnNotify(TNotifyType::ntErr, Except->GetMsgStr());
	}
}

void TAdriaCommunicator::AddOnMsgReceivedCallback(const PAdriaMsgCallback& Callback) {
	TLock Lock(CallbackSection);

	MsgCallbacks.Add(Callback);
}


/////////////////////////////////////////////////////////////////////////////
// Adria - Server
TAdriaApp::TAdriaApp(const PSockEvent& _Communicator, TDataProvider& _DataProvider, const PNotify& _Notify):
		DataProvider(_DataProvider),
		Communicator(_Communicator),
		Notify(_Notify) {

	((TAdriaCommunicator*) Communicator())->AddOnMsgReceivedCallback(this);
	DataProvider.SetPredictionCallback(this);
	DataProvider.SetRulesGeneratedCallback(this);
}

void TAdriaApp::OnMsgReceived(const PAdriaMsg& Msg) {
	try {
		if (Msg->IsPush() && Msg->GetCommand() == TAdriaMsg::RES_TABLE) {
			ProcessPushTable(Msg);
		} else if (Msg->IsGet() && Msg->GetCommand() == TAdriaMsg::HISTORY) {
			ProcessGetHistory(Msg);
		} else if (Msg->IsGet() && Msg->GetCommand() == TAdriaMsg::PREDICTION) {
			ProcessGetPrediction(Msg);
		} else {
			Notify->OnNotifyFmt(TNotifyType::ntWarn, "Invalid message: %s", Msg->GetStr().CStr());
		}
	} catch (const PExcept& Except) {
		Notify->OnNotify(TNotifyType::ntErr, "Failed to process the received message!");
		Notify->OnNotify(TNotifyType::ntErr, Except->GetMsgStr());
	}
}

void TAdriaApp::OnConnected() {
	try {
		DataProvider.OnConnected();
	} catch (const PExcept& Except) {
		Notify->OnNotify(TNotifyType::ntErr, "Failed to run TAdriaServer::OnConnected()");
		Notify->OnNotify(TNotifyType::ntErr, Except->GetMsgStr());
	}
}

void TAdriaApp::OnPrediction(const TInt& CanId, const TFlt& Val) {
	try {
		TInt PredCanId = TDataProvider::CanIdPredCanIdH.GetDat(CanId);

		float Value = (float) Val;

		Notify->OnNotifyFmt(TNotifyType::ntInfo, "Float length: %d", sizeof(Value));

		TChA ContentChA = "";
		ContentChA += (char) PredCanId.Val;
		ContentChA += (char) 1;	// type float

		char* ValueCh = (char*) &Value;
		for (int i = 0; i < 4; i++) {
			ContentChA += *(ValueCh + i);
		}

		TChA Msg = "PUSH res_table\r\nLength=" + TInt(ContentChA.Len()).GetStr() + "\r\n" + ContentChA + "\r\n";

		((TAdriaCommunicator*) Communicator())->Write(Msg);
	} catch (const PExcept& Except) {
		Notify->OnNotify(TNotifyType::ntErr, "Failed to process prediction callback!");
		Notify->OnNotify(TNotifyType::ntErr, Except->GetMsgStr());
	}
}

void TAdriaApp::OnRulesGenerated(const TVec<TPair<TStrV,TStr>>& RuleV) {
	try {
		TChA RuleStr = "";
		for (int RuleIdx = 0; RuleIdx < RuleV.Len(); RuleIdx++) {
			const TStrV& CauseStrV = RuleV[RuleIdx].Val1;
			const TStr& EffectStr = RuleV[RuleIdx].Val2;

			RuleStr += "(";

			// construct the causes
			for (int CauseIdx = 0; CauseIdx < CauseStrV.Len(); CauseIdx++) {
				RuleStr += CauseStrV[CauseIdx];
				if (CauseIdx < CauseStrV.Len()-1) {
					RuleStr += ',';
				}
			}

			RuleStr += "=>";
			RuleStr += EffectStr + ")";

			if (RuleIdx < RuleV.Len()-1) {
				RuleStr += ',';
			}
		}

		// write the rules to a file
		{
			TChA DbPath = DataProvider.GetDbPath();

			if (DbPath.LastCh() != '/') {
				DbPath += '/';
			}

			DbPath += "rules.log";

			TFOut FOut(DbPath, true);

			TChA LogStr = TStr("\n==============================================\n") + RuleStr;
			FOut.PutStr(LogStr);
		}

		TChA Msg = TChA("PUSH rules\r\nLength=") + TInt(RuleStr.Len()).GetStr() + "\r\n" + RuleStr + "\r\n";
		((TAdriaCommunicator*) Communicator())->Write(Msg);
	} catch (const PExcept& Except) {
		Notify->OnNotify(TNotifyType::ntErr, "Failed to process rules generated callback!");
		Notify->OnNotify(TNotifyType::ntErr, Except->GetMsgStr());
	}
}

void TAdriaApp::ShutDown() {
	((TAdriaCommunicator*) Communicator())->ShutDown();
}

void TAdriaApp::ParseTable(const TChA& Table, THash<TUInt, TFlt>& CanIdValH) {
	int NEntries = Table.Len() / TAdriaMsg::BYTES_PER_EL;

	int StartIdx;
	TUInt CanId;
	float Val;
	unsigned char Type;
	for (int EntryIdx = 0; EntryIdx < NEntries; EntryIdx++) {
		StartIdx = EntryIdx*TAdriaMsg::BYTES_PER_EL;

		CanId = TUInt((unsigned char) Table[StartIdx]);
		Type = Table[StartIdx + 1];

		switch (Type) {
		case 0: {
			Val = (float) Table[StartIdx + 2];
			break;
		} case 1: {
			Val = *((float*) (Table.CStr() + StartIdx + 2));
			break;
		} default: {
			throw TExcept::New("Invalid type of measurement!!");
		}
		}

		CanIdValH.AddDat(CanId, Val);
	}
}


void TAdriaApp::ProcessPushTable(const PAdriaMsg& Msg) {
	try {
		TStr TimeStr = TAdriaUtils::TUtils::GetCurrTimeStr();

		// parse the table
		THash<TUInt, TFlt> CanIdValH;
		const TChA& Table = Msg->GetContent();

		ParseTable(Table, CanIdValH);

		TUIntV KeyV;	CanIdValH.GetKeyV(KeyV);
		for (int i = 0; i < KeyV.Len(); i++) {
			TUInt CanId = KeyV[i];
			TFlt Val = CanIdValH(CanId);

			PJsonVal RecJson = TJsonVal::NewObj();
			RecJson->AddToObj("timestamp", TimeStr);
			RecJson->AddToObj("value", Val);

			DataProvider.AddRec(CanId, RecJson);
		}
	} catch (const PExcept& Except) {
		Notify->OnNotify(TNotifyType::ntErr, "Failed to process PUSH res_table!");
		Notify->OnNotify(TNotifyType::ntErr, Except->GetMsgStr());
	}
}

void TAdriaApp::ProcessGetHistory(const PAdriaMsg& Msg) {
	Notify->OnNotify(TNotifyType::ntInfo, "Received history request...");

	try {
		const TStr ComponentId = Msg->GetComponentId();
		const TInt CanId = TStr(Msg->GetParams()).GetInt();

		TUInt64FltKdV HistoryV;	DataProvider.GetHistory(CanId, HistoryV);

		int NHist = HistoryV.Len();

		// content
		TChA ContentChA = "";
		for (int i = 0; i < NHist; i++) {
			const TUInt64FltKd& HistEntry = HistoryV[i];

			ContentChA += HistEntry.Key.GetStr();

			ContentChA += ',';

			ContentChA += HistEntry.Dat.GetStr();

			if (i < NHist-1) {
				ContentChA += ',';
			}
		}

		ContentChA += "\r\n";

		// generate PUSH message
		// first line
		TChA Msg = "PUSH history?";
		Msg += CanId.GetStr();
		Msg += "&";
		Msg += ComponentId;
		Msg += "\r\n";

		// second line
		Msg += "Length=";
		Msg += TInt(ContentChA.Len()-2).GetStr();
		Msg += "\r\n";

		Msg += ContentChA;

		Notify->OnNotifyFmt(TNotifyType::ntInfo, "Sending history response. Number of values: %d", NHist);

		// write the message to the socket
		((TAdriaCommunicator*) Communicator())->Write(Msg);
	} catch (const PExcept& Except) {
		Notify->OnNotify(TNotifyType::ntErr, "Failed to process GET history!");
		Notify->OnNotify(TNotifyType::ntErr, Except->GetMsgStr());
	}
}

void TAdriaApp::ProcessGetPrediction(const PAdriaMsg& Msg) {
	try {
		Notify->OnNotify(TNotifyType::ntInfo, "Received prediction request!");
		DataProvider.MakePredictions();
	} catch (const PExcept& Except) {
		Notify->OnNotify(TNotifyType::ntErr, "Failed to process GET prediction!");
		Notify->OnNotify(TNotifyType::ntErr, Except->GetMsgStr());
	}
}
