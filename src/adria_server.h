#ifndef ADRIA_SERVER_H_
#define ADRIA_SERVER_H_

#include <base.h>
#include <net.h>
#include <mine.h>
#include <thread.h>
#include <analytics.h>
#include <utils.h>

namespace TAdriaServer {

using namespace TAdriaUtils;
using namespace TAdriaAnalytics;

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

//	class TOnlineRuleThread: public TThread {
//	private:
//		const static uint64 SLEEP_TM;
//
//		TDataProvider* DataProvider;
//		PNotify Notify;
//	public:
//		TOnlineRuleThread(TDataProvider* Provider, const PNotify& _Notify);
//
//		void Run();
//	};

public:
	static TIntIntH CanIdPredCanIdH;

private:
	const static bool LOG_READINGS;

	static TIntStrH CanIdVarNmH;
	static uint64 HistDur;
	static uint64 RuleWindowTm;
	static int EntryTblLen;
	static bool FillCanHs();
	static bool Init;

	static TIntV RuleEffectCanV;
	static TIntV RuleObsCanV;
	static TIntIntH RuleEventCanIdIdxH;
	static TIntIntH RuleObsCanIdIdxH;

	const TStr DbPath;
	TFltV EntryTbl;								// current state
	THash<TInt, TUInt64FltKdV> HistH;			// history for showing graphs and making predictions
	TVec<TKeyDat<TUInt64,TFltV>> RuleInstV;		// table that contains values used to learn association rules
	TUInt64FltPrV WaterLevelV;

	TLinRegWrapper WaterLevelReg;
//	TOnlineRuleGenerator RuleGenerator;

	PThread HistThread;
	PThread RuleThread;
//	PThread OnlineRuleThread;

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
	void DelOldRuleInst();
	// returns the history of the sensor with the given CAN ID
	void GetHistory(const int& CanId, TUInt64FltKdV& HistoryV);

	// predictions
	// predicts when the battery will be empty
	double PredictBattery();
	// predicts when the fresh water will be empty
	double PredictFreshWaterLevel(const bool& NotifySrv=true);
	// predicts when the waste water will be full
	double PredictWasteWaterLevel();
	void MakePredictions();

	// update fresh water prediction model
	void LearnFreshWaterLevel();

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
	void CpyStateV(TFltV& StateV);

	// generate rules for UMKO
	void GenRules();

	// preprocesses the instance vectors for the APRIORI algorithm
	void PreprocessApriori(const TVec<TFltV>& EventInstV,
				const TVec<TFltV>& ObsInstV, TIntVV& EventMat, TIntVV& ObsMat);
	void InterpretApriori(const TVec<TPair<TIntV,TInt>>& RuleIdxV, TVec<TPair<TStrV,TStr>>& RuleV) const;

	// load methods
	void LoadStructs();
	void LoadHistV();
	void LoadRuleInstV();
	void LoadWaterLevelV();

	// save methods
	void PersistHist();
	void PersistRuleInstV();
	void PersistWaterLevelV();

private:
	// helpers
	// removes directory Dest and copies Src into it
	void CopyDir(const TStr& Src, const TStr& Dest);

public:
	const TStr& GetDbPath() const { return DbPath; }
};

/////////////////////////////////////////////////////////
// Callback for messages
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




class TAdriaApp;
typedef TPt<TAdriaApp> PAdriaApp;
class TAdriaApp: public TAdriaMsgCallback, public TPredictionCallback,
					public TRulesGeneratedCallback {
private:
	TCRef CRef;
public:
	friend class TPt<TAdriaApp>;
private:
	TDataProvider& DataProvider;
	PSockEvent Communicator;

	PNotify Notify;
public:
	TAdriaApp(const PSockEvent& _Communicator, TDataProvider& _DataProvider, const PNotify& _Notify = TStdNotify::New());
	static PAdriaApp New(const PSockEvent& _Communicator, TDataProvider& _DataProvider, const PNotify& _Notify = TStdNotify::New())
		{ return new TAdriaApp(_Communicator, _DataProvider, _Notify); }

	virtual ~TAdriaApp() {}

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
