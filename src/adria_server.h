#ifndef ADRIA_SERVER_H_
#define ADRIA_SERVER_H_

#include <base.h>
#include <net.h>
#include <mine.h>
#include <thread.h>
#include <processing.h>

namespace TAdriaUtils {

class TUtils {
public:
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

}

namespace TDataAccess {

class TPredictionCallback {
public:
	virtual void OnPrediction(const TInt& CanId, const TFlt& Val) = 0;
	virtual ~TPredictionCallback() {}
};

class TRulesGeneratedCallback {
public:
	virtual void OnRulesGenerated(const TVec<TPair<TStrV,TStr>>& RuleV) = 0;
	virtual ~TRulesGeneratedCallback() {}
};

/////////////////////////////////////////////////////////////////////
// Backup log and backup DB handler
class TDataProvider {
private:
	// A thread that periodically updates the history table
	class TSampleHistThread: public TThread {
	public:
		static uint64 SleepTm;
		static uint64 SampleWaterLevelTm;

	private:
		TDataProvider* DataProvider;
		bool Running;

		PNotify Notify;

	public:
		TSampleHistThread(TDataProvider* Provider, const PNotify& _Notify);
		void Run();
		void Stop() { Running = false; }
	};

	// a thread that periodically recomputes rules which are
	// then sent to UMKO
	class TRuleThread: public TThread {
	private:
		static uint64 SleepTm;

		TDataProvider* DataProvider;
		bool Running;

		PNotify Notify;

	public:
		TRuleThread(TDataProvider* Provider, const PNotify& _Notify);
		void Run();
		void Stop() { Running = false; }
	};

private:
	static TIntStrH CanIdVarNmH;
	static TIntSet PredCanSet;
	static uint64 HistDur;
	static int EntryTblLen;
	static bool FillCanHs();
	static bool Init;

	static TIntV RuleEffectCanV;
	static TIntV RuleObsCanV;
	static TIntIntH RuleEventCanIdIdxH;
	static TIntIntH RuleObsCanIdIdxH;

	const TStr DbPath;
	TFltV EntryTbl;				// current state
	THash<TInt, TUInt64FltKdV> HistH;	// history for showing graphs and making predictions
	TVec<TKeyDat<TUInt64,TFltV>> RuleInstV;	// table that contains values used to learn association rules
	TUInt64FltPrV WaterLevelV;

	TLinRegWrapper WaterLevelReg;

	PThread HistThread;
	PThread RuleThread;

	TPredictionCallback* PredictionCallback;
	TRulesGeneratedCallback* RulesCallback;

	TCriticalSection DataSection;
	TCriticalSection HistSection;
	TCriticalSection RuleSection;
	PNotify Notify;

public:
	TDataProvider(const TStr& DbPath, const PNotify& _Notify);

	virtual ~TDataProvider() { }

//	static void InitAggregates(TQm::PBase& Base, const PNotify& Notify);
public:
	void OnConnected();
	// stores a new record
	void AddRec(const int& CanId, const PJsonVal& Rec);
	// adds the current state to the table used for learning rules
	void AddRuleInstance(const int& CanId);
	// returns the history of the sensor with the given CAN ID
	void GetHistory(const int& CanId, TUInt64FltKdV& HistoryV);

	void SetPredictionCallback(TPredictionCallback* Callback) { PredictionCallback = Callback; }
	void SetRulesGeneratedCallback(TRulesGeneratedCallback* Callback) { RulesCallback = Callback; }

private:
	// saves a record
	void SaveRec(const int& CanId, const PJsonVal& Rec);
	// adds a record to the external log
	void AddRecToLog(const int& CanId, const PJsonVal& Rec);

	// sample data
	void SampleHistFromV(const TFltV& StateV, const uint64& SampleTm);
	void SampleHist();
	void SampleWaterLevel();

	// analytics
	void MakePredictions();
	// generate rules for UMKO
	void GenRules();
	// predicts when the fresh water will be empty
	void PredictWaterLevel();

	// makes a prediction for the desired CAN ID and fires an event
//	void PredictByCan(const int& CanId);

	// preprocesses the instance vectors for the APRIORI algorithm
	void PreprocessApriori(const TVec<TFltV>& EventInstV,
				const TVec<TFltV>& ObsInstV, TIntVV& EventMat, TIntVV& ObsMat);
	void InterpretApriori(const TVec<TPair<TIntV,TInt>>& RuleIdxV, TVec<TPair<TStrV,TStr>>& RuleV) const;

	// load methods
	void LoadStructs();
//	void LoadHistForCan(const TInt& CanId);
	void LoadHistV();
	void LoadRuleInstV();
	void LoadWaterLevelV();

	// save methods
	void PersistHist();
//	void PersistHistForCan(const TInt& CanId);
	void PersistRuleInstV();
	void PersistWaterLevelV();

private:
	// helpers
	// removes directory Dest and copies Src into it
	void CopyDir(const TStr& Src, const TStr& Dest);

public:
	const TStr& GetDbPath() const { return DbPath; }
};

}

namespace TAdriaComm {

using namespace TDataAccess;

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
	TAdriaMsg(const PNotify& _Notify=TStdNotify::New()):
		Buff(400), Method(TAdriaMsgMethod::ammNone), Command(TStr()),
		Params(TStr()), Length(-1), IsLastReadEol(true), Notify(_Notify) {}

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

class TAdriaMsgCallback;
typedef TPt<TAdriaMsgCallback> PAdriaMsgCallback;
class TAdriaMsgCallback {
private:
	TCRef CRef;
public:
	friend class TPt<TAdriaMsgCallback>;
public:
	virtual void OnMsgReceived(const PAdriaMsg& Msg) = 0;
	virtual void OnConnected() = 0;

	virtual ~TAdriaMsgCallback() {}
};


/////////////////////////////////////////////////////////
// Adria socket client
// receives messages and calls the callback
class TAdriaCommunicator : public TSockEvent {
private:
	const TStr Url;
	const int Port;

	PSockHost SockHost;
	PSock Sock;

	PNotify Notify;

	PAdriaMsg CurrMsg;

	TVec<PAdriaMsgCallback> MsgCallbacks;

	TCriticalSection SocketSection;
	TCriticalSection CallbackSection;

	bool IsClosed;

public:
	TAdriaCommunicator(const TStr& _Url, const int& _Port, const PNotify& _Notify=TStdNotify::New());

public:
	static PSockEvent New(const TStr& _Url, const int& _Port, const PNotify& _Notify=TStdNotify::New())
		{ return new TAdriaCommunicator(_Url, _Port, _Notify); }

	~TAdriaCommunicator() { CloseConn(); /*~TSockEvent();*/ }

public:
	TSockEvent& operator=(const TSockEvent&){Fail; return *this;}

public:
	void OnRead(const uint64& SockId, const PSIn& SIn);
	void OnWrite(const uint64& SockId) { Notify->OnNotify(TNotifyType::ntInfo, "Written to socket!"); }
	void OnAccept(const uint64& SockId, const PSock&){Fail;}
	void OnConnect(const uint64& SockId);
	void OnReadEof(const uint64& SockId);
	void OnClose(const uint64& SockId);
	void AfterClose(const uint64& SockId);
	void OnTimeOut(const uint64& SockId);// { OnClose(SockId); }
	void OnError(const uint64& SockId, const int& ErrCd, const TStr& ErrStr);
	void OnGetHost(const PSockHost& SockHost);

	bool Write(const PSIn& SIn);
	bool Write(const TChA& Msg);
	void ShutDown();

public:
	const TStr& GetUrl() const { return Url; }
	const int& GetPort() const { return Port; }

	void AddOnMsgReceivedCallback(const PAdriaMsgCallback& Callback);
	void OnMsgReceived(const PAdriaMsg Msg);
	void OnAdriaConnected();

private:
	void Connect();
	void CloseConn();
	void Reconnect();
};




class TAdriaServer;
typedef TPt<TAdriaServer> PAdriaServer;
class TAdriaServer: public TAdriaMsgCallback, public TPredictionCallback,
					public TRulesGeneratedCallback {
private:
	TCRef CRef;
public:
	friend class TPt<TAdriaServer>;
private:
	TDataProvider& DataProvider;
	PSockEvent Communicator;

	PNotify Notify;
public:
	TAdriaServer(const PSockEvent& _Communicator, TDataProvider& _DataProvider, const PNotify& _Notify = TStdNotify::New());
	static PAdriaServer New(const PSockEvent& _Communicator, TDataProvider& _DataProvider, const PNotify& _Notify = TStdNotify::New())
		{ return new TAdriaServer(_Communicator, _DataProvider, _Notify); }

	~TAdriaServer() {}

public:
	void OnMsgReceived(const PAdriaMsg& Msg);
	void OnConnected();
	void OnPrediction(const TInt& CanId, const TFlt& Val);
	void OnRulesGenerated(const TVec<TPair<TStrV,TStr>>& RuleV);

	void ShutDown();

private:
	void ParseTable(const TChA& Table, THash<TUInt, TFlt>& CanIdValH);
	void ProcessPushTable(const PAdriaMsg& Msg);
	void ProcessGetHistory(const PAdriaMsg& Msg);
	void ProcessGetPrediction(const PAdriaMsg& Msg);
};


}

#endif /* ADRIA_SERVER_H_ */
