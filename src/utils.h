#ifndef UTILS_H_
#define UTILS_H_

#include <base.h>
#include <net.h>
#include <mine.h>

namespace TAdriaUtils {

class TUtils {
public:
	const static int FreshWaterCanId;
	const static int WasteWaterCanId;

	static uint64 GetCurrTimeStamp();
	static TStr GetCurrTimeStr();

	// file names
	static TStr GetLogFName(const TStr& DbPath) { return DbPath + "/readings.log"; }
	static TStr GetHistFName(const TStr& DbPath) { return DbPath + "/history.bin"; }
	static TStr GetHistBackupFName( const TStr& DbPath) { return DbPath + "/history-backup.bin"; }
	static TStr GetRuleFName(const TStr& DbPath) { return DbPath + "/rule_instances.bin"; }
	static TStr GetBackupRuleFName(const TStr& DbPath) { return DbPath + "/rule_instances-backup.bin"; }
	static TStr GetWaterLevelFNm(const TStr& DbPath) { return DbPath + "/water_level.bin"; }
	static TStr GetBackupWLevelFNm(const TStr& DbPath) { return DbPath + "/water_level-backup.bin"; }
	static TStr GetWaterLevelRegFNm(const TStr& DbPath) { return DbPath + "/models/water_level-predict.bin"; }
	static TStr GetBackupWaterLevelRegFNm(const TStr& DbPath) { return DbPath + "/models/water_level-predict-backup.bin"; }
	static TStr GetWaterLevelInstancesLogFNm(const TStr& DbPath) { return DbPath + "/water_level.log"; }

	static void PrintItemSetV(const TVec<TPair<TFlt, TIntV>>& ItemSetSuppV, const PNotify& Notify);
	static void PrintRuleCandV(const TVec<TPair<TFlt,TPair<TIntV,TInt>>>& RuleCandV, const PNotify& Notify);

	// persist
	template <class TStruct>
	static void PersistStruct(const TStr& StructFNm, const TStr& StructBackupFNm, TStruct& Struct, const PNotify& Notify) {
		Notify->OnNotify(TNotifyType::ntInfo, "Persisting structure...");

		try {
			// remove the file and create a new file
			if (TFile::Exists(StructFNm)) {
				TFile::Del(StructFNm);
			}

			// save a new rule file
			{
				TFOut Out(StructFNm);
				Struct.Save(Out);
			}

			// the new file is created, now create a new backup file
			// first remove the old backup file
			if (TFile::Exists(StructBackupFNm)) {
				TFile::Del(StructBackupFNm);
			}

			// save a new rule file
			{
				TFOut Out(StructBackupFNm);
				Struct.Save(Out);
			}
		} catch (const PExcept& Except) {
			Notify->OnNotify(TNotifyType::ntErr, "Failed to persist structure!");
			Notify->OnNotify(TNotifyType::ntErr, Except->GetMsgStr());
		}
	}

	// tries to load a structure from a file `StructFNm` or a backup file `StructBackupFNm`
	// returns true if success
	template <class TStruct>
	static bool LoadStruct(const TStr& StructFNm, const TStr& StructBackupFNm, TStruct& Struct, const PNotify& Notify) {
		Notify->OnNotify(TNotifyType::ntInfo, "Loading structure...");

		try {
			bool Success = false;

			if (TFile::Exists(StructFNm)) {
				try {
					TFIn SIn(StructFNm);
					Struct = TStruct(SIn);
					Success = true;
				} catch (const PExcept& Except) {
					Notify->OnNotify(TNotifyType::ntErr, "TDataProvider::LoadWaterLevelV: An exception occurred while loading water levels!");
					Notify->OnNotify(TNotifyType::ntErr, Except->GetMsgStr());
				}
			}

			if (!Success && TFile::Exists(StructBackupFNm)) {
				try {
					TFIn SIn(StructBackupFNm);
					Struct = TStruct(SIn);
					Success = true;
				} catch (const PExcept& Except) {
					Notify->OnNotify(TNotifyType::ntErr, "TDataProvider::LoadWaterLevelV: An exception occurred while loading backup water levels!");
					Notify->OnNotify(TNotifyType::ntErr, Except->GetMsgStr());
				}
			}
			return Success;
		} catch (const PExcept& Except) {
			Notify->OnNotify(TNotifyType::ntErr, "Failed to load structure!");
			Notify->OnNotify(TNotifyType::ntErr, Except->GetMsgStr());
			return false;
		}
	}
};

enum TAdriaMsgMethod {
	ammNone,
	ammPush,
	ammPost,
	ammGet
};

/////////////////////////////////////////////////////////
// Adria Message class
// parses the message from the input stream and holds it's content
class TAdriaMsg;
typedef TPt<TAdriaMsg> PAdriaMsg;
class TAdriaMsg{
private:
  TCRef CRef;
public:
  friend class TPt<TAdriaMsg>;
public:
	const static TChA POST;
	const static TChA PUSH;
	const static TChA GET;

	const static TChA RES_TABLE;
	const static TChA HISTORY;
	const static TChA PREDICTION;
	const static int BYTES_PER_EL;

private:
	TChA Buff;

	TAdriaMsgMethod Method;
	TChA Command;
	TChA Params;
	TChA ComponentId;
	TInt Length;
	TChA Content;

	bool IsLastReadEol;

	PNotify Notify;

public:
	TAdriaMsg(const PNotify& _Notify=TStdNotify::New());

	static PAdriaMsg New(const PNotify& Notify=TStdNotify::New()) { return new TAdriaMsg(Notify); }

	virtual ~TAdriaMsg() {}

public:
	TStr GetStr() const;

	bool IsComplete() const;
	void Read(const PSIn& In);

	bool IsPush() const { return IsMethod(TAdriaMsgMethod::ammPush); }
	bool IsPost() const { return IsMethod(TAdriaMsgMethod::ammPost); }
	bool IsGet() const { return IsMethod(TAdriaMsgMethod::ammGet); }

	const TChA& GetCommand() const { return Command; }
	const TChA& GetContent() const { return Content; }
	const TChA& GetComponentId() const { return ComponentId; }
	const TChA& GetParams() const { return Params; }

private:
	void ReadUntil(const PSIn& In, const TStr& EndStr, TChA& Out) const;
	void ReadLine(const PSIn& In, TChA& Out) const;

public:
	bool HasMethod() const { return Method != TAdriaMsgMethod::ammNone; }
	bool HasCommand() const { return !Command.Empty(); }
	bool HasComponentId() const { return !ComponentId.Empty(); }
	bool HasParams() const { return !Params.Empty(); }
	bool HasContent() const { return IsPush() || IsPost(); }

private:
	bool IsMethod(const TAdriaMsgMethod& Mtd) const { return Method == Mtd; }

	static bool BuffsEq(const char* Buff1, const char* Buff2, const int& BuffLen);
};

}

#endif /* UTILS_H_ */
